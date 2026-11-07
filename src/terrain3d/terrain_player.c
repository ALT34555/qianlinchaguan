
#include "terrain_view.h"
#include "core/dev_tools.h"
#include "terrain_types.h"
#include "../core/math_util.h"
#include "../systems/world/ice_runtime.h"
#include "../systems/world/surface_internal.h"
#include "../core/measurements.h"
#include "../core/i18n.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>


#define GRAVITY            32.0f
#define JUMP_VELOCITY       9.0f
#define TERMINAL_VELOCITY  78.0f
#define WALK_SPEED          4.3f
#define SPRINT_SPEED        5.6f
#define SWIM_SPEED          2.6f
#define FLY_SPEED          12.0f
#define FLY_SPRINT_SPEED   36.0f
#define FLY_VERTICAL_SPEED 10.0f
#define DOUBLE_TAP_MS     300.0
#define HALF_WIDTH          0.3f
#define EYE_RATIO           0.94f

/* 玩家脚底相对地表的余量，避免陷进地面 */
#define GROUND_EPS       0.01f

/* 冰载荷档位：站/走/跑/起跳/落地 */
#define ICE_LOAD_STAND    1.00f
#define ICE_LOAD_WALK     1.15f
#define ICE_LOAD_RUN      1.35f
#define ICE_LOAD_JUMP     2.20f

static float body_height_of(const GameSettings *s) {
    float h = (float)(s ? s->profile.height_cm : 170) / 100.0f;
    return h > 0.1f ? h : 1.7f;
}


static float speed_scale_of(const GameSettings *s) {
    float h = (float)(s ? s->profile.height_cm : 170);
    return h > 1.0f ? h / 170.0f : 1.0f;
}

/* 眼高偏移：脚底到眼睛 + 台阶平滑 - 蹲下 */
static float eye_offset_of(const TerrainRenderer *tr, float body_h) {
    return body_h * EYE_RATIO + tr->step_offset - tr->crouch_offset;
}

void qlcg_terrain_place_player_at_spawn(TerrainRenderer *tr, const RegionData *reg, const GameSettings *s) {
    /* 局部原点即区块中心，出生区块已选定 */
    tr->x  = 0.0f;
    tr->z  = 0.0f;
    tr->vx = tr->vy = tr->vz = 0.0f;
    tr->on_ground      = true;
    tr->blocked_forward = false;
    tr->plant_contacts = 0;
    memset(&tr->block_feedback,0,sizeof(tr->block_feedback));
    tr->crouch_offset  = 0.0f;
    tr->step_offset    = 0.0f;
    tr->last_jump_tap_ms = -1.0e30;
    float gh  = qlcg_region_height_at(reg, 0.0f, 0.0f);
    float wl  = qlcg_region_water_at(reg, 0.0f, 0.0f);
    float eye = body_height_of(s) * EYE_RATIO;
    tr->y  = qlcg_maxf(gh, wl) + eye + GROUND_EPS;
    /* 落地时略向下看，先收进脚下的地形 */
    tr->pitch = -0.34f;
    tr->yaw   = 0.0f;
}


static void handle_double_tap(TerrainRenderer *tr, const PlayerInput *in, bool flight_allowed) {
    if (!in || !in->jump_pressed) return;
    if (!flight_allowed) return;
    double now = qlcg_time_ms();
    if (now - tr->last_jump_tap_ms <= DOUBLE_TAP_MS) {
        tr->flying = !tr->flying;
        tr->vy     = 0.0f;
        tr->last_jump_tap_ms = -1.0e30;
    } else {
        tr->last_jump_tap_ms = now;
    }
}

static float ground_under(const RegionData *reg, float x, float z, float feet) {
    return fmaxf(qlcg_region_height_at(reg,x,z),qlcg_region_ice_ground(reg,x,z,HALF_WIDTH,feet));
}

/* 破冰当帧：余下分步内冰不再提供支撑 */
static float ground_under_no_ice(const RegionData *reg, float x, float z, float feet) {
    (void)feet;
    return qlcg_region_height_at(reg, x, z);
}

/* 脚底确实踩在冰上：冰顶高于地面且与脚底贴合 */
static bool standing_on_ice(const RegionData *reg, float x, float z, float feet, float *out_top) {
    float top = qlcg_region_ice_ground(reg, x, z, HALF_WIDTH, feet);
    if (!isfinite(top)) return false;
    float terr = qlcg_region_height_at(reg, x, z);
    if (!(top > terr + 0.02f)) return false;
    if (fabsf(feet - top) > 0.30f) return false;
    if (out_top) *out_top = top;
    return true;
}

static void block_horizontal(TerrainRenderer *tr, const PlayerInput *in, float dx, float dz) {
    if (dx!=0) tr->vx=0;
    if (dz!=0) tr->vz=0;
    if (in->forward && !in->back && sinf(tr->yaw)*dx-cosf(tr->yaw)*dz>1e-6f)
        tr->blocked_forward=true;
}

static void move_horizontal(TerrainRenderer *tr, const RegionData *reg, const PlayerInput *in,
                            float *feet, float dx, float dz, float body_h, float step_h,
                            bool was_grounded, bool ice_off) {
    if (dx==0 && dz==0) return;
    float nx=tr->x+dx, nz=tr->z+dz;
    float terrain=qlcg_region_height_at(reg,nx,nz);
    float ground=ice_off ? ground_under_no_ice(reg,nx,nz,*feet) : ground_under(reg,nx,nz,*feet);
    bool supported=(was_grounded || tr->on_ground) && !ice_off;
    bool stepping=supported && !tr->flying && tr->vy<=0;
    if (stepping) ground=fmaxf(ground,qlcg_region_step_surface(reg,nx,nz,*feet,step_h,HALF_WIDTH));
    bool blocked=!isfinite(terrain);
    blocked=blocked || (qlcg_region_blocked(reg,nx,fmaxf(*feet,ground),nz,body_h,HALF_WIDTH)
        && !qlcg_region_blocked(reg,tr->x,*feet,tr->z,body_h,HALF_WIDTH));
    float old_terrain=qlcg_region_height_at(reg,tr->x,tr->z);
    if (!tr->flying && terrain>*feet-GROUND_EPS && terrain>old_terrain+1e-6f) {
        float limit=qlcg_movement_rules()->max_slope_degrees;
        float slope=fmaxf(qlcg_region_slope_at(reg,tr->x,tr->z),qlcg_region_slope_at(reg,nx,nz));
        if (limit<90 && slope>tanf(limit*QLCG_PI/180)+1e-5f) blocked=true;
    }
    float rise=ground-*feet;
    if (rise>0 && (!(stepping || tr->in_water || tr->flying) || rise>step_h)) blocked=true;
    if (blocked) { block_horizontal(tr,in,dx,dz); return; }
    tr->x=nx; tr->z=nz;
    if (rise>0) {
        *feet=ground; tr->step_offset-=rise;
        tr->on_ground=true;
    } else if (stepping && -rise<=step_h) {
        *feet=ground; tr->on_ground=true;
    }
}

/* 玩家所在位置的地表温度 */
static float region_temp_at(const RegionData *reg, float lx, float lz) {
    if (!reg || !reg->temperatures || reg->width <= 0 || reg->height <= 0) return 15.0f;
    float half = (float)(reg->width - 1) * reg->step * 0.5f;
    int gx = qlcg_clampi((int)lroundf((lx + half) / reg->step), 0, reg->width - 1);
    int gz = qlcg_clampi((int)lroundf((lz + half) / reg->step), 0, reg->height - 1);
    return reg->temperatures[gz * reg->width + gx];
}

void qlcg_terrain_update_player(TerrainRenderer *tr, RegionData *reg, const MacroWorld *w,
                                const GameSettings *s, const CalendarClock *clk,
                                const PlayerInput *in, float dt) {
    tr->blocked_forward=false;
    if (!reg || !in) return;
    if (!qlcg_region_chunk_at(reg,tr->x,tr->z,NULL,NULL))
        qlcg_terrain_stream_update(tr,reg,s);
    if (dt > 0.1f) dt = 0.1f;               /* 卡顿时别让玩家瞬移 */
    /* 脚下冰先刷新，身体占领的单元不允许封口 */
    if (w && w->ice_time_valid) {
        qlcg_ice_tick(reg, tr->x, tr->z, 2);
        qlcg_ice_set_occupant(w, reg->center_x + tr->x, reg->center_z + tr->z, HALF_WIDTH);
    }

    const float body_h    = body_height_of(s);
    const float speed_sc  = speed_scale_of(s);
    const float eye_off0  = eye_offset_of(tr, body_h);
    float feet = tr->y - eye_off0;          /* 对外仍是眼高，内部按脚底算 */
    QLCG_DEV_ONLY(bool ice_broke_frame = false;)           /* 本帧是否发生过破冰 */
    float resolved=qlcg_region_ice_resolve(reg,tr->x,feet,tr->z,body_h,HALF_WIDTH);
    if (isfinite(resolved)) { feet=resolved; tr->vy=0; }

    bool flight_allowed = (w && w->opt.play_mode == PLAY_MODE_LEISURE);
    handle_double_tap(tr, in, flight_allowed);
    if (!flight_allowed) tr->flying = false;

    /* ---- 蹲下：眼高插值到 65% */
    bool crouching = !tr->flying && in->crouch;
    float crouch_target = crouching ? body_h * 0.35f : 0.0f;
    tr->crouch_offset += (crouch_target - tr->crouch_offset) * qlcg_minf(1.0f, dt * 16.0f);

    /* ---- 水平意图 */
    float fwd_x = sinf(tr->yaw), fwd_z = -cosf(tr->yaw);
    float rgt_x = cosf(tr->yaw), rgt_z =  sinf(tr->yaw);
    float wish_x = 0.0f, wish_z = 0.0f;
    if (in->forward) { wish_x += fwd_x; wish_z += fwd_z; }
    if (in->back)    { wish_x -= fwd_x; wish_z -= fwd_z; }
    if (in->right)   { wish_x += rgt_x; wish_z += rgt_z; }
    if (in->left)    { wish_x -= rgt_x; wish_z -= rgt_z; }
    float len = hypotf(wish_x, wish_z);
    if (len > 0.01f) { wish_x /= len; wish_z /= len; }


    float gh0 = qlcg_region_height_at(reg, tr->x, tr->z);
    float wl0 = qlcg_region_water_at(reg, tr->x, tr->z);
    float probe_y = feet + 0.4f;
    tr->in_water = (probe_y < wl0) && (gh0 <= probe_y);

    /* ---- 目标速度 */
    float speed;
    if (tr->flying) {
        speed = in->sprint ? FLY_SPRINT_SPEED : FLY_SPEED;
    } else if (tr->in_water) {
        speed = SWIM_SPEED;
    } else if (in->sprint) {
        speed = SPRINT_SPEED;
    } else {
        speed = WALK_SPEED;
    }
    if (crouching && !tr->flying && !tr->in_water) speed = WALK_SPEED * 0.5f;
    speed *= speed_sc;
    float brush = tr->flying ? 1.0f
                             : qlcg_region_brush_speed(reg,tr->x,feet,tr->z,body_h,HALF_WIDTH);
    speed *= brush;

    /* ---- 加速度插值 */
    float accel = (tr->flying || tr->on_ground || tr->in_water) ? 14.0f : 4.0f;
    float k = qlcg_minf(1.0f, accel * dt);
    tr->vx += (wish_x * speed - tr->vx) * k;
    tr->vz += (wish_z * speed - tr->vz) * k;

    /* ---- 竖直 */
    if (tr->flying) {
        float dir = (in->jump ? 1.0f : 0.0f) - (in->crouch ? 1.0f : 0.0f);
        float target = dir * FLY_VERTICAL_SPEED * (in->sprint ? 2.0f : 1.0f) * speed_sc;
        tr->vy += (target - tr->vy) * qlcg_minf(1.0f, 12.0f * dt);
    } else if (tr->in_water) {
        tr->vy -= GRAVITY * 0.25f * dt;
        tr->vy *= qlcg_maxf(0.0f, 1.0f - 3.0f * dt);
        if (in->jump) tr->vy = qlcg_minf(4.0f, tr->vy + 24.0f * dt);
        if (tr->vy < -4.0f) tr->vy = -4.0f;
    } else {
        tr->vy -= GRAVITY * dt;
        if (tr->vy < -TERMINAL_VELOCITY) tr->vy = -TERMINAL_VELOCITY;
        if (in->jump && tr->on_ground) {
            float ice_top = 0.0f;
            bool launched = true;
            if (w && standing_on_ice(reg, tr->x, tr->z, feet, &ice_top)) {
                int r = qlcg_region_ice_apply_load(w, reg, tr->x, tr->z, HALF_WIDTH, feet,
                                                   ICE_LOAD_JUMP, 0.0f, true);
                if (r == 2) {
                    launched = false;      /* 蹬地击穿：取消冲量，按原重力落水 */
                    QLCG_DEV_ONLY(ice_broke_frame = true;)
                    tr->on_ground = false;
                    qlcg_ice_visual_spawn_fragments(tr, reg->center_x + tr->x, ice_top,
                                                    reg->center_z + tr->z, 5);
                }
            }
            if (launched) tr->vy = JUMP_VELOCITY;
        }
    }


    float step_h = qlcg_maxf(0.1f, speed_sc);            /* 自动上台阶高度 */
    float cstep  = qlcg_minf(0.45f, body_h * 0.25f);
    if (cstep < 0.05f) cstep = 0.05f;

    tr->motion.speed   = speed;
    tr->motion.accel   = accel;
    tr->motion.brush   = brush;
    tr->motion.step_h  = step_h;
    tr->motion.eye_off = eye_offset_of(tr, body_h);

    float dx = tr->vx * dt, dy = tr->vy * dt, dz = tr->vz * dt;
    float dist = qlcg_maxf(fabsf(dx), qlcg_maxf(fabsf(dy), fabsf(dz)));
    int steps = (int)ceilf(dist / cstep);
    if (steps < 1) steps = 1;
    if (steps > 512) steps = 512;
    float sx = dx / steps, sy = dy / steps, sz = dz / steps;

    bool was_grounded = tr->on_ground;
    tr->on_ground = false;
    float eye_off = eye_offset_of(tr, body_h);

    bool ice_off = false;       /* 本帧已破冰：余下分步不许再上冰 */
    for (int i = 0; i < steps; i++) {
        /* 竖直 */
        float ny = feet + sy;
        float ceiling=qlcg_region_ice_ceiling(reg,tr->x,tr->z,HALF_WIDTH,feet+body_h);
        if (sy>0 && ny+body_h>ceiling) { ny=ceiling-body_h; tr->vy=0; }
        float ground = ice_off ? ground_under_no_ice(reg,tr->x,tr->z,feet)
                               : ground_under(reg,tr->x,tr->z,feet);
        if (sy<=0 && !ice_off)
            ground=fmaxf(ground,qlcg_region_landing(reg,tr->x,tr->z,feet,ny,HALF_WIDTH));
        /* 向下跨过冰顶：先用归零前速度施加落地载荷 */
        if (sy < 0.0f && w && tr->vy < -3.0f) {
            float ice_top = 0.0f;
            float terr = qlcg_region_height_at(reg,tr->x,tr->z);
            float ice_g = qlcg_region_ice_ground(reg,tr->x,tr->z,HALF_WIDTH,feet);
            if (isfinite(ice_g) && ice_g > terr + 0.02f && ice_g >= ground - 1e-4f &&
                ny <= ice_g && feet > ice_g - 1e-4f) {
                float load = qlcg_clampf(2.0f + .8f * fabsf(tr->vy) / 9.0f, 2.0f, 5.0f);
                if (qlcg_region_ice_apply_load(w, reg, tr->x, tr->z, HALF_WIDTH, feet,
                                               load, 0.0f, true) == 2) {
                    QLCG_DEV_ONLY(ice_broke_frame = true;)
                    ice_off = true;
                    was_grounded = false;
                    qlcg_ice_visual_spawn_fragments(tr, reg->center_x + tr->x, ice_g,
                                                    reg->center_z + tr->z, 6);
                    qlcg_ice_visual_spawn_rim(tr, reg->center_x + tr->x, ice_g,
                                              reg->center_z + tr->z, 1.0f, 10);
                    ground = ground_under_no_ice(reg,tr->x,tr->z,feet);
                    tr->on_ground = false;
                }
                (void)ice_top;
            }
        }
        if (sy>0 && qlcg_region_blocked(reg,tr->x,ny,tr->z,body_h,HALF_WIDTH)
            && !qlcg_region_blocked(reg,tr->x,feet,tr->z,body_h,HALF_WIDTH)) {
            tr->vy=0;
        } else if (sy <= 0.0f && ny < ground) {
            float rise = ground - feet;
            feet = ground;
            tr->vy = 0.0f;
            tr->on_ground = true;
            if (tr->flying) tr->flying = false;   /* 落地自动退出飞行 */
            (void)rise;
        } else {
            feet = ny;
        }

        move_horizontal(tr,reg,in,&feet,sx,0,body_h,step_h,was_grounded,ice_off);
        move_horizontal(tr,reg,in,&feet,0,sz,body_h,step_h,was_grounded,ice_off);
    }

    /* 飞行时不要穿到地面以下 */
    {
        float g = ice_off ? ground_under_no_ice(reg,tr->x,tr->z,feet)
                          : ground_under(reg,tr->x,tr->z,feet);
        if (feet < g) { feet = g; if (tr->vy < 0.0f) tr->vy = 0.0f; }
    }

    /* 世界竖直边界夹取（对齐 base Player） */
    if (feet < (float)QLCG_WORLD_MIN_Y) {
        feet = (float)QLCG_WORLD_MIN_Y;
        tr->vy = 0.0f;
    }
    if (feet > (float)QLCG_WORLD_MAX_Y - body_h) {
        feet = (float)QLCG_WORLD_MAX_Y - body_h;
        if (tr->vy > 0.0f) tr->vy = 0.0f;
    }


    /* 站走疲劳：每帧只按实际接触计一次 */
    if (w && tr->on_ground && !tr->in_water && !tr->flying) {
        float ice_top = 0.0f;
        if (standing_on_ice(reg, tr->x, tr->z, feet, &ice_top)) {
            float hspeed = hypotf(tr->vx, tr->vz);
            float load = (hspeed > 3.2f) ? ICE_LOAD_RUN
                       : ((hspeed > 0.3f) ? ICE_LOAD_WALK : ICE_LOAD_STAND);
            if (qlcg_region_ice_apply_load(w, reg, tr->x, tr->z, HALF_WIDTH, feet,
                                           load, dt, false) == 2) {
                tr->on_ground = false;
                was_grounded = false;
                QLCG_DEV_ONLY(ice_broke_frame = true;)
                qlcg_ice_visual_spawn_fragments(tr, reg->center_x + tr->x, ice_top,
                                                reg->center_z + tr->z, 6);
                qlcg_ice_visual_spawn_rim(tr, reg->center_x + tr->x, ice_top,
                                          reg->center_z + tr->z, 1.0f, 10);
            }
        }
    }

    qlcg_dev_ice_walk(tr, reg, feet, was_grounded, ice_broke_frame);
/* 分步末按新脚底重算水状态，阻尼下一步生效 */
    {
        float gh = qlcg_region_height_at(reg, tr->x, tr->z);
        float wl = qlcg_region_water_at(reg, tr->x, tr->z);
        tr->in_water = (feet + 0.4f < wl) && (gh <= feet + 0.4f);
    }

    /* 裂纹反馈：本帧接触单元的疲劳 */
    qlcg_ice_visual_cracks_clear(tr);
    if (w && reg->ice) {
        IceContact cs[16];
        int cn = qlcg_region_ice_contacts(reg, tr->x, tr->z, HALF_WIDTH, feet, cs, 16);
        for (int i = 0; i < cn; i++) {
            float dmg = qlcg_ice_cell_damage_at(w, reg, cs[i].wx, cs[i].wz);
            if (dmg > 0.04f)
                qlcg_ice_visual_crack_add(tr, cs[i].wx, cs[i].top, cs[i].wz, 0.85f, dmg);
        }
    }
    if (w) qlcg_ice_visual_update(tr, reg, w, dt);

    tr->step_offset *= expf(-14.0f * dt);
    if (fabsf(tr->step_offset) < 1e-3f) tr->step_offset = 0.0f;

    tr->plant_contacts=qlcg_region_plant_contacts(reg,tr->x,feet,tr->z,body_h,HALF_WIDTH);

    /* 写回眼高坐标 */
    eye_off = eye_offset_of(tr, body_h);
    tr->y = feet + eye_off;

    qlcg_terrain_stream_update(tr, reg, s);

    /* ---- 天气与环境反馈（保持原逻辑） */
    int cx = reg->center_cx;
    int cz = reg->center_cz;
    ChunkInspectInfo ci;
    qlcg_world_inspect_chunk(w, cx, cz, &ci);
    if (clk) tr->weather_unix_ms = clk->unix_ms;
    float wtemp = region_temp_at(reg, tr->x, tr->z);
    ChunkWeather cw = qlcg_sample_chunk_weather(&tr->weather_sys, clk ? clk->unix_ms : 0.0,
                                                cx, cz, wtemp, w->equator_chunks);
    tr->cur_weather = cw.profile ? cw.profile : qlcg_get_weather_profile(0);
    tr->precip_intensity = (tr->weather_sys.forced_id >= 0)
        ? qlcg_weather_precipitation(cw.weather_id)
        : qlcg_clampf(cw.intensity, 0.0f, 1.0f);
    qlcg_dev_weather(&tr->cur_weather, &tr->precip_intensity);

    if (s->status_feedback) {
        if (tr->in_water) snprintf(tr->status_text, sizeof(tr->status_text), qlcg_t("status.wading"));
        else if (ci.temperature < -6.0f) snprintf(tr->status_text, sizeof(tr->status_text), qlcg_t("status.cold"), ci.temperature);
        else if (ci.temperature > 34.0f) snprintf(tr->status_text, sizeof(tr->status_text), qlcg_t("status.hot"), ci.temperature);
        else snprintf(tr->status_text, sizeof(tr->status_text), qlcg_t("status.fine"));
    } else {
        tr->status_text[0] = '\0';
    }

    qlcg_terrain_update_aim_probe(tr, reg, w, s, in->inspect_plants);
}
