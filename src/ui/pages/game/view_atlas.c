/* 游戏内舆图：工具条、视口、详情栏、左下刻度块 */
#include "app/app_state.h"
#include "atlas/atlas_tiles.h"
#include "systems/world/world_infinite.h"
#include "hud_layout.h"
#include "view_atlas_layers.h"

/* base 全局行高 */
#define ATLAS_LINE 1.62f
/* base 工具条按钮 12px × text-scale */
#define ATLAS_FONT_SCALE 1.3f

static float atlas_btn_font(UiContext *ui) {
    return 12.0f * ui->met.scale * ATLAS_FONT_SCALE;
}

/* 读数字号：对齐顶栏人名档 13 */
static float atlas_readout_font(UiContext *ui) {
    return 13.0f * ui->met.scale;
}

/* 工具条高：上下内边距 14 + 按钮高 + 下边框 */
float game_atlas_tools_height(UiContext *ui) {
    float z      = ui->met.size;
    float btn_h  = atlas_btn_font(ui) * ATLAS_LINE + 14.0f * z;
    return 28.0f * z + btn_h + 1.0f * z;
}

/* 详情栏高：上下内边距 11 + 行高 + 上边框 */
float game_atlas_detail_height(UiContext *ui) {
    float z = ui->met.size;
    return 22.0f * z + atlas_readout_font(ui) * ATLAS_LINE + 1.0f * z;
}

/* 左下深色信息块：返回绘制后的下沿 */
static float atlas_tip_block(UiContext *ui, float x, float bottom, float w,
                             const char *line1, const char *line2) {
    float z     = ui->met.size;
    float fpx   = atlas_readout_font(ui);
    float lh    = fpx * ATLAS_LINE;
    float pad   = 13.0f * z;
    float h     = 14.0f * z + lh * (line2 && line2[0] ? 2.0f : 1.0f);
    float y     = bottom - h;
    ui_draw_rect(ui, x, y, w, h, ui_rgba(36, 28, 19, 217));   /* base rgba(36,28,19,.85) */
    ui_draw_border(ui, x, y, w, h, 1.0f, ui_rgba(251, 244, 227, 46));
    float tx = x + pad;
    /* 文字色随 --ql-paper，底色描边写死 */
    ui_draw_text_px(ui, UI_FACE_BODY, fpx, false, tx, y + 7.0f * z, line1,
                    tk_of(ui, QL_SURFACE));
    if (line2 && line2[0])
        ui_draw_text_px(ui, UI_FACE_BODY, fpx, false, tx, y + 7.0f * z + lh, line2,
                        tk_of(ui, QL_SURFACE));
    return y;
}

bool game_view_atlas(UiContext *ui) {
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float sx = ui->safe_x_px;
    float sy = ui->safe_y_px;

    /* base 三段纵向切分：顶栏 → 工具条 → 视口 → 详情栏 */
    float bar_h   = game_top_bar_height(ui);
    float tools_h = game_atlas_tools_height(ui);
    float det_h   = game_atlas_detail_height(ui);

    float vp_x = sx;
    float vp_y = sy + bar_h + tools_h;
    float vp_w = (float)ui->win_w - (ui->safe_edges[0]+ui->safe_edges[2]);
    float vp_h = (float)ui->win_h - vp_y - sy - det_h;
    if (vp_w < 1.0f) vp_w = 1.0f;
    if (vp_h < 1.0f) vp_h = 1.0f;

    bool over_ui = game_atlas_over_ui(ui);
    bool survival = g_app.world->opt.play_mode == PLAY_MODE_SURVIVAL;
    double pwx = (g_app.player_cx + 0.5) * QLCG_CHUNK_SIZE_M;
    double pwz = (g_app.player_cz + 0.5) * QLCG_CHUNK_SIZE_M;
    if (g_app.region && g_app.region->center_cx == g_app.player_cx &&
        g_app.region->center_cz == g_app.player_cz) {
        pwx = (double)g_app.region->center_x + g_app.terrain.x;
        pwz = (double)g_app.region->center_z + g_app.terrain.z;
    }
    if (survival) memset(&g_app.atlas.hover_info, 0, sizeof(g_app.atlas.hover_info));

    /* .game-map 全屏纸底，视口外露的就是它 */
    ui_draw_rect(ui, 0, 0, (float)ui->win_w, (float)ui->win_h, tk_of(ui, QL_SURFACE));

    if (!over_ui && !g_app.esc_paused) {
        float mx = ui->mouse_x - ui->global_dx;
        float my = ui->mouse_y - ui->global_dy;
        if (ui->wheel_delta != 0) {
            float notches = (float)ui->wheel_delta / 120.0f;
            notches = qlcg_clampf(notches, -3.0f, 3.0f);
            qlcg_atlas_zoom_step(&g_app.atlas, g_app.world, notches * 0.40f,
                                 mx, my, vp_x, vp_y, vp_w, vp_h);
        }
        if (!g_app.river_craft.active && survival && ui->mouse_dblclicked &&
            ui_hit(ui, vp_x + ui->global_dx, vp_y + ui->global_dy, vp_w, vp_h)) {
            float px, py;
            if (qlcg_atlas_world_to_screen_vis(&g_app.atlas, g_app.world, pwx, pwz,
                                              vp_x, vp_y, vp_w, vp_h, &px, &py)) {
                float dx = mx - px, dy = my - py;
                const float radius = 16.0f;
                if (dx * dx + dy * dy <= radius * radius) {
                    app_toggle_world_view();
                    return true;
                }
            }
        }
        double mwx = 0, mwz = 0;
        if (qlcg_atlas_screen_to_world_d(&g_app.atlas, g_app.world, mx, my,
                                        vp_x, vp_y, vp_w, vp_h, &mwx, &mwz)) {
            double local_x,local_z;
            MacroWorld *hover=qlcg_infinite_resolve(g_app.world,mwx,mwz,&local_x,&local_z);
            int hcx = hover?(int)floor(local_x / QLCG_CHUNK_SIZE_M):0;
            int hcz = hover?(int)floor(local_z / QLCG_CHUNK_SIZE_M):0;
            g_app.atlas.hover_info=(ChunkInspectInfo){0};
            if (hover && qlcg_world_chunk_explored(hover, hcx, hcz))
                qlcg_world_inspect_chunk(hover, hcx, hcz, &g_app.atlas.hover_info);

            if (!g_app.river_craft.active && (!survival || qlcg_world_flat(g_app.world)) && ui->mouse_dblclicked) {
                if(app_atlas_airdrop(mwx,mwz)) {
                    app_push_toast_k(UI_TOAST_SUCCESS, NULL, tr("atlas.airdrop.done"));
                    return true;
                }
            }
            if(hover==g_app.world && g_app.river_craft.active&&g_app.river_craft.tool!=11&&ui->mouse_clicked) {
                double scale=calc_zoom_scale(&g_app.atlas,g_app.world,vp_w);
                app_river_craft_click(mwx,mwz,fmin(512,fmax(2,12/fmax(1e-9,scale))));
            } else if (!g_app.river_craft.active && !survival && ui->mouse_clicked) {
                if (g_app.atlas.eng_tool == ENG_TOOL_NONE) {
                    g_app.atlas.has_selection = true;
                    g_app.atlas.sel_info = g_app.atlas.hover_info;
                } else if (hover==g_app.world && g_app.atlas.eng_tool == ENG_TOOL_RIVER_DIVERT) {
                    if (!g_app.atlas.river_Release_pending) {
                        g_app.atlas.river_x0 = mwx;
                        g_app.atlas.river_z0 = mwz;
                        g_app.atlas.river_Release_pending = true;
                        app_push_toast_k(UI_TOAST_WARNING, NULL, tr("atlas.river.src"));
                    } else {
                        qlcg_eng_macro_river(g_app.world, g_app.atlas.river_x0,
                                             g_app.atlas.river_z0, mwx, mwz, 180.0f);
                        g_app.atlas.river_Release_pending = false;
                        app_push_toast(tr("atlas.river.done"));
                    }
                } else if(hover==g_app.world) {
                    qlcg_eng_macro_brush(g_app.world, g_app.atlas.eng_tool, mwx, mwz,
                                         g_app.atlas.brush_radius);
                }
            }
        }
    }

    AtlasPlayerMarker player = {pwx, pwz, g_app.terrain.yaw};
    qlcg_atlas_render_player_viewport(&g_app.atlas, g_app.world, &g_app.clock, ui,
                                      vp_x, vp_y, vp_w, vp_h, &player);

    /* 501：模糊舆图视口后重抓玻璃背景 */
    float sfx_blur = ui_status_feedback_blur(&g_app.status_feedback);
    if (sfx_blur > 0.0f) {
        ui_backdrop_blur_overlay(ui, sfx_blur);
        ui_backdrop_capture(ui);
    }

    /* ---- 工具条：通栏浅底，按钮组靠右 ---- */
    float tools_y = sy + bar_h;
    ui_draw_rect(ui, 0, tools_y, (float)ui->win_w, tools_h, tk_of(ui, QL_SURFACE_HI));
    ui_draw_rect(ui, 0, tools_y + tools_h - 1.0f, (float)ui->win_w, 1.0f, tk_of(ui, QL_LINE));

    float bf    = atlas_btn_font(ui);
    float rf    = atlas_readout_font(ui);
    float btn_h = bf * ATLAS_LINE + 14.0f * z;
    float btn_y = tools_y + 14.0f * z;
    float gap   = 8.0f * z;
    float sel_w = 132.0f * z;

    /* 工具条顺序：视图/全览/＋/－/定位/调整/山川 */
    const char *lbl[6];
    lbl[0] = tr("atlas.btn.globe");
    lbl[1] = tr("atlas.btn.zoomIn");
    lbl[2] = tr("atlas.btn.zoomOut");
    lbl[3] = tr("atlas.btn.locate");
    lbl[4] = tr("atlas.btn.adjust");
    lbl[5] = tr("atlas.btn.enterTerrain");
    const int   act[6] = { 5, 4, 3, 2, 1, 0 };   /* 对应原 case 序号 */
    float bw[6];
    bool projection=!survival && !qlcg_world_flat(g_app.world);
    float total = projection ? sel_w : -gap;
    for (int i = 0; i < 6; i++) {
        if (survival && (i == 0 || i == 4)) continue;
        bw[i]  = (float)ui_text_width_px(ui, UI_FACE_BODY, bf, false, lbl[i]) + 24.0f * z;
        total += gap + bw[i];
    }
    float bx = (float)ui->win_w - sx - 20.0f * z - total;

    /* 视图下拉 */
    if (projection) {
        const char *items[3] = { tr("atlas.view.globe"), tr("atlas.view.equir"), tr("atlas.view.mercator") };
        int cur = (int)g_app.atlas.planet_view;
        if (cur < 0 || cur > 2) cur = 0;
        /* 下拉 12px × text-scale，异于表单字号 */
        ui_select_sized(ui, 9101, bx, btn_y, sel_w, btn_h, items, 3, cur, bf);
        bx += sel_w + gap;
    }

    for (int i = 0; i < 6; i++) {
        if (survival && (i == 0 || i == 4)) continue;
        bool hit = ui_btn(ui, bx, btn_y, bw[i], btn_h, lbl[i], BTN_GHOST, false, bf, false);
        if (hit) {
            switch (act[i]) {
                case 0:
                    if (survival) app_toggle_world_view();
                    else app_airdrop_to_chunk(g_app.player_cx, g_app.player_cz, true);
                    return true;
                case 1: g_app.atlas.layer_panel_open = !g_app.atlas.layer_panel_open; break;
                case 2: qlcg_atlas_center_on(&g_app.atlas, g_app.world, pwx, pwz, true); break;
                case 3: qlcg_atlas_zoom_step(&g_app.atlas, g_app.world, -0.485f,
                                             vp_x + vp_w * 0.5f, vp_y + vp_h * 0.5f,
                                             vp_x, vp_y, vp_w, vp_h); break;
                case 4: qlcg_atlas_zoom_step(&g_app.atlas, g_app.world, 0.485f,
                                             vp_x + vp_w * 0.5f, vp_y + vp_h * 0.5f,
                                             vp_x, vp_y, vp_w, vp_h); break;
                default: qlcg_atlas_reset_view(&g_app.atlas, g_app.world, false); break;
            }
        }
        bx += bw[i] + gap;
    }

    /* ---- 详情栏：左右随安全区内缩 ---- */
    float det_y = (float)ui->win_h - sy - det_h;
    float det_w = (float)ui->win_w - (ui->safe_edges[0]+ui->safe_edges[2]);
    ui_draw_rect(ui, sx, det_y, det_w, det_h, tk_of(ui, QL_SURFACE_HI));
    ui_draw_rect(ui, sx, det_y, det_w, 1.0f, tk_of(ui, QL_LINE_SOFT));
    {
        const ChunkInspectInfo *ci = !survival && g_app.atlas.has_selection ? &g_app.atlas.sel_info
                                                               : &g_app.atlas.hover_info;
        char line[256];
        if (ci->type_def) {
            snprintf(line, sizeof(line), tr("atlas.info1"),
                     ci->type_def->code, qlcg_chunk_type_name(ci->type_def), ci->cx, ci->cz,
                     ci->elevation, ci->temperature);
        } else {
            snprintf(line, sizeof(line), "%s", tr(survival ? "game.map.exploreHint" : "game.map.hint"));
        }
        ui_draw_text_ellipsis(ui, UI_FACE_MONO, rf, false, sx + 20.0f * z,
                              det_y + 11.0f * z + (rf * ATLAS_LINE - rf) * 0.5f,
                              (float)ui->win_w - (ui->safe_edges[0]+ui->safe_edges[2]) - 40.0f * z, 0, line,
                              tk_of(ui, QL_INK_2));
    }

    /* ---- 左下块：放大层级 + 比例尺 ---- */
    {
        /* L 取 LOD 层级，随缩放单调 */
        int lvl = qlcg_atlas_river_level(&g_app.atlas, g_app.world, vp_w, vp_h);

        char l1[64], l2[48];
        snprintf(l1, sizeof(l1), tr("atlas.level"), lvl);

        float zscale = ((float)vp_w / g_app.world->circumference) * powf(1.42f, g_app.atlas.zoom - 2.0f);
        float m_per_px = (zscale > 0.0f) ? (1.0f / zscale) : 0.0f;
        float bar_m = 0.0f;
        if (m_per_px > 0.0f) {
            float target_m = m_per_px * 110.0f;
            float p10 = powf(10.0f, floorf(log10f(qlcg_maxf(target_m, 1e-3f))));
            float nice = p10;
            if (target_m > p10 * 5.0f)      nice = p10 * 10.0f;
            else if (target_m > p10 * 2.0f) nice = p10 * 5.0f;
            else if (target_m > p10)        nice = p10 * 2.0f;
            bar_m = nice;
        }
        if (bar_m >= 1000.0f)      snprintf(l2, sizeof(l2), "%.0f km", bar_m * 0.001f);
        else if (bar_m >= 1.0f)    snprintf(l2, sizeof(l2), "%.0f m", bar_m);
        else                       snprintf(l2, sizeof(l2), "%.0f cm", bar_m * 100.0f);

        float tw1 = (float)ui_text_width_px(ui, UI_FACE_BODY, rf, false, l1);
        float tw2 = (float)ui_text_width_px(ui, UI_FACE_BODY, rf, false, l2);
        float box_w  = qlcg_maxf(260.0f * z, qlcg_maxf(tw1, tw2) + 26.0f * z);
        float bottom = det_y - 12.0f * z;
        atlas_tip_block(ui, sx + 12.0f * z, bottom, box_w, l1, l2);
    }

    if (!survival) game_atlas_layer_panel(ui);
    app_river_craft_draw(ui,vp_x,vp_y,vp_w,vp_h);
    return false;
}
