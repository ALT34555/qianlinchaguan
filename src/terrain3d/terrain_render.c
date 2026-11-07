/* 山川单帧渲染：天空、地形、水面与天气 */
#include "terrain_view.h"
#include "core/dev_tools.h"
#include "sky_climate.h"
#include "../systems/world/gen/gen_cover.h"
#include "../systems/world/model/plant_material.h"
#include "../systems/world/model/rock_material.h"
#include "terrain_types.h"
#include "terrain_ice_cells.h"
#include "../core/math_util.h"
#include "../core/measurements.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifndef GL_SAMPLE_ALPHA_TO_COVERAGE
#define GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#endif

typedef struct {
    float x,y,z,distance,seed;
} SpringDraw;

static int collect_springs(const RegionData *c,float ox,float oz,const float cam[3],
                           SpringDraw *out,int capacity) {
    int count=0;
    if(c->water_form==WATER_FORM_CUTOFF||!c->source_effects) return 0;
    for(int i=0;i<c->spring_count&&count<capacity;i++) {
        SpringEmitter s=c->springs[i];
        float x=ox+s.x,z=oz+s.z;
        float distance=hypotf(x-cam[0],z-cam[2]);
        if(distance>=80) continue;
        float gx=(s.x+CHUNK_SIZE*.5f)/c->step,gz=(s.z+CHUNK_SIZE*.5f)/c->step;
        int ix=qlcg_clampi((int)gx,0,CHUNK_CELLS-1);
        int iz=qlcg_clampi((int)gz,0,CHUNK_CELLS-1);
        if(qlcg_cell_draws_ice(c,ix,iz)) continue;
        int idx=((int)floorf(gz+.5f)+1)*c->width+(int)floorf(gx+.5f)+1;
        if(!c->source_effects[idx].spring) continue;
        float y=qlcg_region_water_at(c,s.x,s.z);
        if(!isfinite(y)||y<=-1e8f) continue;
        distance=hypotf(distance,y-cam[1]);
        if(distance>=80) continue;
        out[count++]=(SpringDraw){x,y,z,distance,(s.seed&65535)/65536.0f};
    }
    return count;
}

void qlcg_terrain_render(TerrainRenderer *tr, const RegionData *reg, const MacroWorld *w,
                         const CalendarClock *clk, int utc_offset_min,
                         const GameSettings *s, int win_w, int win_h) {
    if (!reg || !reg->stream) return;
    if (clk) tr->weather_unix_ms = clk->unix_ms;
    qlcg_terrain_stream_update(tr, (RegionData *)reg, s);
    RegionStream *st = reg->stream;
    float aspect = (win_h > 0) ? ((float)win_w / (float)win_h) : 1.77f;
    float fov_rad = (float)s->fov * (QLCG_PI / 180.0f);

    /* 昼夜与天气调色（对齐 base Sky） */
    const WeatherProfile *wp = tr->cur_weather ? tr->cur_weather : qlcg_get_weather_profile(0);
    float precip_eff = tr->precip_intensity;
    qlcg_dev_weather(&wp, &precip_eff);
    precip_eff = qlcg_maxf(precip_eff, tr->rain_override);

    AstraFrame astro;
    bool astronomy = w->opt.astronomy_enabled && qlcg_astra_upload(&tr->astronomy, &w->opt);
    if (astronomy) {
        double lon = ((double)reg->center_x + tr->x) / w->circumference * 6.283185307179586;
        double lat = -((double)reg->center_z + tr->z) / qlcg_world_depth(w) * 3.141592653589793;
        lat = fmax(-1.570796326794897, fmin(1.570796326794897, lat));
        qlcg_astra_sample(&w->opt, clk, lon, lat, tr->astronomy.catalog, &astro);
    }
    SkyClimate sky;
    qlcg_sky_climate_astra(clk, utc_offset_min, tr->plant_season, wp, s->weather_atmosphere,
                     w->sim_time, precip_eff, astronomy ? &astro : NULL, &sky);
    float sx = sky.sun_dir[0], sy = sky.sun_dir[1], sz = sky.sun_dir[2];
    float sky_top[3] = { sky.zenith[0], sky.zenith[1], sky.zenith[2] };
    float sky_bot[3] = { sky.horizon[0], sky.horizon[1], sky.horizon[2] };

    /* base 水下雾与天空遮蔽 */
    float water_level=qlcg_region_water_at(reg,tr->x,tr->z);
    float bed_level=qlcg_region_height_at(reg,tr->x,tr->z);
    bool underwater=tr->y<water_level && tr->y>bed_level;
    if (underwater) {
        const float night[3]={8/255.0f,21/255.0f,35/255.0f};
        const float day[3]={22/255.0f,75/255.0f,96/255.0f};
        for (int k=0;k<3;k++) sky_top[k]=sky_bot[k]=qlcg_lerpf(night[k],day[k],sky.daylight);
    }

    float shake_x = (tr->cam_shake > 0.01f) ? sinf(w->sim_time * 42.0f) * tr->cam_shake * 0.35f : 0.0f;
    float shake_y = (tr->cam_shake > 0.01f) ? cosf(w->sim_time * 37.0f) * tr->cam_shake * 0.35f : 0.0f;

    float cp = cosf(tr->pitch), sp = sinf(tr->pitch);
    float cy = cosf(tr->yaw),   syaw = sinf(tr->yaw);
    float fwd[3] = { syaw * cp, sp, -cy * cp };
    float rgt[3] = { cy, 0.0f, syaw };
    float up[3]  = { -syaw * sp, cp, cy * sp };

    /* 相机坐标改用宏观格锚点基准，与地形顶点一致 */
    float cam_pos[3] = {
        (float)((double)tr->x + (double)reg->center_x - tr->anchor_x) + shake_x,
        tr->y + shake_y,
        (float)((double)tr->z + (double)reg->center_z - tr->anchor_z)
    };
    float proj[16], view[16], mvp[16];
    qlcg_mat4_perspective(proj, fov_rad, aspect, 0.15f, (st->radius+2)*CHUNK_SIZE*1.5f);
    qlcg_mat4_look_dir(view, cam_pos, tr->yaw, tr->pitch);
    qlcg_mat4_mul(proj, view, mvp);

    /* 地形与水体复用相机相对投影 */
    float relative_mvp[16];
    view[12] = view[13] = view[14] = 0.0f;
    qlcg_mat4_mul(proj, view, relative_mvp);

    /* 1. 绘制天空穹顶 */
    glDisable(GL_DEPTH_TEST);
    glUseProgram(tr->sky_prog);
    glUniform1i(qlcg_gl_uniform(tr->sky_prog, "uAstronomy"), astronomy);
    glUniform1i(qlcg_gl_uniform(tr->sky_prog, "uLunarPhase"), astronomy && wp->id < 900);
    if (astronomy) {
        qlcg_astra_uniforms(tr->sky_prog, &astro);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tr->astronomy.texture);
        glUniform1i(qlcg_gl_uniform(tr->sky_prog, "uAstralMap"), 0);
    }
    glUniform1i(qlcg_gl_uniform(tr->sky_prog,"uUnderwater"),underwater?1:0);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uCamFwd"), fwd[0], fwd[1], fwd[2]);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uCamRight"), rgt[0], rgt[1], rgt[2]);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uCamUp"), up[0], up[1], up[2]);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uTanHalfFov"), tanf(fov_rad * 0.5f));
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uAspect"), aspect);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uSunDir"), sx, sy, sz);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uHorizon"), sky_bot[0], sky_bot[1], sky_bot[2]);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uZenith"), sky_top[0], sky_top[1], sky_top[2]);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uGlowColor"), sky.glow[0], sky.glow[1], sky.glow[2]);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uGlow"), sky.glow_amt);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uMoonDir"), sky.moon_dir[0], sky.moon_dir[1], sky.moon_dir[2]);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uStar"), sky.star);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uMoon"), sky.moon);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uCloudColor"), sky.cloud[0], sky.cloud[1], sky.cloud[2]);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uRainCloudColor"), sky.rain_cloud[0], sky.rain_cloud[1], sky.rain_cloud[2]);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uRainAlpha"), s->weather_atmosphere ? sky.rain_alpha : 0.0f);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uFlash"), sky.flash);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uCloudCov"), s->decorative_clouds ? sky.cloud_cov : 0.0f);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uSimTime"), w->sim_time);
    if (!qlcg_dev_skip_draw(QLCG_DEV_SKIP_SKY)) {
        glBindVertexArray(tr->sky_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        if (astronomy && !underwater) qlcg_astra_draw(&tr->astronomy, &astro, &sky, fwd, rgt, up,
            aspect, tanf(fov_rad * .5f), w->sim_time, s->decorative_clouds ? sky.cloud_cov : 0,
            s->weather_atmosphere ? sky.rain_alpha : 0);
        if(astronomy && !underwater) qlcg_astra_draw_events(&tr->astronomy,w->astra_events,clk,&astro);
    }

    /* 2. 绘制视距圆内地形 */
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    float fog_near = 0.0f, fog_far = 1.0f;
    qlcg_fog_range((float)st->radius * (float)CHUNK_SIZE, wp, s->weather_atmosphere,
                   &fog_near, &fog_far);
    glUseProgram(tr->terrain_prog);
    glUniform1i(qlcg_gl_uniform(tr->terrain_prog,"uUnderwater"),underwater?1:0);
    glUniformMatrix4fv(qlcg_gl_uniform(tr->terrain_prog, "uMVP"), 1, GL_FALSE, relative_mvp);
    glUniform3f(qlcg_gl_uniform(tr->terrain_prog, "uSunDir"), sx, sy, sz);
    glUniform3f(qlcg_gl_uniform(tr->terrain_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
    glUniform3f(qlcg_gl_uniform(tr->terrain_prog, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
    glUniform2f(qlcg_gl_uniform(tr->terrain_prog, "uFogRange"), fog_near, fog_far);
    glUniform1f(qlcg_gl_uniform(tr->terrain_prog, "uLightScale"), sky.light_scale);
    glUniform3f(qlcg_gl_uniform(tr->terrain_prog, "uTint"), sky.tint[0], sky.tint[1], sky.tint[2]);
    glUniform1f(qlcg_gl_uniform(tr->terrain_prog, "uGroundDetail"), 1.0f);
    /* 按就地温度选相邻两个气候带纹理，带内连续插值 */
    float bp = 2.0f;
    if (reg->temperatures && reg->width > 0 && reg->height > 0) {
        int tmid = (reg->height / 2) * reg->width + reg->width / 2;
        bp = qlcg_cover_band_of_temp(reg->temperatures[tmid]);
    }
    int b0 = qlcg_clampi((int)bp, 0, QLCG_GROUND_BAND_N - 1);
    int b1 = qlcg_clampi(b0 + 1, 0, QLCG_GROUND_BAND_N - 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tr->ground_tex[b0]);
    glUniform1i(qlcg_gl_uniform(tr->terrain_prog, "uGroundTex0"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tr->ground_tex[b1]);
    glUniform1i(qlcg_gl_uniform(tr->terrain_prog, "uGroundTex1"), 1);
    glActiveTexture(GL_TEXTURE0);
    glUniform1f(qlcg_gl_uniform(tr->terrain_prog, "uGroundMix"),
                qlcg_clampf(bp - (float)b0, 0.0f, 1.0f));
    glUniform2f(qlcg_gl_uniform(tr->terrain_prog, "uGroundAnchor"),
                (float)fmod(tr->anchor_x, (double)QLCG_GROUND_TEX_SPAN),
                (float)fmod(tr->anchor_z, (double)QLCG_GROUND_TEX_SPAN));
    glUniform1f(qlcg_gl_uniform(tr->terrain_prog, "uGroundSpan"), QLCG_GROUND_TEX_SPAN);
    float b_rad = (tr->eng_tool != ENG_TOOL_NONE && tr->has_hover) ? tr->brush_radius : 0.0f;
    glUniform3f(qlcg_gl_uniform(tr->terrain_prog, "uBrushPos"), tr->hover_lx, tr->hover_ly, tr->hover_lz);
    glUniform1f(qlcg_gl_uniform(tr->terrain_prog, "uBrushRad"), b_rad);

    /* 合批：可见块一次 baseVertex 多重绘制 */
    const int vpb = (CHUNK_CELLS + 1) * (CHUNK_CELLS + 1);
    int md_n = 0;
    glUniform1i(qlcg_gl_uniform(tr->terrain_prog,"uCliffMaterial"),1);
    for (int i = 0; i < st->count; i++) {
        int slot = st->active[i];
        RegionData *c = st->slots[slot].data;
        if (!tr->chunks[slot].terrain_ready) continue;
        int dx = c->center_cx-st->center_cx, dz = c->center_cz-st->center_cz;
        if (dx*dx+dz*dz > (st->radius+0.5f)*(st->radius+0.5f)) continue;
        float wx = (float)((double)c->center_x - tr->anchor_x) - cam_pos[0];
        float wz = (float)((double)c->center_z - tr->anchor_z) - cam_pos[2];
        float wl = sqrtf(wx * wx + wz * wz);
        bool far_lod = wl>220.0f && !tr->chunks[slot].cliff_detail;
        tr->md_count[md_n] = far_lod ? tr->terrain_lod_count : tr->terrain_idx_count;
        tr->md_index[md_n] = far_lod ? (GLvoid *)(size_t)(tr->terrain_lod_off * (int)sizeof(uint32_t))
                                     : (GLvoid *)0;
        tr->md_base[md_n] = tr->chunks[slot].terrain_slot * vpb;
        md_n++;
        if (tr->chunks[slot].cliff_vertex_count && !qlcg_dev_skip_draw(QLCG_DEV_SKIP_TERRAIN)) {
            glBindVertexArray(tr->chunks[slot].cliff_vao);
            glDrawArrays(GL_TRIANGLES,0,tr->chunks[slot].cliff_vertex_count);
        }
    }
    glUniform1i(qlcg_gl_uniform(tr->terrain_prog,"uCliffMaterial"),0);
    if (md_n > 0 && glMultiDrawElementsBaseVertex && !qlcg_dev_skip_draw(QLCG_DEV_SKIP_TERRAIN)) {
        glBindVertexArray(tr->terrain_vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tr->terrain_ebo);
        glMultiDrawElementsBaseVertex(GL_TRIANGLES, tr->md_count, GL_UNSIGNED_INT,
                                      tr->md_index, md_n, tr->md_base);
    }

    /* 3. 绘制硬件实例化植被、岩石与领地旗帜 */
    if (qlcg_dev_skip_draw(QLCG_DEV_SKIP_PROP)) goto skip_props;
    glUseProgram(tr->prop_prog);
    glUniform1i(qlcg_gl_uniform(tr->prop_prog,"uUnderwater"),underwater?1:0);
    glUniformMatrix4fv(qlcg_gl_uniform(tr->prop_prog, "uMVP"), 1, GL_FALSE, mvp);
    glUniform3f(qlcg_gl_uniform(tr->prop_prog, "uSunDir"), sx, sy, sz);
    glUniform3f(qlcg_gl_uniform(tr->prop_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
    glUniform3f(qlcg_gl_uniform(tr->prop_prog, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
    glUniform2f(qlcg_gl_uniform(tr->prop_prog, "uFogRange"), fog_near, fog_far);
    glUniform1f(qlcg_gl_uniform(tr->prop_prog, "uLightScale"), sky.light_scale);
    glUniform3f(qlcg_gl_uniform(tr->prop_prog, "uTint"), sky.tint[0], sky.tint[1], sky.tint[2]);
    glUniform1f(qlcg_gl_uniform(tr->prop_prog, "uTime"), w->sim_time);
    glUniform1f(qlcg_gl_uniform(tr->prop_prog, "uWind"), qlcg_maxf(tr->wind_strength, wp->wind));

    if (tr->tree_inst_count > 0) {
        glBindVertexArray(tr->tree_vao);
        glDrawElementsInstanced(GL_TRIANGLES, tr->tree_mesh_indices, GL_UNSIGNED_SHORT, 0, tr->tree_inst_count);
    }
    if (tr->rock_inst_count > 0) {
        glBindVertexArray(tr->rock_vao);
        glDrawElementsInstanced(GL_TRIANGLES, tr->rock_mesh_indices, GL_UNSIGNED_SHORT, 0, tr->rock_inst_count);
    }
    if (tr->flag_inst_count > 0) {
        glUseProgram(tr->flag_prog);
        glUniform1i(qlcg_gl_uniform(tr->flag_prog,"uUnderwater"),underwater?1:0);
        glUniformMatrix4fv(qlcg_gl_uniform(tr->flag_prog, "uMVP"), 1, GL_FALSE, mvp);
        glUniform3f(qlcg_gl_uniform(tr->flag_prog, "uSunDir"), sx, sy, sz);
        glUniform3f(qlcg_gl_uniform(tr->flag_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
        glUniform3f(qlcg_gl_uniform(tr->flag_prog, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
        glUniform2f(qlcg_gl_uniform(tr->flag_prog, "uFogRange"), fog_near, fog_far);
        glUniform1f(qlcg_gl_uniform(tr->flag_prog, "uLightScale"), sky.light_scale);
        glUniform3f(qlcg_gl_uniform(tr->flag_prog, "uTint"), sky.tint[0], sky.tint[1], sky.tint[2]);
        glUniform1f(qlcg_gl_uniform(tr->flag_prog, "uTime"), w->sim_time);
        glUniform1f(qlcg_gl_uniform(tr->flag_prog, "uEnable"), s->natural_physics ? 1.0f : 0.0f);
        glBindVertexArray(tr->flag_vao);
        glDrawElementsInstanced(GL_TRIANGLES, tr->flag_mesh_indices, GL_UNSIGNED_SHORT, 0, tr->flag_inst_count);
    }

    /* 植物：真实网格按物种分批 */
    if (tr->plant_batch_count > 0 && tr->plant_inst_total > 0) {
        glUseProgram(tr->plant_prog);
        glUniform1i(qlcg_gl_uniform(tr->plant_prog,"uUnderwater"),underwater?1:0);
        glUniformMatrix4fv(qlcg_gl_uniform(tr->plant_prog, "uMVP"), 1, GL_FALSE, relative_mvp);
        glUniform3f(qlcg_gl_uniform(tr->plant_prog, "uSunDir"), sx, sy, sz);
        glUniform3f(qlcg_gl_uniform(tr->plant_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
        glUniform3f(qlcg_gl_uniform(tr->plant_prog, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
        glUniform2f(qlcg_gl_uniform(tr->plant_prog, "uFogRange"), fog_near, fog_far);
        glUniform1f(qlcg_gl_uniform(tr->plant_prog, "uLightScale"), sky.light_scale);
        glUniform3f(qlcg_gl_uniform(tr->plant_prog, "uTint"), sky.tint[0], sky.tint[1], sky.tint[2]);
        glUniform1f(qlcg_gl_uniform(tr->plant_prog, "uTime"), w->sim_time);
        glUniform1f(qlcg_gl_uniform(tr->plant_prog, "uEnable"), s->natural_physics ? 1.0f : 0.0f);
        for (int bi = 0; bi < tr->plant_batch_count; bi++) {
            PlantBatch *pb = &tr->plants[bi];
            if (pb->inst_count <= 0 || !pb->vao) continue;
            const LpGeometry *pg = qlcg_plant_geometry(pb->species, pb->form, pb->season);
            glUniform1f(qlcg_gl_uniform(tr->plant_prog, "uHeight"), pg ? (float)pg->height : 1.0f);
            glUniform1f(qlcg_gl_uniform(tr->plant_prog, "uShrub"), pb->category == PLANT_CAT_GUAN ? 1.0f : 0.0f);
            glUniform1f(qlcg_gl_uniform(tr->plant_prog, "uGrass"), pb->category == PLANT_CAT_CAO ? 1.0f : 0.0f);
            glBindVertexArray(pb->vao);
            glDrawElementsInstanced(GL_TRIANGLES, pb->idx_count, GL_UNSIGNED_INT, 0, pb->inst_count);
        }
    }

    /* 岩石：真实网格按原型分批 */
    if (tr->rock_batch_count > 0 && tr->rock_inst_total > 0) {
        glUseProgram(tr->plant_prog);
        glUniform1i(qlcg_gl_uniform(tr->plant_prog,"uUnderwater"),underwater?1:0);
        glUniformMatrix4fv(qlcg_gl_uniform(tr->plant_prog, "uMVP"), 1, GL_FALSE, relative_mvp);
        glUniform3f(qlcg_gl_uniform(tr->plant_prog, "uSunDir"), sx, sy, sz);
        glUniform3f(qlcg_gl_uniform(tr->plant_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
        glUniform3f(qlcg_gl_uniform(tr->plant_prog, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
        glUniform2f(qlcg_gl_uniform(tr->plant_prog, "uFogRange"), fog_near, fog_far);
        glUniform1f(qlcg_gl_uniform(tr->plant_prog, "uTime"), w->sim_time);
        glUniform1f(qlcg_gl_uniform(tr->plant_prog, "uEnable"), 0.0f);
        glUniform1f(qlcg_gl_uniform(tr->plant_prog, "uShrub"), 0.0f);
        glUniform1f(qlcg_gl_uniform(tr->plant_prog, "uGrass"), 0.0f);
        glUniform1f(qlcg_gl_uniform(tr->plant_prog, "uHeight"), 1.0f);
        glUniform3f(qlcg_gl_uniform(tr->plant_prog, "uTint"), sky.tint[0], sky.tint[1], sky.tint[2]);
        for (int bi = 0; bi < tr->rock_batch_count; bi++) {
            PlantBatch *rb = &tr->rocks[bi];
            if (rb->inst_count <= 0 || !rb->vao) continue;
            glBindVertexArray(rb->vao);
            glDrawElementsInstanced(GL_TRIANGLES, rb->idx_count, GL_UNSIGNED_INT, 0, rb->inst_count);
        }
    }
    /* 远景贴图：12–18 交叉面片，18 外朝向玩家 */
    if (tr->far_prog && qlcg_prop_sprites_can_far(&tr->sprites) &&
        (tr->far_cross_count > 0 || tr->far_bill_count > 0)) {
        float cr_x = cosf(tr->yaw), cr_z = sinf(tr->yaw);
        glUseProgram(tr->far_prog);
        glUniform1i(qlcg_gl_uniform(tr->far_prog, "uUnderwater"), underwater ? 1 : 0);
        glUniform1i(qlcg_gl_uniform(tr->far_prog, "uMsaa"), tr->msaa_active ? 1 : 0);
        glUniformMatrix4fv(qlcg_gl_uniform(tr->far_prog, "uMVP"), 1, GL_FALSE, relative_mvp);
        glUniform3f(qlcg_gl_uniform(tr->far_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
        glUniform3f(qlcg_gl_uniform(tr->far_prog, "uCamRight"), cr_x, 0.0f, cr_z);
        glUniform1f(qlcg_gl_uniform(tr->far_prog, "uNow"),
                    tr->fade_origin_ms > 0.0
                        ? (float)((qlcg_time_ms() - tr->fade_origin_ms) * 0.001)
                        : 0.0f);
        glUniform3f(qlcg_gl_uniform(tr->far_prog, "uSunDir"), sx, sy, sz);
        glUniform3f(qlcg_gl_uniform(tr->far_prog, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
        glUniform2f(qlcg_gl_uniform(tr->far_prog, "uFogRange"), fog_near, fog_far);
        glUniform3f(qlcg_gl_uniform(tr->far_prog, "uTint"), sky.tint[0], sky.tint[1], sky.tint[2]);
        glUniform1f(qlcg_gl_uniform(tr->far_prog, "uLightScale"), sky.light_scale);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tr->sprites.tex);
        glUniform1i(qlcg_gl_uniform(tr->far_prog, "uAtlas"), 0);
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        if (tr->far_cross_count > 0) {
            glUniform1i(qlcg_gl_uniform(tr->far_prog, "uMode"), 0);
            glBindVertexArray(tr->far_cross_vao);
            glDrawElementsInstanced(GL_TRIANGLES, tr->far_cross_idx, GL_UNSIGNED_SHORT, 0,
                                    tr->far_cross_count);
        }
        if (tr->far_bill_count > 0) {
            glUniform1i(qlcg_gl_uniform(tr->far_prog, "uMode"), 1);
            glBindVertexArray(tr->far_bill_vao);
            glDrawElementsInstanced(GL_TRIANGLES, tr->far_bill_idx, GL_UNSIGNED_SHORT, 0,
                                    tr->far_bill_count);
        }
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }
skip_props:

    /* 4. 绘制动态水体与冰壳 */
    if (!qlcg_dev_skip_draw(QLCG_DEV_SKIP_WATER)) {
    /* 4a. 冰顶：不透明，先画在开水之前，避免半透明叠色 */
    if (!qlcg_dev_skip_draw(QLCG_DEV_SKIP_ICE) && tr->ice_vis.ice_top_prog) {
        GLuint ip = tr->ice_vis.ice_top_prog;
        QLCG_DEV_ONLY(int dbg_draw = 0;)
        glUseProgram(ip);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDisable(GL_CULL_FACE);
        /* 冰顶与水共面：多边形偏移稳定压住水面，消除远处闪烁 */
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -8.0f);
        glUniform1i(qlcg_gl_uniform(ip,"uUnderwater"),underwater?1:0);
        glUniformMatrix4fv(qlcg_gl_uniform(ip, "uMVP"), 1, GL_FALSE, relative_mvp);
        glUniform3f(qlcg_gl_uniform(ip, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
        glUniform3f(qlcg_gl_uniform(ip, "uSunDir"), sx, sy, sz);
        glUniform3f(qlcg_gl_uniform(ip, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
        glUniform3f(qlcg_gl_uniform(ip, "uZenith"), sky_top[0], sky_top[1], sky_top[2]);
        glUniform2f(qlcg_gl_uniform(ip, "uFogRange"), fog_near, fog_far);
        glUniform1f(qlcg_gl_uniform(ip, "uLightScale"), sky.light_scale);
        glUniform3f(qlcg_gl_uniform(ip, "uTint"), sky.tint[0], sky.tint[1], sky.tint[2]);
        glUniform1f(qlcg_gl_uniform(ip, "uLift"), tr->ice_lift);
        GLint u_bottom = qlcg_gl_uniform(ip, "uBottom");
        GLint u_off = qlcg_gl_uniform(ip, "uChunkOffset");
        for (int pass = 0; pass < 2; pass++) {
            glUniform1i(u_bottom, pass);
            for (int i = 0; i < st->count; i++) {
                int slot = st->active[i];
                RegionData *c = st->slots[slot].data;
                TerrainChunkGPU *g = &tr->chunks[slot];
                if (g->ice_idx_count <= 0) continue;
                int ddx = c->center_cx-st->center_cx, ddz = c->center_cz-st->center_cz;
                if (ddx*ddx+ddz*ddz > (st->radius+0.5f)*(st->radius+0.5f)) continue;
                float cxw = (float)((double)c->center_x - tr->anchor_x);
                float czw = (float)((double)c->center_z - tr->anchor_z);
                float d2 = (cxw-cam_pos[0])*(cxw-cam_pos[0]) + (czw-cam_pos[2])*(czw-cam_pos[2]);
                if (pass == 1 && !underwater && d2 > 60.0f*60.0f) continue;
                /* 冰底只在相机低于该块水面时才可能看见 */
                if (pass == 1) {
                    int mid = (c->height / 2) * c->width + c->width / 2;
                    float wl_c = c->water_levels ? c->water_levels[mid] : -1e9f;
                    if (cam_pos[1] > wl_c + 0.05f) continue;
                }
                glUniform3f(u_off, cxw, 0.0f, czw);
                glBindVertexArray(g->water_vao);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g->ice_ebo);
                glDrawElements(GL_TRIANGLES, g->ice_idx_count, GL_UNSIGNED_INT, 0);
                QLCG_DEV_ONLY(dbg_draw++;)
            }
        }
        qlcg_dev_ice_pass(dbg_draw, underwater, tr->ice_lift);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(tr->water_prog);
    glUniform1i(qlcg_gl_uniform(tr->water_prog,"uUnderwater"),underwater?1:0);
    glUniform2f(qlcg_gl_uniform(tr->water_prog,"uWorldOrigin"),fmod(reg->center_x,4096.0),fmod(reg->center_z,4096.0));
    glUniformMatrix4fv(qlcg_gl_uniform(tr->water_prog, "uMVP"), 1, GL_FALSE, relative_mvp);
    glUniform3f(qlcg_gl_uniform(tr->water_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
    glUniform3f(qlcg_gl_uniform(tr->water_prog, "uSunDir"), sx, sy, sz);
    glUniform3f(qlcg_gl_uniform(tr->water_prog, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
    glUniform2f(qlcg_gl_uniform(tr->water_prog, "uFogRange"), fog_near, fog_far);
    glUniform1f(qlcg_gl_uniform(tr->water_prog, "uLightScale"), sky.light_scale);
    glUniform3f(qlcg_gl_uniform(tr->water_prog, "uZenith"), sky_top[0], sky_top[1], sky_top[2]);
    glUniform1f(qlcg_gl_uniform(tr->water_prog, "uTime"), w->sim_time);
    glUniform1f(qlcg_gl_uniform(tr->water_prog, "uWaveAmp"), s->water_waves ? 1.0f : 0.0f);
    SpringDraw falls[128];int fall_count=0;
    GLint water_offset = qlcg_gl_uniform(tr->water_prog, "uChunkOffset");
    GLint water_form = qlcg_gl_uniform(tr->water_prog, "uWaterForm");
    SpringDraw springs[16];
    int spring_count=0;
    glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1.0f,-8.0f);
    for (int i = 0; i < st->count; i++) {
        int slot = st->active[i];
        RegionData *c = st->slots[slot].data;
        if (tr->chunks[slot].water_idx_count <= 0) continue;
        int dx = c->center_cx-st->center_cx, dz = c->center_cz-st->center_cz;
        if (dx*dx+dz*dz > (st->radius+0.5f)*(st->radius+0.5f)) continue;
        float ox=(float)((double)c->center_x-tr->anchor_x);
        float oz=(float)((double)c->center_z-tr->anchor_z);
        glUniform3f(water_offset,ox,0,oz);
        glUniform1i(water_form,(int)c->water_form);
        glBindVertexArray(tr->chunks[slot].water_vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tr->chunks[slot].water_ebo);
        glDrawElements(GL_TRIANGLES, tr->chunks[slot].water_idx_count, GL_UNSIGNED_INT, 0);
        if(c->water_form!=WATER_FORM_CUTOFF && s->particles && !underwater) {
            for(int j=0;j<4 && fall_count<128;j++) {
                const float *e=tr->chunks[slot].fall_emitters[j];
                if(e[3]<=1) continue;
                int ix=qlcg_clampi((int)((e[0]+16)/2),0,15),iz=qlcg_clampi((int)((e[2]+16)/2),0,15);
                if(qlcg_cell_draws_ice(c,ix,iz)) continue;
                float distance=hypotf(hypotf(ox+e[0]-cam_pos[0],oz+e[2]-cam_pos[2]),e[1]-cam_pos[1]);
                if(distance<420) falls[fall_count++]=(SpringDraw){ox+e[0],e[1],oz+e[2],distance,
                    (qlcg_hash_u32((uint32_t)c->center_cx*73856093u^(uint32_t)c->center_cz*19349663u^j)&65535)/65536.0f};
            }
        }
        if(s->particles&&!underwater&&c->spring_count)
            spring_count+=collect_springs(c,ox,oz,cam_pos,springs+spring_count,16-spring_count);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    if(s->particles && !underwater && tr->spring_prog) {
        GLuint program=tr->spring_prog;
        glEnable(GL_PROGRAM_POINT_SIZE);glDepthMask(GL_FALSE);glUseProgram(program);
        glUniformMatrix4fv(qlcg_gl_uniform(program,"uMVP"),1,GL_FALSE,mvp);
        glUniform1f(qlcg_gl_uniform(program,"uTime"),w->sim_time);
        glUniform1f(qlcg_gl_uniform(program,"uLight"),sky.light_scale);
        glUniform1f(qlcg_gl_uniform(program,"uFallSpray"),1);
        glUniform1f(qlcg_gl_uniform(program,"uPixels"),win_h);
        glBindVertexArray(tr->weather_vao);
        for(int i=0;i<fall_count;i++) {
            SpringDraw a=falls[i];
            glUniform3f(qlcg_gl_uniform(program,"uOrigin"),a.x,a.y+.12f,a.z);
            glUniform1f(qlcg_gl_uniform(program,"uSeed"),a.seed);
            glUniform1f(qlcg_gl_uniform(program,"uDistance"),a.distance);
            glDrawArrays(GL_POINTS,0,96);
        }
        glUniform1f(qlcg_gl_uniform(program,"uFallSpray"),0);
        glDepthMask(GL_TRUE);glDisable(GL_PROGRAM_POINT_SIZE);
    }
    if(spring_count&&tr->spring_prog) {
        GLuint program=tr->spring_prog;
        glEnable(GL_PROGRAM_POINT_SIZE);
        glDepthMask(GL_FALSE);
        glUseProgram(program);
        glUniformMatrix4fv(qlcg_gl_uniform(program,"uMVP"),1,GL_FALSE,mvp);
        glUniform1f(qlcg_gl_uniform(program,"uTime"),w->sim_time);
        glUniform1f(qlcg_gl_uniform(program,"uLight"),sky.light_scale);
        glBindVertexArray(tr->weather_vao);
        for(int i=0;i<spring_count;i++) {
            SpringDraw a=springs[i];
            glUniform3f(qlcg_gl_uniform(program,"uOrigin"),a.x,a.y,a.z);
            glUniform1f(qlcg_gl_uniform(program,"uSeed"),a.seed);
            glUniform1f(qlcg_gl_uniform(program,"uDistance"),a.distance);
            glDrawArrays(GL_POINTS,0,24);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_PROGRAM_POINT_SIZE);
    }

    /* 4b. 浮冰、破冰碎片与裂纹 */
    if (!qlcg_dev_skip_draw(QLCG_DEV_SKIP_ICE)) {
        static const float tint_default[3] = {1.0f, 1.0f, 1.0f};
        const float *tnt = sky.tint;
        if (!tnt) tnt = tint_default;
        qlcg_ice_visual_draw_flow(tr, mvp, cam_pos, sky_bot, fog_near, fog_far, sky.light_scale,
                                  (const float *)&sky.sun_dir[0], tnt, w->sim_time);
        qlcg_ice_visual_draw_cracks(tr, mvp, cam_pos, sky_bot, fog_near, fog_far, sky.light_scale);
    }
    }

    /* 5. 绘制雨/雪/冰雹/沙尘天气粒子 */
    float p_rain = qlcg_maxf(wp->rain, tr->rain_override);
    float p_snow = wp->snow;
    float p_hail = wp->hail;
    float p_dust = wp->dust;
    float p_any = qlcg_maxf(qlcg_maxf(p_rain, p_snow), qlcg_maxf(p_hail, p_dust));
    if (!underwater && s->particles && p_any > 0.02f) {
        int limit = s->weather_atmosphere ? 1200 : 240;
        static const float RAIN_COL[3] = { 0.76f, 0.84f, 0.94f };
        static const float SNOW_COL[3] = { 0.96f, 0.98f, 1.00f };
        static const float HAIL_COL[3] = { 1.00f, 1.00f, 1.00f };
        static const float DUST_COL[3] = { 0.75f, 0.76f, 0.70f };
        static const float SAND_COL[3] = { 0.80f, 0.69f, 0.48f };
        glEnable(GL_PROGRAM_POINT_SIZE);
        glUseProgram(tr->weather_prog);
        glUniformMatrix4fv(qlcg_gl_uniform(tr->weather_prog, "uMVP"), 1, GL_FALSE, mvp);
        glUniform3f(qlcg_gl_uniform(tr->weather_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
        glUniform1f(qlcg_gl_uniform(tr->weather_prog, "uTime"), w->sim_time);
        glUniform1f(qlcg_gl_uniform(tr->weather_prog, "uWind"), wp->wind);
        glUniform1f(qlcg_gl_uniform(tr->weather_prog, "uVortex"), wp->vortex ? 1.0f : 0.0f);
        glBindVertexArray(tr->weather_vao);
        struct { float rate, fall, size, alpha, streak; const float *col; } passes[4] = {
            { p_rain, 16.0f, 4.0f, 0.60f, 1.0f, RAIN_COL },
            { p_snow,  3.5f, 4.2f, 0.80f, 0.0f, SNOW_COL },
            { p_hail, 10.0f, 4.6f, 0.90f, 0.0f, HAIL_COL },
            { p_dust,  2.0f, 2.4f, 0.50f, 0.0f, (wp->id == 304) ? SAND_COL : DUST_COL },
        };
        for (int i = 0; i < 4; i++) {
            int cnt = qlcg_clampi((int)(passes[i].rate * (float)limit), 0, limit);
            if (cnt <= 0) continue;
            glUniform1f(qlcg_gl_uniform(tr->weather_prog, "uFall"), passes[i].fall);
            glUniform1f(qlcg_gl_uniform(tr->weather_prog, "uSize"), passes[i].size);
            glUniform1f(qlcg_gl_uniform(tr->weather_prog, "uAlpha"), passes[i].alpha);
            glUniform1f(qlcg_gl_uniform(tr->weather_prog, "uStreak"), passes[i].streak);
            glUniform3f(qlcg_gl_uniform(tr->weather_prog, "uPartCol"),
                        passes[i].col[0], passes[i].col[1], passes[i].col[2]);
            glDrawArrays(GL_POINTS, 0, cnt);
        }
    }
}
