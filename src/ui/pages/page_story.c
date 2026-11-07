/* 存档列表页：按文件折叠 */
#include "app/app_state.h"
#include "ui/ui_sprite.h"
#include "ui/ui_layout.h"
#include "ui/ui_scroll.h"

/* 改名输入框的固定焦点 id */
#define SAVE_RENAME_INPUT_ID 0x5A7E11u
#define SAVE_SLOTS_SCROLL_ID 0x5A7E12u

/* 旧档尾段缺失的槽不可改名 */
static bool save_file_legacy(const SaveFileInfo *f) {
    if (!f) return false;
    for (int i = 0; i < QLCG_SAVE_SLOT_COUNT; i++)
        if (f->slots[i].present && f->slots[i].disk_version < 6u) return true;
    return false;
}

/* 进入改名态并接管键盘焦点 */
static void save_rename_begin(UiContext *ui, const SaveFileInfo *f) {
    if (!f) return;
    ui_u8_copy(g_app.save_edit_id, sizeof(g_app.save_edit_id), f->id);
    ui_u8_copy(g_app.save_edit_buf, sizeof(g_app.save_edit_buf), f->world_name);
    size_t byte_cap = sizeof(g_app.save_edit_buf) - 1;
    if (byte_cap > sizeof(ui->input_buf) - 1) byte_cap = sizeof(ui->input_buf) - 1;
    ui_u8_copy(ui->input_buf, sizeof(ui->input_buf), g_app.save_edit_buf);
    ui->input_id          = SAVE_RENAME_INPUT_ID;
    ui->input_focus_frame = ui->frame_idx;
    ui->input_mode        = UI_INPUT_TEXT;
    ui->input_max_bytes   = byte_cap;
    ui->input_max_chars   = 80;
    ui->input_utf16_hi    = 0;
    ui->input_dirty       = false;
    ui->input_commit      = false;
    UiEditField fld = { ui->input_buf, byte_cap + 1, 80, &ui->edit_field };
    ui_edit_reset(&fld);
    ui_edit_end(&fld, false);
}

/* 退出改名编辑态并放开键盘 */
static void save_rename_end(UiContext *ui) {
    g_app.save_edit_id[0]  = '\0';
    g_app.save_edit_buf[0] = '\0';
    if (ui->input_id == SAVE_RENAME_INPUT_ID) ui->input_id = 0;
    ui->input_dirty  = false;
    ui->input_commit = false;
}

/* 折叠箭头：两笔线段，免额外素材 */
static void draw_chevron(UiContext *ui, float cx, float cy, float size, bool down,
                         uint32_t color) {
    float r = size * 0.5f;
    float t = qlcg_maxf(1.4f * ui->met.size, 1.0f);
    if (down) {
        ui_draw_line(ui, cx - r, cy - r * 0.45f, cx, cy + r * 0.45f, t, color);
        ui_draw_line(ui, cx + r, cy - r * 0.45f, cx, cy + r * 0.45f, t, color);
    } else {
        ui_draw_line(ui, cx - r * 0.45f, cy - r, cx + r * 0.45f, cy, t, color);
        ui_draw_line(ui, cx - r * 0.45f, cy + r, cx + r * 0.45f, cy, t, color);
    }
}

/* 卡片头部高度 */
static float file_head_height(UiContext *ui, float ops_h) {
    float z  = ui->met.size;
    float sc = ui->met.scale;
    float text_block = 13.5f * sc * 1.62f + 3.0f * z + 10.0f * sc * 1.70f;
    return qlcg_maxf(text_block + 22.0f * z, ops_h + 22.0f * z);
}

static float file_card_height(UiContext *ui, bool expanded, float head_h) {
    float z = ui->met.size;
    if (!expanded) return head_h;
    float row_h = ui_ctl_height(30.0f * z, 11.0f * ui->met.scale);
    return head_h + QLCG_SAVE_SLOT_COUNT * (row_h + 4.0f * z) + 10.0f * z;
}

/* 按高度贪心切页 */
static int paginate(const float *heights, int n, float avail, float gap,
                    int *starts, int *ends, int cap) {
    int page = 0, i = 0;
    while (i < n && page < cap) {
        int s = i;
        float used = 0.0f;
        while (i < n) {
            float need = heights[i] + (i > s ? gap : 0.0f);
            if (used + need > avail && i > s) break;
            used += need;
            i++;
        }
        starts[page] = s;
        ends[page] = i;
        page++;
    }
    return page;
}

/* 单行槽：返回是否点了读取 */
static bool draw_slot_row(UiContext *ui, const SaveFileInfo *f, int slot,
                          float x, float y, float w, float row_h, float font_px) {
    float z  = ui->met.size;
    float sc = ui->met.scale;
    const SaveSlotMeta *m = f->slots[slot].present ? &f->slots[slot] : NULL;
    bool newest = (f->newest_slot == slot);
    bool hov = ui_hit(ui, x, y, w, row_h);
    if (m && newest) ui_draw_rect(ui, x, y, w, row_h, with_alpha(tk_of(ui, QL_PRIMARY), 0.06f));
    else if (hov && m) ui_draw_rect(ui, x, y, w, row_h, tk_of(ui, QL_SURFACE_HI));
    ui_draw_rect(ui, x, y + row_h - 1.0f, w, 1.0f, tk_of(ui, QL_LINE_FAINT));

    char kind[48];
    const char *title;
    if (slot == SAVE_SLOT_QUICK || slot == SAVE_SLOT_AUTO) {
        app_slot_kind_label(slot, kind, sizeof(kind));
        title = kind;
    } else if (!m) {
        title = "-";
    } else {
        title = qlcg_save_slot_display_name(m);
        if (!title[0]) title = f->world_name;
    }

    bool clicked = false;
    float right = x + w - 10.0f * z;
    if (m) {
        const char *s_load = tr("saves.slotLoad");
        float bw = (float)ui_text_width_px(ui, UI_FACE_BODY, 11.0f * sc, false, s_load) + 20.0f * z;
        float bh = ui_ctl_height(26.0f * z, 11.0f * sc);
        float bx = x + w - 8.0f * z - bw;
        if (ui_btn(ui, bx, y + (row_h - bh) * 0.5f, bw, bh, s_load,
                   BTN_PRIMARY, false, 11.0f * sc, false))
            clicked = true;
        right = bx - 12.0f * z;
    }

    float tx = x + 10.0f * z;
    const char *tag = newest && m ? tr("saves.latest") : "";
    const char *detail = m ? m->saved_at_str :
        (slot == SAVE_SLOT_QUICK || slot == SAVE_SLOT_AUTO ? tr("saves.slotEmpty") : "");
    float tag_px = 9.5f * sc;
    float detail_px = 10.0f * sc;
    float tag_w = *tag ? ui_text_width_px(ui, UI_FACE_BODY, tag_px, false, tag) + 10.0f * z : 0.0f;
    float detail_w = ui_text_width_px(ui, UI_FACE_BODY, detail_px, false, detail);
    float room = qlcg_maxf(0.0f, right - tx - tag_w - 8.0f * z);
    float title_cap = qlcg_maxf(room * 0.6f, room - detail_w);
    float title_w = qlcg_minf(title_cap, ui_text_width_px(ui, UI_FACE_BODY, font_px, newest, title));
    ui_draw_text_ellipsis(ui, UI_FACE_BODY, font_px, newest, tx,
                          y + (row_h - font_px * 1.35f) * 0.5f, title_w, 0,
                          title, m ? tk_of(ui, QL_INK) : tk_of(ui, QL_INK_3));
    tx += title_w + 8.0f * z;
    if (tag_w > 0.0f && tx + tag_w <= right) {
        ui_draw_text_px(ui, UI_FACE_BODY, tag_px, false, tx,
                        y + (row_h - tag_px * 1.35f) * 0.5f, tag, tk_of(ui, QL_PRIMARY_TEXT));
        tx += tag_w;
    }
    if (right > tx)
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, detail_px, false, tx,
                              y + (row_h - detail_px * 1.35f) * 0.5f, right - tx, m ? 0 : 2,
                              detail, tk_of(ui, QL_INK_3));
    return clicked;
}

void render_screen_memories(UiContext *ui) {
    float z  = ui->met.size;
    float sc = ui->met.scale;
    UiRect page = draw_page_shell(ui);

    /* 目标不在列表时收起改名态与展开态 */
    if (g_app.save_edit_id[0] || g_app.save_expanded_id[0]) {
        bool found_edit = !g_app.save_edit_id[0];
        bool found_open = !g_app.save_expanded_id[0];
        for (int i = 0; i < g_app.save_count; i++) {
            if (!g_app.saves[i].valid) continue;
            if (g_app.save_edit_id[0] && strcmp(g_app.saves[i].id, g_app.save_edit_id) == 0)
                found_edit = true;
            if (g_app.save_expanded_id[0] && strcmp(g_app.saves[i].id, g_app.save_expanded_id) == 0)
                found_open = true;
        }
        if (!found_edit) save_rename_end(ui);
        if (!found_open) g_app.save_expanded_id[0] = '\0';
    }

    float h1 = qlcg_clampf((float)ui->win_w * 0.023f, 20.0f, 27.0f) * sc;
    float head_bottom = ui_layout_split_head(ui, page.x, page.y, page.w, h1, tr("saves.title"),
                                             11.0f * sc, tr("saves.filterModes"));

    /* 筛选胶囊：高度随字号自适应，超宽自动换行 */
    float fpx    = 11.5f * sc;
    float pill_h = ui_ctl_height(28.0f * z, fpx);
    float fy     = head_bottom + 10.0f * z;
    const char *filters[3] = { tr("saves.allModes"), tr("mode.leisure"), tr("mode.survival") };
    int filter_vals[3] = {-1, PLAY_MODE_LEISURE, PLAY_MODE_SURVIVAL};
    float fx = page.x;
    for (int i = 0; i < 3; i++) {
        float w = (float)ui_text_width_px(ui, UI_FACE_BODY, fpx, false, filters[i]) + 24.0f * z;
        if (fx > page.x && fx + w > page.x + page.w) {
            fx = page.x;
            fy += pill_h + 10.0f * z;
        }
        bool act = (g_app.save_filter_mode == filter_vals[i]);
        bool hov = ui_hit(ui, fx, fy, w, pill_h);
        ui_draw_rect(ui, fx, fy, w, pill_h, act ? tk_of(ui, QL_PRIMARY_UI) : tk_of(ui, QL_SURFACE));
        ui_draw_border(ui, fx, fy, w, pill_h, 1.0f,
                       act ? tk_of(ui, QL_LINE_PRIMARY) : tk_of(ui, QL_LINE_SOFT));
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, fpx, false, fx + 4.0f,
                              fy + (pill_h - fpx * 1.35f) * 0.5f, w - 8.0f, 1, filters[i],
                              act ? tk_of(ui, QL_ON_PRIMARY) : (hov ? tk_of(ui, QL_INK) : tk_of(ui, QL_INK_2)));
        ui_draw_circle(ui, fx + w * 0.5f, fy + pill_h + 4.0f * z, 2.2f * z,
                       act ? tk_of(ui, QL_PRIMARY_UI) : with_alpha(tk_of(ui, QL_INK_3), 0.55f));
        if (hov && ui->mouse_clicked) {
            g_app.save_filter_mode = filter_vals[i];
            g_app.save_page = 0;
            g_app.save_expanded_id[0] = '\0';
            save_rename_end(ui);
        }
        fx += w + 8.0f * z;
    }
    /* 刷新目录胶囊 */
    {
        const char *s_ref = tr("saves.refresh");
        float w = (float)ui_text_width_px(ui, UI_FACE_BODY, fpx, false, s_ref) + 24.0f * z;
        if (fx > page.x && fx + w > page.x + page.w) {
            fx = page.x;
            fy += pill_h + 10.0f * z;
        }
        bool hov = ui_hit(ui, fx, fy, w, pill_h);
        ui_draw_rect(ui, fx, fy, w, pill_h, tk_of(ui, QL_SURFACE));
        ui_draw_border(ui, fx, fy, w, pill_h, 1.0f, tk_of(ui, QL_LINE_SOFT));
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, fpx, false, fx + 4.0f,
                              fy + (pill_h - fpx * 1.35f) * 0.5f, w - 8.0f, 1,
                              s_ref, hov ? tk_of(ui, QL_INK) : tk_of(ui, QL_INK_2));
        ui_draw_circle(ui, fx + w * 0.5f, fy + pill_h + 4.0f * z, 2.2f * z,
                       with_alpha(tk_of(ui, QL_INK_3), 0.55f));
        if (hov && ui->mouse_clicked) {
            g_app.save_expanded_id[0] = '\0';
            save_rename_end(ui);
            app_refresh_saves();
            g_app.save_page = 0;
        }
    }

    float y = fy + pill_h + 12.0f * z;
    /* 底部让出一行给翻页条 */
    float pg_h = ui_ctl_height(26.0f * z, 11.0f * sc);
    float pg_y = page.y + page.h - 10.0f * z - pg_h;
    float list_bottom = pg_y - 10.0f * z;

    if (list_bottom > y + 20.0f) {
        ui_draw_dashed_border(ui, page.x, y, page.w, list_bottom - y,
                              1.0f, 5.0f * z, 4.0f * z,
                              with_alpha(tk_of(ui, QL_PRIMARY_UI), 0.42f));
    }

    float bh     = ui_ctl_height(28.0f * z, fpx);
    float card_w = page.w - 16.0f * z;
    float inner_w = card_w - 26.0f * z;
    float gap_btn = 7.0f * z;
    float row_h  = ui_ctl_height(30.0f * z, 11.0f * sc);

    const char *s_exp    = tr("saves.exportBtn");
    const char *s_ren    = tr("saves.rename");
    const char *s_conv   = tr("saves.convertLeisure");
    const char *s_ok     = tr("saves.renameConfirm");
    const char *s_cancel = tr("saves.renameCancel");

    float bw_exp  = (float)ui_text_width_px(ui, UI_FACE_BODY, fpx, false, s_exp)  + 22.0f * z;
    float bw_ren  = (float)ui_text_width_px(ui, UI_FACE_BODY, fpx, false, s_ren)  + 22.0f * z;
    float bw_conv = (float)ui_text_width_px(ui, UI_FACE_BODY, fpx, false, s_conv) + 22.0f * z;
    float bw_ok   = (float)ui_text_width_px(ui, UI_FACE_BODY, fpx, false, s_ok)   + 22.0f * z;
    float bw_ccl  = (float)ui_text_width_px(ui, UI_FACE_BODY, fpx, false, s_cancel) + 22.0f * z;

    /* 三按钮折行时按两行上界估高 */
    float ops_w = bw_exp + bw_ren + bw_conv + gap_btn * 2.0f;
    bool wrap_ops = ops_w > (inner_w - 150.0f * z);
    float ops_h = wrap_ops ? (bh * 2.0f + 6.0f * z) : bh;
    float head_h = file_head_height(ui, ops_h);

    /* 先按模式过滤 */
    int idx[MAX_SAVE_SLOTS];
    float heights[MAX_SAVE_SLOTS];
    int starts[MAX_SAVE_SLOTS];
    int ends[MAX_SAVE_SLOTS];
    int total = 0;
    int open = -1;
    for (int i = 0; i < g_app.save_count; i++) {
        const SaveFileInfo *f = &g_app.saves[i];
        if (!f->valid) continue;
        if (g_app.save_filter_mode != -1 && (int)f->play_mode != g_app.save_filter_mode) continue;
        idx[total] = i;
        if (g_app.save_expanded_id[0] && strcmp(g_app.save_expanded_id, f->id) == 0)
            open = total;
        heights[total] = head_h;
        total++;
    }
    float avail = list_bottom - y;
    int pages = 0;
    int first = 0, last = 0;
    if (total > 0) {
        pages = paginate(heights, total, avail, 10.0f * z, starts, ends, MAX_SAVE_SLOTS);
        if (pages <= 0) pages = 1;
        if (g_app.save_page >= pages) g_app.save_page = pages - 1;
        if (g_app.save_page < 0) g_app.save_page = 0;
        first = starts[g_app.save_page];
        last  = ends[g_app.save_page];
    } else {
        pages = 1;
        g_app.save_page = 0;
    }
    if (open < first || open >= last) {
        open = -1;
        g_app.save_expanded_id[0] = '\0';
    }

    /* 固定分页，卡片共用剩余展开空间 */
    float compact_h = 13.5f * sc * 1.62f + 14.0f * z;
    float header_h[MAX_SAVE_SLOTS], body_h[MAX_SAVE_SLOTS], compact_t[MAX_SAVE_SLOTS];
    float used = qlcg_maxf(0.0f, (float)(last - first - 1)) * 10.0f * z;
    float bodies = 0.0f;
    for (int k = 0; k < total; k++) {
        const SaveFileInfo *f = &g_app.saves[idx[k]];
        uint32_t id = (qlcg_hash_str(f->id) ^ 0x5A7E1300u) | 0x80000000u;
        UiMotion *m = ui_motion_step(ui, id, open >= 0 && k != open, false, k == open);
        if (k < first || k >= last) {
            m->hover = m->toggle = 0.0f;
            continue;
        }
        if (m->hover < 0.001f) m->hover = 0.0f;
        if (m->hover > 0.999f) m->hover = 1.0f;
        if (m->toggle < 0.001f) m->toggle = 0.0f;
        if (m->toggle > 0.999f) m->toggle = 1.0f;
        compact_t[k] = m->hover;
        header_h[k] = head_h + (compact_h - head_h) * m->hover;
        body_h[k] = (file_card_height(ui, true, head_h) - head_h) * m->toggle;
        used += header_h[k];
        bodies += body_h[k];
    }
    float body_scale = bodies > 0.0f ? qlcg_clampf((avail - used) / bodies, 0.0f, 1.0f) : 0.0f;
    int shown = 0;

    for (int k = first; k < last; k++) {
        SaveFileInfo *f = &g_app.saves[idx[k]];
        bool expanded = k == open;
        bool compact = open >= 0 && !expanded;
        bool editing = (g_app.save_edit_id[0] != '\0') && strcmp(g_app.save_edit_id, f->id) == 0;
        bool surv = (f->play_mode == PLAY_MODE_SURVIVAL);
        float hh = header_h[k];
        float body = body_h[k] * body_scale;
        float ch = hh + body;
        float card_x = page.x + 8.0f * z;
        bool hov = ui_hit(ui, card_x, y, card_w, ch);
        ui_neu(ui, card_x, y, card_w, ch, 6.0f * z, tk_of(ui, QL_NEU_DARK), tk_of(ui, QL_NEU_LIGHT));
        ui_draw_rect(ui, card_x, y, card_w, ch, tk_of(ui, QL_SURFACE));
        ui_draw_border(ui, card_x, y, card_w, ch, 1.0f,
                       hov ? tk_of(ui, QL_LINE_PRIMARY) : tk_of(ui, QL_LINE_FAINT));

        /* 折叠箭头 */
        float icon = 12.0f * z;
        draw_chevron(ui, card_x + 9.0f * z + icon * 0.5f, y + hh * 0.5f, icon, expanded,
                     expanded ? tk_of(ui, QL_PRIMARY_UI) : tk_of(ui, QL_INK_3));
        float ix = card_x + 9.0f * z + icon + 8.0f * z;
        float iy = y + 11.0f * z;
        float summary_avail = inner_w - (surv ? ops_w : ops_w - bw_conv - gap_btn) - 8.0f * z;
        float text_avail = qlcg_maxf(120.0f * z, summary_avail);
        bool btn_click = false;
        int file_action = -1;

        if (editing) {
            /* 改名态：输入框与保存取消 */
            float field_w = qlcg_maxf(120.0f * z, inner_w - bw_ok - bw_ccl - gap_btn - 8.0f * z);
            bool commit = ui->input_commit;
            ui_text_input(ui, SAVE_RENAME_INPUT_ID, ix, iy, field_w, bh,
                          g_app.save_edit_buf, sizeof(g_app.save_edit_buf), 80,
                          tr("saves.newName"));
            if (commit && ui->input_id != SAVE_RENAME_INPUT_ID) {
                if (app_save_rename(f, g_app.save_edit_buf)) {
                    save_rename_end(ui);
                    return;   /* 已重扫，本帧不再画旧列表 */
                }
            }
            float by = y + (head_h - bh) * 0.5f;
            float bx = card_x + card_w - 13.0f * z - bw_ok;
            if (ui_btn(ui, bx, by, bw_ok, bh, s_ok, BTN_PRIMARY, false, fpx, false)) {
                if (app_save_rename(f, g_app.save_edit_buf)) {
                    save_rename_end(ui);
                    return;
                }
            }
            bx -= bw_ccl + gap_btn;
            if (ui_btn(ui, bx, by, bw_ccl, bh, s_cancel, BTN_GHOST, false, fpx, false)) {
                save_rename_end(ui);
            }
            btn_click = true;
        } else {
            float name_px = 13.5f * sc;
            float detail_t = 1.0f - compact_t[k];
            float saved_alpha = ui->global_alpha;
            float name_w = qlcg_maxf(40.0f, text_avail * 0.62f);
            name_w += (card_x + card_w - 13.0f * z - ix - name_w) * compact_t[k];
            iy += ((compact_h - name_px * 1.62f) * 0.5f - 11.0f * z) * compact_t[k];
            ui_clip_push(ui, card_x, y, card_w, hh);
            ui_draw_text_ellipsis(ui, UI_FACE_DISPLAY, name_px, false, ix, iy,
                                  name_w, 0,
                                  f->world_name, tk_of(ui, QL_INK));
            ui->global_alpha *= detail_t;
            float nw = qlcg_minf(text_avail * 0.62f,
                                 (float)ui_text_width_px(ui, UI_FACE_DISPLAY, name_px, false,
                                                         f->world_name));
            float nx = ix + nw + 7.0f * z;
            const char *tag = surv ? tr("mode.survival") : tr("mode.leisure");
            float tag_px = 10.5f * sc;
            float meta_px = 10.0f * sc;
            float tag_w = (float)ui_text_width_px(ui, UI_FACE_BODY, tag_px, false, tag) + 10.0f * z;
            if (nx + tag_w <= ix + text_avail) {
                ui_draw_border(ui, nx, iy + 2.0f * z, tag_w, tag_px + 6.0f * z, 1.0f,
                               with_alpha(tk_of(ui, QL_ACCENT), 0.40f));
                ui_draw_text_px_center(ui, UI_FACE_BODY, tag_px, false, nx + tag_w * 0.5f,
                                       iy + 4.0f * z, tag, tk_of(ui, QL_ACCENT_TEXT));

                char dimensions[160];
                snprintf(dimensions, sizeof(dimensions), tr("saves.worldSizeFmt"),
                         qlcg_macro_size_label(f->macro_size_idx), f->chunks_per_cell);
                float dx = nx + tag_w + 9.0f * z;
                ui_draw_text_ellipsis(ui, UI_FACE_BODY, meta_px, false, dx,
                                      iy + 4.0f * z + (tag_px - meta_px) * 0.675f,
                                      ix + summary_avail - dx, 0, dimensions, tk_of(ui, QL_INK_3));
            }

            char meta[192];
            char cnt[32];
            snprintf(cnt, sizeof(cnt), tr("saves.slotCountFmt"), f->present_count);
            snprintf(meta, sizeof(meta), "%s %u · %s · %s · %s", tr("creator.seedShort"),
                     f->seed, qlcg_calendar_mode_name(f->calendar_mode), cnt,
                     f->saved_at_str);
            ui_draw_text_ellipsis(ui, UI_FACE_BODY, meta_px, false, ix,
                                  iy + 13.5f * sc * 1.62f + 3.0f * z, text_avail, 0,
                                  meta, tk_of(ui, QL_INK_3));

            /* 按钮右起排，超出折行 */
            float bx = card_x + card_w - 13.0f * z;
            float by = y + (head_h - ops_h) * 0.5f;
            for (int oi = 0; !compact && detail_t > 0.001f && oi < 3; oi++) {
                if (oi == 2 && !surv) break;
                const char *lbl;
                float bwv;
                if (oi == 0)      { lbl = s_exp;  bwv = bw_exp;  }
                else if (oi == 1) { lbl = s_ren;  bwv = bw_ren;  }
                else              { lbl = s_conv; bwv = bw_conv; }
                if (bx - bwv < card_x + 21.0f * z && bx < card_x + card_w - 13.0f * z) {
                    bx = card_x + card_w - 13.0f * z;
                    by += bh + 6.0f * z;
                }
                bx -= bwv;
                bool blocked = ui->input_blocked;
                ui->input_blocked = blocked || compact_t[k] > 0.01f;
                bool clicked = ui_btn(ui, bx, by, bwv, bh, lbl, BTN_GHOST, false, fpx, false);
                ui->input_blocked = blocked;
                if (clicked) {
                    btn_click = true;
                    file_action = oi;
                }
                bx -= gap_btn;
            }
            ui_clip_pop(ui);
            ui->global_alpha = saved_alpha;
        }

        if (file_action == 0) app_export_save_file(f);
        else if (file_action == 1) {
            if (save_file_legacy(f))
                app_push_toast_k(UI_TOAST_WARNING, NULL, tr("saves.err.legacy"));
            else save_rename_begin(ui, f);
        } else if (file_action == 2) {
            app_save_copy_leisure(f, f->newest_slot);
            return;
        }

        if (body > 10.0f * z && !editing) {
            float sy = y + hh + 2.0f * z;
            float view_h = body - 10.0f * z;
            bool scroll = expanded && view_h >= row_h;
            float off = 0.0f;
            if (scroll) off = ui_scroll_begin(ui, SAVE_SLOTS_SCROLL_ID, card_x + 8.0f * z,
                                            sy, card_w - 16.0f * z, view_h);
            else ui_clip_push(ui, card_x + 8.0f * z, sy, card_w - 16.0f * z, view_h);
            float top = sy;
            sy -= off;
            int load_slot = -1;
            bool blocked = ui->input_blocked;
            ui->input_blocked = blocked || !expanded;
            for (int s = 0; s < QLCG_SAVE_SLOT_COUNT; s++) {
                bool hit = draw_slot_row(ui, f, s, card_x + 8.0f * z, sy,
                                         card_w - 16.0f * z, row_h, 11.5f * sc);
                if (hit) load_slot = s;
                sy += row_h + 4.0f * z;
            }
            ui->input_blocked = blocked;
            if (scroll) ui_scroll_end(ui, SAVE_SLOTS_SCROLL_ID, sy + off - top);
            else ui_clip_pop(ui);
            if (load_slot >= 0) {
                app_load_from_save_file(f, load_slot);
                return;
            }
        }

        /* 点头部展开/收起，按钮区不触发 */
        if (!editing && !btn_click && ui->mouse_clicked &&
            ui_hit(ui, card_x, y, compact ? card_w : card_w - ops_w - 24.0f * z, hh)) {
            save_rename_end(ui);
            ui_scroll_reset(ui, SAVE_SLOTS_SCROLL_ID);
            if (expanded) g_app.save_expanded_id[0] = '\0';
            else ui_u8_copy(g_app.save_expanded_id, sizeof(g_app.save_expanded_id), f->id);
        }

        y += ch + 10.0f * z;
        shown++;
    }

    if (total == 0) {
        float ex = page.x + page.w * 0.5f;
        float ey = (y + list_bottom) * 0.5f - (18.0f * z + 12.0f * sc);
        {
            const SpriteDef *flower = ui_sprite_by_name("decor-flower");
            if (flower) {
                ui_sprite_icon(ui, flower, ex - 17.0f * z, ey, 34.0f * z,
                               ui_rgba(255, 255, 255, 150));
            }
        }
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, 12.0f * sc, false, page.x + 8.0f,
                              ey + 40.0f * z, page.w - 16.0f, 1,
                              tr("saves.emptyTitle"), tk_of(ui, QL_INK_2));
        ui_draw_text_ellipsis(ui, UI_FACE_BODY, 11.0f * sc, false, page.x + 8.0f,
                              ey + 40.0f * z + 12.0f * sc * 1.7f, page.w - 16.0f, 1,
                              tr("saves.emptyHint"), tk_of(ui, QL_INK_3));
    } else if (shown == 0) {
        g_app.save_page = 0;
    }

    /* 翻页条：‹ 当前/总数 ›，一页放得下就不显示 */
    if (pages > 1) {
        float aw  = bh * 1.25f;
        float cy  = pg_y + (pg_h - bh) * 0.5f;
        float mid = page.x + page.w * 0.5f;
        float gap = 34.0f * z;
        if (ui_btn(ui, mid - gap - aw, cy, aw, bh, "<", BTN_GHOST, false, fpx, false) &&
            g_app.save_page > 0) {
            g_app.save_page--;
            g_app.save_expanded_id[0] = '\0';
            save_rename_end(ui);
        }
        char pinfo[32];
        snprintf(pinfo, sizeof(pinfo), "%d / %d", g_app.save_page + 1, pages);
        ui_draw_text_px_center(ui, UI_FACE_BODY, fpx, false, mid,
                               cy + (bh - fpx * 1.35f) * 0.5f, pinfo, tk_of(ui, QL_INK_2));
        if (ui_btn(ui, mid + gap, cy, aw, bh, ">", BTN_GHOST, false, fpx, false) &&
            g_app.save_page + 1 < pages) {
            g_app.save_page++;
            g_app.save_expanded_id[0] = '\0';
            save_rename_end(ui);
        }
    }
}
