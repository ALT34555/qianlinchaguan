/* ESC 菜单：右侧玻璃抽屉 */
#include "app/app_state.h"
#include "app/app_doll_control.h"
#include "hud_layout.h"
#include "ui/ui_sprite.h"
#include "ui/ui_scroll.h"
#include <stdio.h>

#define SLOT_NAME_INPUT_BASE 0x5A7665u

/* base 抽屉宽：520z 与窗宽取小再乘安全区 */
float game_esc_drawer_width(UiContext *ui) {
    float ratio = 1.0f - 2.0f * ui->safe_x_pct * 0.01f;
    if (ratio < 0.2f) ratio = 0.2f;
    return qlcg_minf(520.0f * ui->met.size, (float)ui->win_w) * ratio;
}

/* 保存后导出当前文件 */
static bool save_and_export(void) {
    int slot = g_app.manual_slot_user ? g_app.manual_slot : app_manual_slot_default();
    if (!app_save_current(slot, true)) return false;
    return app_export_current_save();
}

static void sync_manual_slot_name(int slot) {
    const SaveSlotMeta *meta = app_current_save_slot(slot);
    char *name = g_app.manual_slot_names[slot - SAVE_SLOT_MANUAL_1];
    ui_u8_copy(name, QLCG_SAVE_SLOT_DISPLAY_NAME_BYTES,
               meta ? qlcg_save_slot_display_name(meta) : "");
}

static void open_manual_slots(void) {
    for (int slot = SAVE_SLOT_MANUAL_1; slot <= SAVE_SLOT_MANUAL_3; slot++)
        sync_manual_slot_name(slot);
    g_app.manual_slots_open = true;
}

static float draw_save_slots(UiContext *ui, float x, float y, float w, int *load_slot) {
    float z = ui->met.size;
    float px = 11.5f * ui->met.scale;
    float gap = 6.0f * z;
    float row_h = ui_ctl_height(30.0f * z, 13.0f * ui->met.scale);
    const char *store = tr("game.pause.store");
    const char *rename = tr("saves.rename");
    const char *enter = tr("saves.slotLoad");
    float store_w = qlcg_minf(w * 0.25f, ui_text_width_px(ui, UI_FACE_BODY, px, false, store) + 16.0f * z);
    float rename_w = qlcg_minf(w * 0.25f, ui_text_width_px(ui, UI_FACE_BODY, px, false, rename) + 16.0f * z);
    float enter_w = qlcg_minf(w * 0.25f, ui_text_width_px(ui, UI_FACE_BODY, px, false, enter) + 16.0f * z);
    float ry = y;
    for (int slot = 0; slot < QLCG_SAVE_SLOT_COUNT; slot++) {
        const SaveSlotMeta *meta = app_current_save_slot(slot);
        bool manual = slot >= SAVE_SLOT_MANUAL_1 && slot <= SAVE_SLOT_MANUAL_3;
        char label[64];
        app_slot_kind_label(slot, label, sizeof(label));
        float action_w = meta ? enter_w : (manual ? store_w : 0.0f);
        float detail_x = x + w * 0.36f;
        float detail_w = w * 0.64f - (action_w > 0.0f ? action_w + gap : 0.0f);
        float ty = ry + (row_h - px * 1.35f) * 0.5f;
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, px, true, x, ty, w * 0.36f - gap, 0,
                              label, tk_of(ui, QL_INK));
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, px, false, detail_x, ty, detail_w, 0,
                              meta ? meta->saved_at_str : tr("saves.slotEmpty"), tk_of(ui, QL_INK_2));
        if (meta && ui_btn(ui, x + w - enter_w, ry, enter_w, row_h,
                           enter, BTN_GHOST, false, px, false))
            *load_slot = slot;
        if (!meta && manual && ui_btn(ui, x + w - store_w, ry, store_w, row_h,
                                      store, BTN_GHOST, false, px, false)) {
            if (app_save_manual_slot(slot, "")) sync_manual_slot_name(slot);
        }
        ry += row_h + gap;
        if (!manual || !meta) {
            ry += gap;
            continue;
        }
        char *name = g_app.manual_slot_names[slot - SAVE_SLOT_MANUAL_1];
        float store_x = x + w - store_w;
        float name_w = store_x - x - rename_w - gap * 2.0f;
        ui_text_input(ui, SLOT_NAME_INPUT_BASE + (uint32_t)slot, x, ry, name_w, row_h,
                      name, QLCG_SAVE_SLOT_DISPLAY_NAME_BYTES, 80, tr("game.pause.slotName"));
        if (ui_btn(ui, store_x - gap - rename_w, ry, rename_w, row_h,
                   rename, BTN_QUIET, false, px, false) &&
            strcmp(name, qlcg_save_slot_display_name(meta)) != 0) {
            if (app_rename_manual_slot(slot, name)) sync_manual_slot_name(slot);
        }
        if (ui_btn(ui, store_x, ry, store_w, row_h, store, BTN_GHOST, false, px, false)) {
            const char *stored_name = !meta->name[0] && strcmp(name, meta->default_name) == 0 ? "" : name;
            if (app_save_manual_slot(slot, stored_name)) sync_manual_slot_name(slot);
        }
        ry += row_h + 12.0f * z;
    }
    return ry - y;
}

void game_esc_menu(UiContext *ui) {
    static float anim = 0.0f;   /* 滑入进度，关闭时复位 */
    if (!g_app.esc_paused) { anim = 0.0f; return; }

    float z  = ui->met.size;
    float sc = ui->met.scale;
    float sx = ui->safe_x_px;
    float sy = ui->safe_y_px;

    /* 无全屏遮罩：抽屉自带玻璃底，画面保持可见 */
    float dw = game_esc_drawer_width(ui);
    float dh = (float)ui->win_h - (ui->safe_edges[1]+ui->safe_edges[3]);
    float dy = sy;

    /* 滑入：对齐 base 0.28s ease-out */
    if (ui->reduce_motion) anim = 1.0f;
    else                   anim = qlcg_minf(1.0f, anim + ui->dt * 3.6f);
    float ease = 1.0f - (1.0f - anim) * (1.0f - anim) * (1.0f - anim);
    float dx = (float)ui->win_w - sx - dw + dw * (1.0f - ease);

    /* 抽屉：玻璃底 + 左缘亮线 + 投影 12% */
    ui_shadow_soft(ui, dx, dy, dw, dh, 28.0f * z, 0.0f,
                   with_alpha(tk_of(ui, QL_SHADOW_LG), 0.12f));
    ui_draw_rect_glass(ui, dx, dy, dw, dh, tk_of(ui, QL_AERO_TINT));
    ui_draw_rect_grad(ui, dx, dy, dw, dh * 0.64f, ui_rgba(255, 255, 255, 158),
                      ui_rgba(255, 255, 255, 10));
    ui_draw_rect_grad(ui, dx, dy + dh * 0.64f, dw, dh * 0.36f, ui_rgba(255, 255, 255, 10),
                      ui_rgba(255, 255, 255, 51));
    ui_draw_rect(ui, dx, dy, 1.0f, dh, tk_of(ui, QL_AERO_LINE));
    ui_draw_rect(ui, dx, dy, dw, 1.0f, tk_of(ui, QL_AERO_LINE));

    float pad = 24.0f * z;
    float ix  = dx + pad;
    float iw  = dw - pad * 2.0f;
    /* 内容起点：顶栏下 + 26px（对齐 base） */
    float content_top = game_character_chat_top(ui) + 12.0f * z;
    float view_h = dh - (content_top - dy) - 24.0f * z;
    if (view_h < 120.0f) view_h = 120.0f;
    float sc_off = ui_scroll_begin(ui, 0x5A7A01u, dx, content_top, dw, view_h);
    float y = content_top - sc_off;

    if (g_app.world_menu_auto_pause) {
        float desc_px = 12.0f * sc;
        float desc_lh = desc_px * 1.62f;
        int nl = ui_draw_text_wrapped(ui, UI_FACE_BODY, desc_px, false, ix, y, iw,
                                     desc_lh, tr("game.pause.description"), tk_of(ui, QL_INK_2));
        y += (float)(nl > 0 ? nl : 1) * desc_lh + 28.0f * z;
    }

    float btn_px = 12.5f * sc * 1.3f;
    float btn_h  = btn_px * 1.62f + 20.0f * z + 2.0f * z;
    float gap    = 8.0f * z;

    if (g_app.world) {
        bool open_bag = ui_btn(ui, ix, y, iw, btn_h, tr("controls.openInventory"),
                               BTN_GHOST, false, btn_px, false);
        const SpriteDef *bag = ui_sprite_by_name("btn-inventory");
        float icon = qlcg_minf(30*z, btn_h*.65f);
        if (bag)
            ui_sprite_icon4(ui, bag, ix+10*z, y+(btn_h-icon)*.5f, icon,
                            tk_of(ui,QL_SURFACE), tk_of(ui,QL_PRIMARY_UI), tk_of(ui,QL_ACCENT_UI));
        y += btn_h + gap;
        if (open_bag) {
            ui_pointer_consume(ui);
            app_text_focus_clear();
            g_app.manual_slots_open = false;
            g_app.esc_paused = false;
            game_inventory_toggle();
            g_app.inventory_ui.from_menu = g_app.inventory_ui.open;
        }
    }
    if (app_doll_controlled(NULL)) {
        if (ui_btn(ui,ix,y,iw,btn_h,tr("doll.release"),BTN_GHOST,false,btn_px,false)) {
            if (app_doll_release()) g_app.esc_paused=false;
            else app_push_toast_k(UI_TOAST_WARNING,NULL,tr("doll.unavailable"));
            ui_pointer_consume(ui);
        }
        y+=btn_h+gap;
    }
    if (g_app.world && ui_btn(ui, ix, y, iw, btn_h, tr("game.pause.save"),
                              BTN_GHOST, false, btn_px, g_app.manual_slots_open)) {
        if (g_app.manual_slots_open) {
            g_app.manual_slots_open = false;
            if (ui->input_id >= SLOT_NAME_INPUT_BASE + SAVE_SLOT_MANUAL_1 &&
                ui->input_id <= SLOT_NAME_INPUT_BASE + SAVE_SLOT_MANUAL_3)
                app_text_focus_clear();
        } else {
            open_manual_slots();
        }
    }
    y += btn_h + gap;
    int load_slot = -1;
    if (g_app.world && g_app.manual_slots_open) y += draw_save_slots(ui, ix, y, iw, &load_slot);

    char kb[64];
    qlcg_bind_key_label(g_app.settings.binds[ACT_QUICKSAVE].key,
                        g_app.settings.binds[ACT_QUICKSAVE].mods, kb, sizeof(kb));
    char hint[128];
    snprintf(hint, sizeof(hint), tr("game.pause.quickHint"), kb);
    ui_draw_text_px(ui, UI_FACE_BODY, 10.0f * sc, false, ix, y,
                    hint, tk_of(ui, QL_INK_3));
    y += 10.0f * sc * 1.6f + 10.0f * z;

    if (ui_btn(ui, ix, y, iw, btn_h, tr("game.pause.export"), BTN_GHOST, false, btn_px, false)) {
        save_and_export();
    }
    y += btn_h + gap;

    if (ui_btn(ui, ix, y, iw, btn_h, tr("game.pause.settings"), BTN_GHOST, false, btn_px, false)) {
        /* 不切页：设置以抽屉叠在场景上，游戏画面保持可见 */
        g_app.settings_in_drawer    = true;
        g_app.settings_back_to_game = true;
        g_app.set_tab               = SET_TAB_PROFILE;
    }
    y += btn_h + gap;

    /* 「回到舆图」只在 3D 视图出现（对齐 base） */
    if (g_app.view_mode == VIEW_MODE_TERRAIN) {
        if (ui_btn(ui, ix, y, iw, btn_h, tr("game.pause.map"), BTN_GHOST, false, btn_px, false)) {
            app_toggle_world_view();
            g_app.esc_paused = false;
        }
        y += btn_h + gap;
    }

    /* .text-button：居中，上外边距 16px */
    y += 16.0f * z - gap;
    if (ui_btn(ui, ix, y, iw, btn_h * 0.9f, tr("game.pause.home"), BTN_QUIET, false, btn_px, false)) {
        g_app.esc_paused = false;
        g_app.rail_tab   = RAIL_IDX_HOME;
        g_app.screen     = SCREEN_START_HOME;
        app_refresh_saves();
    }
    y += btn_h * 0.9f + 20.0f * z;

    ui_scroll_end(ui, 0x5A7A01u, y + sc_off - content_top);
    if (load_slot >= 0) app_load_current_save_slot(load_slot);
}
