/* F12 调试面板：5×5小地图与区块属性 */
#include "app/app_state.h"
#include "systems/world/faction_naming.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define DM_GRID 5
#define DM_HALF (DM_GRID / 2)

/* 八向箭头，索引同 FLOW_DIR_NAMES */
static const char *k_flow_arrow[8] = {"→", "↗", "↑", "↖", "←", "↙", "↓", "↘"};
static const char *k_flow_name[8]  = {"东", "东北", "北", "西北", "西", "西南", "南", "东南"};

static const char *flow_arrow_of(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < 8; i++)
        if (strcmp(name, k_flow_name[i]) == 0) return k_flow_arrow[i];
    return NULL;
}

/* 深底反白，浅底用墨色 */
static uint32_t label_ink(uint32_t fill) {
    int r = (int)((fill >> 24) & 0xFFu);
    int g = (int)((fill >> 16) & 0xFFu);
    int b = (int)((fill >> 8) & 0xFFu);
    int lum = (r * 299 + g * 587 + b * 114) / 1000;
    return lum < 120 ? ui_rgba(248, 244, 232, 235) : ui_rgba(24, 18, 12, 210);
}

/* 玩家当前运动状态 */
static const char *player_state_text(void) {
    if (g_app.terrain.flying)  return tr("debug.mode.flying");
    if (g_app.terrain.in_water) return tr("debug.mode.water");
    if (g_app.terrain.on_ground) return tr("debug.mode.ground");
    return tr("debug.mode.air");
}

static UiRect debug_panel_rect(UiContext *ui, float *map_size) {
    const float z = ui->met.size, sc = ui->met.scale;
    const float pad = 12.0f * z, pw = 340.0f * z, gap = 6.0f * z;
    const float small_h = 11.0f * sc * 1.55f;
    const float title_h = 15.0f * sc * 1.75f, pill_h = 22.0f * z;
    float map_w = pw - pad * 2.0f;
    float map_h = map_w;

    /* 绘制与输入共用面板边界 */
    float ph = pad + title_h + small_h + gap + map_h + gap * 0.8f
             + small_h * 1.3f
             + small_h * 6.0f
             + gap * 1.6f + title_h + small_h * 2.2f
             + gap * 1.6f + small_h * 3.2f
             + gap * 0.8f + pill_h * 2.0f + gap * 0.6f
             + pad;

    /* 高倍率优先压缩小地图 */
    {
        float avail = (float)ui->win_h - 16.0f * z;
        if (ph > avail) {
            float nm = map_w - (ph - avail);
            float minm = 90.0f * z;
            if (nm < minm) nm = minm;
            if (nm < map_w) {
                ph -= (map_w - nm);
                map_w = nm;
                map_h = nm;
            }
            if (ph > avail) ph = avail;   /* 地图已到下限仍溢出时硬截断 */
        }
    }

    float px = (float)ui->win_w - pw - 14.0f * z;
    float py = 48.0f * z;
    if (py + ph > (float)ui->win_h - 8.0f * z)
        py = fmaxf(8.0f * z, (float)ui->win_h - ph - 8.0f * z);

    if (map_size) *map_size = map_w;
    return (UiRect){px, py, pw, ph};
}

bool game_debug_over_ui(UiContext *ui) {
    if (!g_app.show_debug_f12 || !g_app.world) return false;
    UiRect r = debug_panel_rect(ui, NULL);
    return ui_pointer_in_rect(ui, r.x, r.y, r.w, r.h);
}

void render_debug_overlay_f12(UiContext *ui) {
    if (!g_app.show_debug_f12 || !g_app.world) return;

    const float z   = ui->met.size;
    const float sc  = ui->met.scale;
    const float pad = 12.0f * z;
    const float pw  = 340.0f * z;
    const float gap = 6.0f * z;
    const float small_h = 11.0f * sc * 1.55f;
    const float title_h = 15.0f * sc * 1.75f;
    const float pill_h  = 22.0f * z;

    float map_w;
    UiRect panel = debug_panel_rect(ui, &map_w);
    float map_h = map_w;
    float px = panel.x, py = panel.y, ph = panel.h;

    ui_draw_rect_glass(ui, px, py, pw, ph, ui->pal.bg_panel_glass);
    ui_draw_border(ui, px, py, pw, ph, 1.5f * z, ui->pal.border_gold);

    char buf[192];
    float y = py + pad;

    /* ---- 标题 */
    ui_draw_text(ui, FONT_BOLD, px + pad, y, tr("debug.title"), ui->pal.text_main);
    ui_draw_text_right(ui, FONT_SMALL, px + pw - pad, y + 2.0f * z, "F12", ui->pal.text_sub);
    y += title_h;

    /* ---- 性能行（纯数值，不进 i18n） */
    snprintf(buf, sizeof(buf), "%.0f FPS · %.0fms · %.2fms · %.1fms",
             g_app.fps_smooth, g_app.world->gen_ms,
             g_app.atlas.last_render_ms, g_app.airdrop_ms);
    ui_draw_text(ui, FONT_MONO, px + pad, y, buf, ui->pal.accent_tea);
    y += small_h + gap;

    /* ---- 5×5 区块小地图 */
    {
        float cell = map_w / (float)DM_GRID;
        float mx0 = px + pad, my0 = y;
        const ChunkTypeDef *legend[8];
        int nlegend = 0;

        for (int dz = -DM_HALF; dz <= DM_HALF; dz++) {
            for (int dx = -DM_HALF; dx <= DM_HALF; dx++) {
                ChunkInspectInfo ci;
                qlcg_world_inspect_chunk(g_app.world, g_app.player_cx + dx,
                                         g_app.player_cz + dz, &ci);
                float qx = mx0 + (float)(dx + DM_HALF) * cell;
                float qy = my0 + (float)(dz + DM_HALF) * cell;
                uint32_t fill = qlcg_chunk_display_color(ci.type_def);
                uint32_t ink = label_ink(fill);
                ui_draw_rect(ui, qx, qy, cell - 1.0f * z, cell - 1.0f * z,
                             ui_rgba_hex(fill, 240));
                if (ci.type_def->code && ci.type_def->code[0])
                    ui_draw_text_px_center(ui, UI_FACE_MONO, cell * 0.30f, false,
                                           qx + cell * 0.5f, qy + 2.0f * z,
                                           ci.type_def->code, ink);
                if (ci.river_width > 0.0f) {
                    const char *arrow = flow_arrow_of(ci.flow_dir_name);
                    if (arrow)
                        ui_draw_text_px_center(ui, UI_FACE_BODY, cell * 0.48f, true,
                                               qx + cell * 0.5f, qy + cell * 0.45f,
                                               arrow, ink);
                }
                /* 收集图例，最多 8 种 */
                bool dup = false;
                for (int k = 0; k < nlegend; k++)
                    if (legend[k] == ci.type_def) dup = true;
                if (!dup && nlegend < 8) legend[nlegend++] = ci.type_def;
            }
        }

        /* 当前区块高亮 */
        ui_draw_border(ui, mx0 + (float)DM_HALF * cell, my0 + (float)DM_HALF * cell,
                       cell, cell, 2.0f * z, ui_rgba(255, 255, 255, 235));

        /* 北向 */
        ui_draw_text_right(ui, FONT_SMALL, mx0 + map_w - 2.0f * z, my0 + 1.0f * z,
                           tr("debug.north"), ui->pal.text_main);

        /* 玩家位置与朝向：局部坐标先换算成区块内比例 */
        {
            float lx = g_app.terrain.x / (float)CHUNK_SIZE;
            float lz = g_app.terrain.z / (float)CHUNK_SIZE;
            float ppx = mx0 + (lx + (float)DM_HALF + 0.5f) * cell;
            float ppy = my0 + (lz + (float)DM_HALF + 0.5f) * cell;
            float dx = sinf(g_app.terrain.yaw), dz = -cosf(g_app.terrain.yaw);
            ui_draw_circle(ui, ppx, ppy, 3.5f * z, ui_rgba(230, 57, 70, 255));
            ui_draw_line(ui, ppx, ppy, ppx + dx * cell * 0.42f, ppy + dz * cell * 0.42f,
                         2.0f * z, ui_rgba(230, 57, 70, 255));
        }

        y = my0 + map_h + gap * 0.8f;

        /* ---- 图例 */
        {
            float lx = px + pad;
            float lw = pw - pad * 2.0f;
            for (int k = 0; k < nlegend; k++) {
                const ChunkTypeDef *d = legend[k];
                float w = 14.0f * z + ui_text_width(ui, FONT_SMALL, d->name) + 16.0f * z;
                if (lx + w > px + pad + lw) break;
                ui_draw_rect(ui, lx, y + 2.0f * z, 9.0f * z, 9.0f * z,
                             ui_rgba_hex(qlcg_chunk_display_color(d), 255));
                snprintf(buf, sizeof(buf), "%s %s", d->code ? d->code : "", d->name);
                ui_draw_text(ui, FONT_SMALL, lx + 12.0f * z, y + 1.0f * z, buf,
                             ui->pal.text_sub);
                lx += w;
            }
        }
        y += small_h * 1.3f;
    }

    /* ---- 当前区块属性 */
    {
        ChunkInspectInfo cur;
        qlcg_world_inspect_chunk(g_app.world, g_app.player_cx, g_app.player_cz, &cur);

        snprintf(buf, sizeof(buf), "%s (%d, %d) · %s", tr("debug.currentChunk"),
                 g_app.player_cx, g_app.player_cz, player_state_text());
        ui_draw_text(ui, FONT_SMALL, px + pad, y, buf, ui->pal.text_main);
        y += small_h;

        snprintf(buf, sizeof(buf), "%s: %s %s", tr("debug.type"),
                 cur.type_def->code ? cur.type_def->code : "-", qlcg_chunk_type_name(cur.type_def));
        ui_draw_text(ui, FONT_SMALL, px + pad, y, buf, ui->pal.text_main);
        y += small_h;

        snprintf(buf, sizeof(buf), "%s: %.1f°, %.1f°", tr("debug.coord"),
                 cur.lon_deg, cur.lat_deg);
        ui_draw_text(ui, FONT_SMALL, px + pad, y, buf, ui->pal.text_sub);
        y += small_h;

        const WeatherProfile *wp = g_app.terrain.cur_weather
                                 ? g_app.terrain.cur_weather : qlcg_get_weather_profile(0);
        snprintf(buf, sizeof(buf), "%s: %s %s", tr("debug.weather"), wp->code, wp->name);
        ui_draw_text(ui, FONT_SMALL, px + pad, y, buf, ui->pal.text_sub);
        y += small_h;

        snprintf(buf, sizeof(buf), "%s: %s", tr("debug.faction"),
                 cur.territory ? qlcg_faction_display_name(cur.territory) : tr("debug.faction.none"));
        ui_draw_text(ui, FONT_SMALL, px + pad, y, buf, ui->pal.text_sub);
        y += small_h;

        if (g_app.region && g_app.region->stream) {
            RegionStream *st = g_app.region->stream;
            snprintf(buf, sizeof(buf), tr("debug.stream"), st->count,
                     st->offset_count - st->cursor, st->radius * CHUNK_SIZE);
            ui_draw_text(ui, FONT_SMALL, px + pad, y, buf, ui->pal.accent_tea);
        }
        y += small_h + gap * 1.6f;
    }

    /* ---- 准星探针 */
    {
        ui_draw_line(ui, px + pad, y - gap * 0.6f, px + pw - pad, y - gap * 0.6f,
                     1.0f * z, ui->pal.border_soft);
        ui_draw_text(ui, FONT_BOLD, px + pad, y,
                     g_app.terrain.aim.kind != AIM_HIT_NONE
                         ? g_app.terrain.aim.title : tr("debug.aim.none"),
                     ui->pal.text_main);
        y += title_h;
        if (g_app.terrain.aim.kind != AIM_HIT_NONE) {
            ui_draw_text(ui, FONT_SMALL, px + pad, y, g_app.terrain.aim.detail1,
                         ui->pal.text_sub);
            y += small_h;
            ui_draw_text(ui, FONT_SMALL, px + pad, y, g_app.terrain.aim.detail2,
                         ui->pal.text_sub);
            y += small_h;
        } else {
            y += small_h;
        }
        y += gap * 1.2f;
    }

    /* ---- 三历 */
    {
        ui_draw_line(ui, px + pad, y - gap * 0.6f, px + pw - pad, y - gap * 0.6f,
                     1.0f * z, ui->pal.border_soft);
        char l1[96], l2[96], l3[96];
        qlcg_calendar_format_debug(&g_app.snap, g_app.clock.mode,
                                   l1, sizeof(l1), l2, sizeof(l2), l3, sizeof(l3));
        ui_draw_text(ui, FONT_SMALL, px + pad, y, l1, ui->pal.text_main);
        ui_draw_text(ui, FONT_SMALL, px + pad, y + small_h, l2, ui->pal.text_sub);
        ui_draw_text(ui, FONT_SMALL, px + pad, y + small_h * 2.0f, l3, ui->pal.text_sub);
        y += small_h * 3.2f;
    }

    /* ---- 调速与时间跳转 */
    {
        const char *lbls[5] = {tr("debug.pause"), "0.5x", "1x", "8x", "64x"};
        const float vals[5] = {0.0f, 0.5f, 1.0f, 8.0f, 64.0f};
        float bw = (pw - pad * 2.0f - 4.0f * 4.0f * z) / 5.0f;
        for (int i = 0; i < 5; i++) {
            bool act = (i == 0 && g_app.clock.paused) ||
                       (i > 0 && !g_app.clock.paused &&
                        fabsf(g_app.clock.time_speed - vals[i]) < 0.1f);
            if (ui_pill(ui, px + pad + (float)i * (bw + 4.0f * z), y, bw, pill_h,
                        lbls[i], act)) {
                if (i == 0) {
                    g_app.clock.paused = !g_app.clock.paused;
                } else {
                    g_app.clock.paused = false;
                    g_app.clock.time_speed = vals[i];
                }
            }
        }
        y += pill_h + gap * 0.9f;

        float b3 = (pw - pad * 2.0f - 2.0f * 4.0f * z) / 3.0f;
        int utc_off = qlcg_timezone_offset_min(
            (float)g_app.player_cx * (float)CHUNK_SIZE, g_app.world->circumference);
        if (ui_pill(ui, px + pad, y, b3, pill_h, tr("debug.jump.hour"), false)) {
            g_app.clock.unix_ms += 7200.0 * 1000.0;
            qlcg_calendar_snapshot(&g_app.clock, utc_off, &g_app.snap);
        }
        if (ui_pill(ui, px + pad + b3 + 4.0f * z, y, b3, pill_h,
                    tr("debug.jump.day"), false)) {
            g_app.clock.unix_ms += 86400.0 * 1000.0;
            qlcg_calendar_snapshot(&g_app.clock, utc_off, &g_app.snap);
        }
        if (ui_pill(ui, px + pad + (b3 + 4.0f * z) * 2.0f, y, b3, pill_h,
                    tr("debug.calendar.toggle"), false)) {
            switch (g_app.clock.mode) {
                case CALENDAR_DEFAULT: g_app.clock.mode = CALENDAR_GREGORIAN; break;
                case CALENDAR_GREGORIAN: g_app.clock.mode = CALENDAR_LUNAR; break;
                case CALENDAR_LUNAR: g_app.clock.mode = CALENDAR_YUAN; break;
                default: g_app.clock.mode = CALENDAR_DEFAULT; break;
            }
            g_app.creator_opt.calendar_mode = g_app.clock.mode;
            qlcg_calendar_snapshot(&g_app.clock, utc_off, &g_app.snap);
        }
    }
}
