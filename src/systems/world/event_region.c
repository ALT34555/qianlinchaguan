/* 区域事件落点与逐帧推进 */
#include "planet_macro.h"
#include "surface_internal.h"
#include "surface_stream.h"
#include "ice_runtime.h"
#include "../../core/math_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>

void qlcg_apply_region_event(RegionData *reg, MacroWorld *w, NaturalEventType type,
                             float lx, float lz, int season_idx, const GameSettings *s) {
    if (!reg) return;
    if (reg->stream) {
        float wx0 = reg->center_x + lx, wz0 = reg->center_z + lz;
        float h_old = 0.0f;
        bool cold = (type == NAT_EVENT_COLD_WAVE) && w && w->ice_state && w->ice_time_valid;
        if (cold) {
            SurfaceWater s0;
            IceSite i0;
            qlcg_sample_water(w, wx0, wz0, season_idx, &s0, &i0);
            IceSample o0;
            qlcg_ice_evaluate(&i0, &w->ice_time, &o0);
            h_old = o0.thickness;
        }
        qlcg_trigger_macro_event(w,type,wx0,wz0);
        if (cold) {
            /* 新旧基线差作为稀疏过渡起点，不当帧造厚冰 */
            SurfaceWater s1;
            IceSite i1;
            qlcg_sample_water(w, wx0, wz0, season_idx, &s1, &i1);
            IceSample o1;
            qlcg_ice_evaluate(&i1, &w->ice_time, &o1);
            qlcg_ice_thermal_begin(w, wx0, wz0, w->ice_time.climate_day,
                                   h_old - o1.thickness, 0.0f, 2.0f);
        }
        qlcg_region_stream_refresh(reg,-FLT_MAX,-FLT_MAX,FLT_MAX,FLT_MAX,season_idx,s);
        if (cold) qlcg_ice_refresh_all(reg);
        return;
    }
    qlcg_eng_push_undo(reg, w);
    float wx = reg->center_x + lx;
    float wz = reg->center_z + lz;
    qlcg_trigger_macro_event(w, type, wx, wz);

    float span = (float)(reg->width - 1) * reg->step;
    float half = span * 0.5f;
    float rad  = 115.0f;

    for (int z = 0; z < reg->height; z++) {
        for (int x = 0; x < reg->width; x++) {
            int idx = z * reg->width + x;
            float px = (float)x * reg->step - half;
            float pz = (float)z * reg->step - half;
            float d  = hypotf(px - lx, pz - lz);
            if (d > rad) continue;
            float k = 1.0f - d / rad;
            if (type == NAT_EVENT_EARTHQUAKE) {
                float crack = sinf((px - lx) * 0.09f + (pz - lz) * 0.07f);
                reg->heights[idx] += crack * 24.0f * k;
            } else if (type == NAT_EVENT_DESERTIFY) {
                reg->desert[idx]  = qlcg_clampf(reg->desert[idx] + 0.78f * k, 0.0f, 1.0f);
                reg->wetness[idx] = qlcg_clampf(reg->wetness[idx] - 0.45f * k, 0.0f, 1.0f);
                reg->forest[idx]  = qlcg_clampf(reg->forest[idx] - 0.70f * k, 0.0f, 1.0f);
            } else if (type == NAT_EVENT_FLOOD || type == NAT_EVENT_TYPHOON) {
                reg->water_levels[idx] = qlcg_maxf(reg->water_levels[idx], reg->heights[idx] + 3.8f * k);
                reg->wetness[idx]      = qlcg_clampf(reg->wetness[idx] + 0.48f * k, 0.0f, 1.0f);
                reg->water_form        = WATER_FORM_FLOOD;
            } else if (type == NAT_EVENT_COLD_WAVE) {
                /* 只改一个权威气温来源；冰按过渡缓变，不当帧造厚冰 */
                reg->temperatures[idx] -= 15.0f * k;
                if (reg->ice)
                    reg->ice->annual_c[idx] = (int16_t)qlcg_clampi(
                        (int)lrintf(reg->temperatures[idx] * 100.0f), -32768, 32767);
            } else if (type == NAT_EVENT_WILDFIRE) {
                reg->forest[idx] = qlcg_clampf(reg->forest[idx] - 0.85f * k, 0.0f, 1.0f);
                reg->desert[idx] = qlcg_clampf(reg->desert[idx] + 0.42f * k, 0.0f, 1.0f);
            }
        }
    }
    qlcg_ice_chunk_invalidate(reg);
    if (type == NAT_EVENT_COLD_WAVE && w && w->ice_state) {
        /* 以事件中心估算新旧基线差，用稀疏过渡让冰逐步增厚 */
        int cx = qlcg_clampi((int)lrintf((lx + half) / reg->step), 0, reg->width - 1);
        int cz = qlcg_clampi((int)lrintf((lz + half) / reg->step), 0, reg->height - 1);
        float wxc = reg->center_x + lx, wzc = reg->center_z + lz;
        float annual_old = reg->temperatures[cz * reg->width + cx] + 15.0f;
        float annual_new = reg->temperatures[cz * reg->width + cx];
        float h_old = qlcg_ice_baseline_h(w, reg, wxc, wzc, annual_old);
        float h_new = qlcg_ice_baseline_h(w, reg, wxc, wzc, annual_new);
        qlcg_ice_thermal_begin(w, wxc, wzc, w->ice_time.climate_day, h_old - h_new, 0.0f, 2.0f);
        qlcg_ice_refresh_all(reg);
    }
    qlcg_region_recolor(reg, w, season_idx);
    qlcg_region_scatter_props(reg, w, season_idx, s);
}

/* 推进自然灾害事件生命周期与震动/狂风/暴 */
bool qlcg_tick_natural_events(MacroWorld *w, RegionData *reg, float dt,
                              float *out_shake, float *out_wind, float *out_rain) {
    bool changed = false;
    float shake = 0.0f, wind = 0.2f, rain = 0.0f;
    w->sim_time += dt;

    for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
        ActiveNaturalEvent *ev = &w->events[i];
        if (!ev->active) continue;
        ev->age += dt;
        if (ev->age >= ev->duration) {
            ev->active = false;
            changed = true;
            continue;
        }
        float life = 1.0f - ev->age / ev->duration;
        ev->wx += ev->vx * dt;
        ev->wz += ev->vz * dt;
        if (ev->type == NAT_EVENT_EARTHQUAKE) shake = qlcg_maxf(shake, life * 1.25f);
        if (ev->type == NAT_EVENT_TYPHOON) {
            wind = qlcg_maxf(wind, 2.4f * life);
            rain = qlcg_maxf(rain, 0.95f * life);
        }
        if (ev->type == NAT_EVENT_FLOOD) rain = qlcg_maxf(rain, 0.80f * life);
    }

    if (out_shake) *out_shake = shake;
    if (out_wind)  *out_wind  = wind;
    if (out_rain)  *out_rain  = rain;
    (void)reg;
    return changed;
}

