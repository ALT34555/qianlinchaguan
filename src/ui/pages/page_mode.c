/* 模式选择页 */
#include "app/app_state.h"
#include "ui/ui_sprite.h"
#include "ui/ui_layout.h"

void render_screen_play_mode_select(UiContext *ui) {
    float z  = ui->met.size;
    float sc = ui->met.scale;
    UiRect page = draw_page_shell(ui);

    float h1 = qlcg_clampf((float)ui->win_w * 0.023f, 20.0f, 27.0f) * sc;
    float head_bottom = ui_layout_split_head(ui, page.x, page.y, page.w, h1, tr("create.title"),
                                             11.0f * sc, tr("mode.choose"));

    const char *TITLES[2] = { tr("mode.survival"), tr("mode.leisure") };
    const char *DESCS[2] = {
        tr("mode.survival.desc"),
        tr("mode.leisure.desc")
    };

    float gap      = ui->met.gap_grid;
    float card_w   = (page.w - gap) * 0.5f;
    float pad      = 20.0f * z;
    float mark     = 40.0f * z;
    float title_px = 18.0f * sc;
    float desc_px  = 12.0f * sc;
    float desc_lh  = desc_px * 1.7f;

    int max_lines = 2;
    for (int i = 0; i < 2; i++) {
        int nl = ui_measure_text_wrapped(ui, UI_FACE_BODY, desc_px, false,
                                         qlcg_maxf(20.0f, card_w - pad * 2.0f), DESCS[i]);
        if (nl > max_lines) max_lines = nl;
    }
    float cont_h = mark + 8.0f * z + title_px * 1.4f + (float)max_lines * desc_lh;
    float need_h = cont_h + pad * 2.0f;

    float avail  = page.y + page.h - head_bottom - 20.0f * z;
    float card_h = qlcg_maxf(need_h, qlcg_clampf(avail * 0.56f, 180.0f * z, 240.0f * z));
    float card_y = head_bottom + qlcg_maxf((avail - card_h) * 0.32f, 10.0f * z);

    for (int i = 0; i < 2; i++) {
        float cx = page.x + (float)i * (card_w + gap);
        bool hov = ui_hit(ui, cx, card_y, card_w, card_h);
        float lift = hov ? -3.0f * z : 0.0f;
        float y  = card_y + lift;

        if (hov) {
            ui_neu(ui, cx, y, card_w, card_h, 10.0f * z, tk_of(ui, QL_NEU_DARK), tk_of(ui, QL_NEU_LIGHT));
        } else {
            ui_neu(ui, cx, y, card_w, card_h, 7.0f * z, tk_of(ui, QL_NEU_DARK), tk_of(ui, QL_NEU_LIGHT));
        }
        ui_draw_rect(ui, cx, y, card_w, card_h, tk_of(ui, QL_SURFACE));
        ui_draw_border(ui, cx, y, card_w, card_h, 1.0f,
                       hov ? tk_of(ui, QL_LINE_PRIMARY) : tk_of(ui, QL_SURFACE));

        float mx = cx + pad;
        float my = y + qlcg_maxf((card_h - cont_h) * 0.5f, pad);
        ui_draw_rect(ui, mx, my, mark, mark, tk_of(ui, QL_PRIMARY_SOFT));
        ui_draw_border(ui, mx, my, mark, mark, 1.0f, tk_of(ui, QL_LINE_PRIMARY));
        {
            const SpriteDef *icon = ui_sprite_by_name((i == 0) ? "menu-lock" : "wave");
            float inset = mark * 0.16f;
            if (icon) {
                ui_sprite_icon4(ui, icon, mx + inset, my + inset, mark - inset * 2.0f,
                                tk_of(ui, QL_SURFACE), tk_of(ui, QL_PRIMARY_UI),
                                tk_of(ui, QL_ACCENT_UI));
            }
        }

        ui_draw_text_ellipsis(ui, UI_FACE_DISPLAY, title_px, false, mx, my + mark + 8.0f * z,
                              card_w - pad * 2.0f, 0, TITLES[i], tk_of(ui, QL_INK));
        ui_draw_text_wrapped(ui, UI_FACE_BODY, desc_px, false, mx,
                             my + mark + 8.0f * z + title_px * 1.4f,
                             card_w - pad * 2.0f, desc_lh, DESCS[i], tk_of(ui, QL_INK_2));

        if (hov && ui->mouse_clicked) {
            g_app.creator_opt = qlcg_default_creator_options(i == 0 ? PLAY_MODE_SURVIVAL : PLAY_MODE_LEISURE,
                                                             &g_app.settings);
            g_app.creator_tab = 0;
            g_app.atlas.planet_view = PLANET_VIEW_GLOBE;
            g_app.atlas.sat_mode = false;
            g_app.atlas.layer_scroll = 0.0f;
            g_app.atlas.layer_dragging = false;
            qlcg_init_atlas_display(&g_app.atlas.display);
            g_app.fold_geo = false;
            app_rebuild_creator_world();
            g_app.screen = SCREEN_WORLD_CREATOR;
        }
    }
}
