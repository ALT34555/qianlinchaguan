/* 世界创建器：书签卷轴 + 舆图预览 */
#include "app/app_state.h"
#include "ui/ui_input.h"
#include "ui/ui_scroll.h"
#include "ui/ui_layout.h"
#include "game/view_atlas_layers.h"
#include "systems/world/faction_naming.h"
#include "systems/world/world_infinite.h"

void creator_label(UiContext *ui, float x, float y, const char *text, uint32_t color) {
    ui_draw_text_px(ui, UI_FACE_BODY, 11.5f * ui->met.scale, false, x, y, text, color);
}

bool creator_fold(UiContext *ui, float x, float y, float w, const char *title, bool *open) {
    UiLayout L;
    ui_layout_begin(&L, ui, x, y, w);
    bool before = *open;
    ui_layout_fold(&L, 0, title, open);
    return *open != before;
}

/* 每格 / 赤道总长：base 同款三档小数 */
static void creator_span_text(float meters, char *buf, size_t cap) {
    if (meters >= 1000.0f) snprintf(buf, cap, "%.3f km", meters / 1000.0f);
    else snprintf(buf, cap, "%.0f m", meters);
}

static void layout_text_field(UiLayout *L, uint32_t id, const char *label,
                              char *value, size_t cap, uint32_t max_chars) {
    UiContext *ui = L->ui;
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float lpx = 11.5f * sc;
    float llh = ui_line_height(UI_FACE_BODY, lpx);
    float fpx = 13.0f * sc;
    float fh  = ui_ctl_height(30.0f * z, fpx);
    float x   = L->x + L->indent;
    float w   = L->w - L->indent;

    ui_draw_text_ellipsis(ui, UI_FACE_BODY, lpx, false, x, L->cursor, w, 0, label, tk_of(ui, QL_INK_2));
    L->cursor += llh + 4.0f * z;
    ui_text_input(ui, id, x, L->cursor, w, fh, value, cap, max_chars, NULL);
    L->cursor += fh + L->row_gap;
}

void render_screen_world_creator(UiContext *ui) {
    float z  = ui->met.size;
    float sc = ui->met.scale;
    UiRect page = draw_page_shell(ui);
    WorldCreatorOptions *opt = &g_app.creator_opt;
    bool leisure = opt->play_mode == PLAY_MODE_LEISURE;
    bool flat=opt->map_mode==WORLD_MAP_INFINITE;
    bool map_visible=false;
    const uint32_t map_select=0x435217u;
    int tab_count = leisure ? 3 : 2;
    if (g_app.creator_tab >= tab_count) g_app.creator_tab = 0;
    int next_tab = g_app.creator_tab;
    bool need_regen = false;
    const uint32_t projection_select = 0x435213u;
    const uint32_t calendar_select = 0x435212u;
    const uint32_t seed_input = 0x435214u;
    static const CalendarMode calendars[] = {
        CALENDAR_DEFAULT, CALENDAR_GREGORIAN, CALENDAR_LUNAR, CALENDAR_YUAN
    };
    bool calendar_visible = false;

    /* 顶行：标题 + 返回按钮（自适应防撞） */
    char head[128];
    snprintf(head, sizeof(head), "%s · %s", tr("create.title"),
             opt->play_mode == PLAY_MODE_SURVIVAL ? tr("mode.survival") : tr("mode.leisure"));
    float hpx    = 18.0f * sc;
    float hlh    = ui_line_height(UI_FACE_DISPLAY, hpx);
    float bpx    = 11.5f * sc;
    float back_h = ui_ctl_height(28.0f * z, bpx);
    const char *s_back = tr("creator.back");
    float back_w = (float)ui_text_width_px(ui, UI_FACE_BODY, bpx, false, s_back) + 26.0f * z;
    float top_h  = qlcg_maxf(hlh, back_h);

    ui_draw_text_ellipsis(ui, UI_FACE_DISPLAY, hpx, false, page.x,
                          page.y + (top_h - hlh) * 0.5f,
                          qlcg_maxf(40.0f, page.w - back_w - 12.0f * z), 0, head, tk_of(ui, QL_INK));
    if (ui_btn(ui, page.x + page.w - back_w, page.y + (top_h - back_h) * 0.5f,
               back_w, back_h, s_back, BTN_GHOST, false, bpx, false)) {
        g_app.screen = SCREEN_PLAY_MODE_SELECT;
        return;
    }

    float fbtn_h = ui_ctl_height(34.0f * z, 15.0f * sc);
    float foot_h = fbtn_h + 14.0f * z;
    float body_y = page.y + top_h + 8.0f * z;
    float body_h = qlcg_maxf(80.0f, page.y + page.h - body_y - foot_h);
    float panel_w = qlcg_clampf(page.w * 0.38f, 260.0f * z, 440.0f * z);

    /* 左栏卷轴外框 */
    ui_panel(ui, page.x, body_y, panel_w, body_h, 8.0f, tk_of(ui, QL_SURFACE),
             tk_of(ui, QL_LINE_SOFT), tk_of(ui, QL_SHADOW_SM));

    /* 书签 */
    float tab_px = 13.0f * sc;
    float tab_h  = ui_ctl_height(34.0f * z, tab_px);
    float tab_w  = panel_w / tab_count;
    const char *tabs[3] = {
        tr("creator.tab.presets"),
        tr("creator.tab.custom"),
        tr("creator.tab.preview")
    };
    for (int i = 0; i < tab_count; i++) {
        float tx = page.x + tab_w * (float)i;
        bool act = (g_app.creator_tab == i);
        bool hov = ui_hit(ui, tx, body_y, tab_w, tab_h);
        ui_draw_rect(ui, tx, body_y, tab_w, tab_h,
                     act ? tk_of(ui, QL_PRIMARY_SOFT) : (hov ? tk_of(ui, QL_SURFACE_HI) : tk_of(ui, QL_SURFACE)));
        ui_draw_border(ui, tx, body_y, tab_w, tab_h, 1.0f,
                       act ? tk_of(ui, QL_LINE_PRIMARY) : tk_of(ui, QL_LINE_FAINT));
        ui_draw_circle(ui, tx + tab_w * 0.5f, body_y + tab_h + 4.0f * z, 2.2f * z,
                       act ? tk_of(ui, QL_PRIMARY_UI) : with_alpha(tk_of(ui, QL_INK_3), 0.55f));
        ui_draw_text_ellipsis(ui, UI_FACE_DISPLAY, tab_px, false, tx + 4.0f,
                              body_y + (tab_h - tab_px * 1.25f) * 0.5f,
                              qlcg_maxf(8.0f, tab_w - 8.0f), 1, tabs[i],
                              act ? tk_of(ui, QL_PRIMARY_TEXT) : tk_of(ui, QL_INK_2));
        if (hov && ui->mouse_clicked) next_tab = i;
    }

    float scroll_y = body_y + tab_h + 8.0f * z;
    float scroll_h = qlcg_maxf(40.0f, body_y + body_h - scroll_y - 2.0f * z);
    float cx = page.x + 16.0f * z;
    float cw = qlcg_maxf(40.0f, panel_w - 32.0f * z);
    bool adjusting = g_app.creator_tab == 2;
    if (!adjusting) g_app.atlas.layer_dragging = false;
    float sc_off = adjusting ? 0.0f : ui_scroll_begin(ui, 0x5E771Eu, page.x, scroll_y, panel_w, scroll_h);
    float start_y = scroll_y + 8.0f * z - sc_off;
    char buf[192];

    UiLayout L;
    ui_layout_begin(&L, ui, cx, start_y, cw);

    if (g_app.creator_tab == 0) {
        /* 预设页：世界名称 + 星球预设 */
        layout_text_field(&L, 0x435215u, tr("creator.worldName"),
                          opt->world_name, sizeof(opt->world_name), 80);

        if(!flat) {
        ui_layout_note(&L, tr("creator.category.planet"), false);
        static const int ORDER[5] = {1, 2, 0, 3, 4};
        const char *pnames[5] = {
            tr("creator.preset.primordial"),
            tr("creator.preset.frozen"),
            tr("creator.preset.habitable"),
            tr("creator.preset.archipelago"),
            tr("creator.preset.redhot")
        };
        int active_order = -1;
        for (int i = 0; i < 5; i++) {
            if (opt->preset == (WorldPresetId)ORDER[i]) active_order = i;
        }
        int picked = ui_layout_segments(&L, pnames, 5, active_order, 12.0f * sc);
        if (picked >= 0 && picked < 5) {
            qlcg_apply_world_preset(opt, (WorldPresetId)ORDER[picked]);
            need_regen = true;
        }

        ui_layout_note(&L, tr("creator.preset.hint"), true);
        ui_layout_gap(&L, 4.0f * z);

        } else ui_layout_note(&L,tr("creator.infinite.home"),true);

        /* 只读摘要 */
        ui_layout_kv(&L, tr("creator.generationMode"), qlcg_terrain_recipe_name(opt->terrain_recipe));
        {
            char tiltbuf[48];
            snprintf(tiltbuf, sizeof(tiltbuf), "%.1f° · %s", opt->axial_tilt_deg,
                     opt->prograde ? tr("creator.rotation.prograde") : tr("creator.rotation.retrograde"));
            ui_layout_kv(&L, tr("creator.axialTilt"), tiltbuf);
        }
        {
            int rows_n = g_app.world ? g_app.world->rows : 0;
            char grid[96];
            if(flat) { snprintf(grid,sizeof(grid),"100 × 100"); } else if (rows_n > 0) {
                snprintf(grid, sizeof(grid), "%s · %d × %d",
                         qlcg_macro_size_label(opt->macro_size_idx), opt->columns, rows_n);
            } else {
                snprintf(grid, sizeof(grid), "%s · %d",
                         qlcg_macro_size_label(opt->macro_size_idx), opt->columns);
            }
            ui_layout_kv(&L, tr("creator.equatorGrid"), grid);
        }
        {
            char seedbuf[48];
            snprintf(seedbuf, sizeof(seedbuf), "%u", opt->seed);
            ui_layout_kv(&L, tr("creator.seedShort"), seedbuf);
        }
    } else if (g_app.creator_tab == 1) {
        /* 自定义页 */
        const char *modes[2]={tr("creator.map.planet"),tr("creator.map.infinite")};
        float map_y=L.cursor;
        ui_layout_select(&L,map_select,tr("creator.mapMode"),modes,2,(int)opt->map_mode);
        map_visible=L.cursor-L.row_gap>scroll_y && map_y<scroll_y+scroll_h;
        char seed_text[sizeof(opt->seed_str)];
        ui_u8_copy(seed_text, sizeof(seed_text),
                   ui->input_id == seed_input ? ui->input_buf : opt->seed_str);
        layout_text_field(&L, 0x435216u, tr("player.worldName"),
                          opt->ren_ming, sizeof(opt->ren_ming), 40);

        int h_min = (opt->play_mode == PLAY_MODE_SURVIVAL) ? 140 : 50;
        int h_max = (opt->play_mode == PLAY_MODE_SURVIVAL) ? 210 : 500;
        MeasurementSystem msys = g_app.settings.measurement_system;
        const char *h_key = (msys == MEAS_MING) ? "profile.height.ming"
                          : (msys == MEAS_IMPERIAL ? "profile.height.imperial" : "profile.height.metric");
        ui_layout_height_input(&L, 0x48475402u, tr(h_key), msys, &opt->height_cm, h_min, h_max);

        ui_layout_note(&L, tr("creator.macroTerrain"), false);
        const char *rnames[4] = {
            tr("creator.macroTerrain.balanced"),
            tr("creator.macroTerrain.archipelago"),
            tr("creator.macroTerrain.mountains"),
            tr("creator.macroTerrain.rift")
        };
        int rp = ui_layout_segments(&L, rnames, 4, (int)opt->terrain_recipe, 11.0f * sc);
        if (rp >= 0) {
            opt->terrain_recipe = (MacroTerrainRecipe)rp;
            need_regen = true;
        }

        /* 世界种子 + 随机按钮 */
        ui_layout_note(&L, tr("creator.seed"), false);
        {
            float fh = ui_ctl_height(30.0f * z, 13.0f * sc);
            const char *s_rnd = tr("creator.random");
            float rbw = (float)ui_text_width_px(ui, UI_FACE_BODY, 11.0f * sc, false, s_rnd) + 20.0f * z;
            float sw  = qlcg_maxf(40.0f, cw - rbw - 6.0f * z);
            ui_text_input(ui, seed_input, cx, L.cursor, sw, fh,
                          seed_text, sizeof(seed_text), 0, NULL);
            if (ui_btn(ui, cx + sw + 6.0f * z, L.cursor, rbw, fh, s_rnd,
                       BTN_GHOST, false, 11.0f * sc, false)) {
                opt->seed = qlcg_hash32(opt->seed + ui->frame_idx * 1664525u + 1013904223u);
                snprintf(opt->seed_str, sizeof(opt->seed_str), "%u", opt->seed);
                need_regen = true;
            } else if (ui->input_id != seed_input && seed_text[0] &&
                       strcmp(seed_text, opt->seed_str) != 0) {
                uint32_t seed = qlcg_hash_str(seed_text);
                ui_u8_copy(opt->seed_str, sizeof(opt->seed_str), seed_text);
                if (seed != opt->seed) {
                    opt->seed = seed;
                    need_regen = true;
                }
            }
            L.cursor += fh + L.row_gap;
        }

        if (ui_layout_checkbox(&L, tr("creator.artificial"), &opt->artificial_enabled)) need_regen = true;
        if (ui_layout_checkbox(&L, tr("player.flagSpawn"), &opt->spawn_as_flag)) need_regen = true;

        /* 地理折叠组 */
        if (ui_layout_fold(&L, 0x435201u, tr("creator.fold.geoFull"), &g_app.fold_geo)) {
            if(!flat) {
            const char *snames[3] = {
                tr("creator.macroSize.0"),
                tr("creator.macroSize.1"),
                tr("creator.macroSize.2")
            };
            int sp = ui_layout_segments(&L, snames, 3, opt->macro_size_idx, 11.0f * sc);
            if (sp >= 0) {
                opt->macro_size_idx = sp;
                qlcg_sync_creator_dimensions(opt);
                need_regen = true;
            }

            } else ui_layout_note(&L,tr("creator.infinite.size"),true);
            {
                enum { SCALE_STOPS = 7 };
                char span[32];
                char values[SCALE_STOPS][12];
                const char *labels[SCALE_STOPS];
                for (int i = 0; i < SCALE_STOPS; ++i) {
                    snprintf(values[i], sizeof(values[i]), "%d", qlcg_macro_scale_chunks(i));
                    labels[i] = values[i];
                }
                float scale_index = (float)opt->macro_scale_idx;
                if (ui_layout_slider_steps(&L, tr("creator.macroScale"), &scale_index,
                                           0.0f, SCALE_STOPS - 1, SCALE_STOPS, 0.0f, labels, NULL)) {
                    opt->macro_scale_idx = (int)scale_index;
                    qlcg_sync_creator_dimensions(opt);
                    need_regen = true;
                }
                ui_layout_gap(&L, ui_line_height(UI_FACE_BODY, 10.5f * sc));
                creator_span_text((float)opt->equator_chunks * (float)QLCG_CHUNK_SIZE_M,
                                  span, sizeof(span));
                ui_layout_kv(&L, tr(flat?"creator.infinite.width":"creator.macroDimensions"), span);
            }

            if(!flat) {
            char vbuf[48];
            snprintf(vbuf, sizeof(vbuf), "%.0f%%", opt->land_ratio * 100.0f);
            if (ui_layout_slider(&L, tr("creator.landRatio"), &opt->land_ratio, 0.12f, 0.72f, vbuf)) need_regen = true;
            snprintf(vbuf, sizeof(vbuf), "%.0f%%", opt->precipitation * 100.0f);
            float rain_step = opt->precipitation * 5.0f;
            if (ui_layout_slider_steps(&L, tr("creator.precipitation"), &rain_step,
                                       0.0f, 10.0f, 11, 0.0f, NULL, vbuf)) {
                opt->precipitation = rain_step / 5.0f;
                need_regen = true;
            }
            float tec = (float)opt->tectonic_activity;
            snprintf(vbuf, sizeof(vbuf), "%d", opt->tectonic_activity);
            if (ui_layout_slider(&L, tr("creator.tectonicActivity"), &tec, 0.0f, 9.0f, vbuf)) {
                opt->tectonic_activity = qlcg_clampi((int)roundf(tec), 0, 9);
                need_regen = true;
            }
            snprintf(vbuf, sizeof(vbuf), "%.0f℃ / %.0f℃", opt->pole_temp, opt->equator_temp);
            if (ui_layout_slider(&L, tr("creator.equatorTemp"), &opt->equator_temp, -5.0f, 48.0f, vbuf)) need_regen = true;
            } else {
                ui_layout_note(&L,tr("creator.infinite.home"),true);
                if(g_app.world && qlcg_world_flat(g_app.world)) {
                    char measured[64];
                    snprintf(measured,sizeof(measured),"%.2f%% (%d / %d)",
                             100.0*g_app.world->generated_land_cells/g_app.world->cell_count,
                             g_app.world->generated_land_cells,g_app.world->cell_count);
                    ui_layout_kv(&L,tr("creator.infinite.actualLand"),measured);
                }
            }
        }

        /* 天文折叠组 */
        if (ui_layout_fold(&L, 0x435202u, tr("creator.fold.skyFull"), &g_app.fold_astro)) {
            if(!flat) {
            if (ui_layout_checkbox(&L, tr("creator.astronomy"), &opt->astronomy_enabled) && g_app.world) {
                g_app.world->opt.astronomy_enabled = opt->astronomy_enabled;
                g_app.world->opt.astronomy_version = opt->astronomy_version;
                if (opt->astronomy_enabled) {
                    if(!g_app.world->astra_events)g_app.world->astra_events=qlcg_astra_events_create(opt->seed);
                    qlcg_astra_upload(&g_app.terrain.astronomy, opt);
                }
                else qlcg_astra_release(&g_app.terrain.astronomy);
            }
            const char *cals[4];
            int current = 0;
            for (int i = 0; i < 4; ++i) {
                cals[i] = qlcg_calendar_mode_name(calendars[i]);
                if (opt->calendar_mode == calendars[i]) current = i;
            }
            float calendar_y = L.cursor;
            ui_layout_select(&L, calendar_select, tr("creator.calendar"), cals, 4, current);
            calendar_visible = L.cursor - L.row_gap > scroll_y &&
                               calendar_y < scroll_y + scroll_h;

            if (ui_layout_number_input(&L, 0x435210u, tr("creator.axialTilt"),
                                       &opt->axial_tilt_deg, 0.0f, 60.0f, 0.5f, 1, "°"))
                need_regen = true;
            {
                const char *rots[2] = {
                    tr("creator.rotation.prograde"), tr("creator.rotation.retrograde")
                };
                int rp = ui_layout_segments(&L, rots, 2, opt->prograde ? 0 : 1, 11.0f * sc);
                if (rp >= 0 && ((rp == 0) != opt->prograde)) {
                    opt->prograde = (rp == 0);
                    need_regen = true;
                }
            }
            {
                float md = (float)opt->monsoon_direction;
                if (ui_layout_number_input(&L, 0x435211u, tr("creator.monsoon"),
                                           &md, 0.0f, 359.0f, 1.0f, 0, "°")) {
                    opt->monsoon_direction = qlcg_clampi((int)roundf(md), 0, 359);
                    need_regen = true;
                }
            }

            }
            float spd = opt->creation_time_speed;
            snprintf(buf, sizeof(buf), "%.0f×", spd);
            /* 速度0~360；base预设0/1/72/360 */
            if (ui_layout_slider(&L, tr("settings.speed"), &spd, 0.0f, 360.0f, buf)) opt->creation_time_speed = spd;
        }

        /* 人间折叠组 */
        if (ui_layout_fold(&L, 0x435203u, tr("creator.fold.humanFull"), &g_app.fold_human)) {
            ui_layout_note(&L, tr("creator.culture"), false);
            ui_layout_note(&L, tr("creator.culture.builtin"), true);
            int count = qlcg_i18n_culture_count();
            bool changed = false;
            bool checked[QLCG_MAX_LOCALES];
            for (int i = 0; i < count; ++i) {
                if (i == 2) ui_layout_note(&L, tr("creator.culture.mods"), true);
                const QlLocaleMeta *meta = qlcg_i18n_culture_meta(i);
                const char *label = i == 0 ? tr("culture.zh-CN") :
                                    i == 1 ? tr("culture.en") : meta->label;
                checked[i] = qlcg_culture_checked(opt, meta->code);
                if (ui_layout_checkbox(&L, label, &checked[i])) changed = true;
            }
            if (changed) {
                opt->culture[0] = '\0';
                memset(opt->culture_choices, 0, sizeof(opt->culture_choices));
                int next = 0;
                for (int i = 0; i < count; ++i) {
                    if (checked[i]) {
                        snprintf(opt->culture_choices[next], sizeof(opt->culture_choices[next]),
                                 "%s", qlcg_i18n_culture_meta(i)->code);
                        ++next;
                    }
                }
                if (g_app.world) qlcg_world_set_culture(g_app.world, opt);
            }
            ui_layout_note(&L, tr("creator.culture.hint"), true);
            float civ = (float)opt->initial_civ_limit;
            snprintf(buf, sizeof(buf), "%d", opt->initial_civ_limit);
            if (ui_layout_slider(&L, tr("creator.initialCivilizationLimit"), &civ, 1.0f, 20.0f, buf)) {
                opt->initial_civ_limit = (int)roundf(civ);
                need_regen = true;
            }
            if (ui_layout_checkbox(&L, tr("creator.independence"), &opt->flag_independence)) need_regen = true;
        }
    }

    if (need_regen) app_rebuild_creator_world();

    if (!adjusting) ui_scroll_end(ui, 0x5E771Eu, ui_layout_end(&L) + 12.0f * z);

    /* 右栏：舆图预览与工具条 */
    float vp_x = page.x + panel_w + 12.0f * z;
    float vp_w = qlcg_maxf(80.0f, page.x + page.w - vp_x);
    float vbar_h = ui_ctl_height(26.0f * z, 11.0f * sc);
    float info_lh = ui_line_height(UI_FACE_BODY, 10.5f * sc);
    float vp_y = body_y;
    float vp_h = qlcg_maxf(60.0f, body_h - vbar_h - info_lh - 12.0f * z);
    if (!leisure) {
        float side = qlcg_minf(vp_w, body_h);
        vp_x += (vp_w - side) * 0.5f;
        vp_y += (body_h - side) * 0.5f;
        vp_w = vp_h = side;
    }
    if (g_app.world) {
        qlcg_atlas_render_viewport(&g_app.atlas, g_app.world, &g_app.clock, ui,
                                   vp_x, vp_y, vp_w, vp_h, true,
                                   g_app.world->spawn_cx, g_app.world->spawn_cz);
        ui_draw_border(ui, vp_x, vp_y, vp_w, vp_h, 1.0f, tk_of(ui, QL_LINE_FAINT));
    }
    if (g_app.world && leisure) {
        const char *views[3] = {
            tr("atlas.view.globe"), tr("atlas.view.equir"), tr("atlas.view.mercator")
        };
        float bar_y = vp_y + vp_h + 6.0f * z;
        float zoom_w = ui_ctl_height(26.0f * z, 11.0f * sc);
        float zx = vp_x + vp_w - zoom_w * 2.0f - 4.0f * z;
        float select_w = zx - vp_x - 6.0f * z;
        if (!flat && select_w > 20.0f * z)
            ui_select_sized(ui, projection_select, vp_x, bar_y, select_w, vbar_h,
                            views, 3, (int)g_app.atlas.planet_view, 11.0f * sc);
        float zoom_px = qlcg_minf(12.0f * sc, zoom_w * 0.52f);
        if (ui_btn(ui, zx, bar_y, zoom_w, vbar_h, tr("ui.plus"), BTN_GHOST, false, zoom_px, false))
            qlcg_atlas_zoom_step(&g_app.atlas, g_app.world, 0.485f, vp_x + vp_w * 0.5f, vp_y + vp_h * 0.5f, vp_x, vp_y, vp_w, vp_h);
        if (ui_btn(ui, zx + zoom_w + 4.0f * z, bar_y, zoom_w, vbar_h, tr("ui.minus"), BTN_GHOST, false, zoom_px, false))
            qlcg_atlas_zoom_step(&g_app.atlas, g_app.world, -0.485f, vp_x + vp_w * 0.5f, vp_y + vp_h * 0.5f, vp_x, vp_y, vp_w, vp_h);

        memset(&g_app.atlas.hover_info, 0, sizeof(g_app.atlas.hover_info));
        if (ui_hit(ui, vp_x + ui->global_dx, vp_y + ui->global_dy, vp_w, vp_h)) {
            if (ui->mouse_clicked && ui->mouse_down) {
                g_app.atlas.dragging = true;
                qlcg_platform_capture();
            }
            if (ui->wheel_delta != 0) {
                float dz = qlcg_clampf(ui->wheel_delta / 120.0f, -3.0f, 3.0f) * 0.40f;
                qlcg_atlas_zoom_step(&g_app.atlas, g_app.world, dz, ui->mouse_x - ui->global_dx, ui->mouse_y - ui->global_dy,
                                     vp_x, vp_y, vp_w, vp_h);
            }
            float mwx = 0, mwz = 0;
            if (qlcg_atlas_screen_to_world(&g_app.atlas, g_app.world, ui->mouse_x - ui->global_dx, ui->mouse_y - ui->global_dy,
                                           vp_x, vp_y, vp_w, vp_h, &mwx, &mwz)) {
                int hcx = (int)floorf(mwx / (float)QLCG_CHUNK_SIZE_M);
                int hcz = (int)floorf(mwz / (float)QLCG_CHUNK_SIZE_M);
                qlcg_world_inspect_chunk(g_app.world, hcx, hcz, &g_app.atlas.hover_info);
            }
        }

        const ChunkInspectInfo *ci = &g_app.atlas.hover_info;
        if (ci->type_def) {
            snprintf(buf, sizeof(buf), "(%d, %d) %s %s · %.0fm · %.1f℃",
                     ci->cx, ci->cz, ci->type_def->code, qlcg_chunk_type_name(ci->type_def), ci->elevation, ci->temperature);
        } else {
            snprintf(buf, sizeof(buf), "%s", tr("atlas.viewportTipShort"));
        }
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, 10.5f * sc, false, vp_x,
                              bar_y + vbar_h + 4.0f * z, vp_w, 0, buf, tk_of(ui, QL_INK_3));
    }

    if (adjusting && g_app.world) {
        float bh = ui_ctl_height(28.0f * z, 11.5f * sc);
        float gap = 6.0f * z;
        float bw = (cw - gap) * 0.5f;
        if (ui_btn(ui, cx, scroll_y, bw, bh, tr("atlas.spawn"),
                   BTN_GHOST, false, 11.5f * sc, false))
            qlcg_atlas_reset_view(&g_app.atlas, g_app.world, true);
        if (ui_btn(ui, cx + bw + gap, scroll_y, bw, bh, tr("atlas.overview"),
                   BTN_GHOST, false, 11.5f * sc, false))
            qlcg_atlas_reset_view(&g_app.atlas, g_app.world, false);
        if (ui_btn(ui, cx, scroll_y + bh + gap, cw, bh, tr("atlas.satellite"),
                   g_app.atlas.sat_mode ? BTN_SEGMENT_ON : BTN_GHOST, false, 11.5f * sc, false))
            g_app.atlas.sat_mode = !g_app.atlas.sat_mode;
        float tools_h = 2.0f * (bh + gap);
        if (scroll_h > tools_h) {
            UiRect layers = { page.x, scroll_y + tools_h, panel_w, scroll_h - tools_h };
            atlas_layer_panel(ui, &g_app.atlas, layers);
        }
    }

    /* 底部操作条 */
    float fy = page.y + page.h - fbtn_h - 2.0f * z;
    ui_draw_rect(ui, page.x, fy - 8.0f * z, page.w, 1.0f, tk_of(ui, QL_LINE_SOFT));
    const char *s_enter = tr("creator.enterWorldBtn");
    const char *s_bmap  = tr("creator.browseMapBtn");
    float enter_w = (float)ui_text_width_px(ui, UI_FACE_BODY, 15.0f * sc, false, s_enter) + 40.0f * z;
    if (ui_btn(ui, page.x + page.w - enter_w, fy, enter_w, fbtn_h, s_enter,
               BTN_PRIMARY, false, 15.0f * sc, false)) {
        app_enter_world_from_creator();
        return;
    }
    float map_w = (float)ui_text_width_px(ui, UI_FACE_BODY, 13.0f * sc, false, s_bmap) + 28.0f * z;
    if (leisure && ui_btn(ui, page.x, fy, map_w, fbtn_h, s_bmap, BTN_GHOST, false, 13.0f * sc, false)) {
        next_tab = 2;
    }

    if(ui->select_id==map_select) {
        if(!map_visible) ui->select_id=0;
        else {
            int picked=ui_select_popup(ui);
            if(picked>=0 && picked!=(int)opt->map_mode) {
                opt->map_mode=(WorldMapMode)picked; opt->plane_x=opt->plane_z=0;
                if(picked==WORLD_MAP_INFINITE) qlcg_infinite_options(opt);
                else qlcg_apply_world_preset(opt,WORLD_PRESET_HABITABLE);
                qlcg_sync_creator_dimensions(opt); app_rebuild_creator_world();
            }
        }
    } else if (ui->select_id == projection_select && leisure && g_app.world && !flat) {
        int picked = ui_select_popup(ui);
        if (picked >= 0) qlcg_atlas_set_projection(&g_app.atlas, g_app.world, (PlanetView)picked);
    } else if (ui->select_id == calendar_select) {
        if (!calendar_visible) {
            ui->select_id = 0;
        } else {
            int picked = ui_select_popup(ui);
            if (picked >= 0) {
                opt->calendar_mode = calendars[picked];
                app_refresh_creator_clock();
            }
        }
    }
    g_app.creator_tab = next_tab;
}
