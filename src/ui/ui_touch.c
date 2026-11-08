/* 触屏虚拟键：左摇杆 + 右三动作键，对齐 base */
#include "ui_touch.h"
#include "../app/app_state.h"
#include "ui_sprite.h"
#include "ui_widgets.h"
#include "../core/math_util.h"
#include "../core/game_settings.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 冲刺沿用 base 的 dodge 字形 */
static const char *BTN_ICON[UI_TOUCH_BTN_COUNT] = {
    "dodge", "jump", "crouch"
};

/* 8 样式对齐 base；6/7 走 dpad 分支 */
static const char *STYLE_ID[8] = {
    "jxqy", "bagua", "luopan", "lotus", "minimal", "classic", "cross", "octagon"
};
static const char *STYLE_BASE[8] = {
    "m-joy-base", "joy-base-bagua", "joy-base-luopan", "joy-base-lotus",
    "joy-base-minimal", "stick-base", "dpad-cross-whole", "dpad-cross-whole"
};
static const char *STYLE_KNOB[8] = {
    "m-joy-knob", "joy-knob-seal", "joy-knob-jade", "joy-knob-sword",
    "joy-knob-ink", "stick-knob", "joy-knob-ink", "joy-knob-ink"
};

static int touch_style(void) {
    int st = g_app.settings.joystick_style;
    return (st >= 0 && st < 8) ? st : 0;
}

void ui_touch_reset(UiTouch *t) {
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->opacity = 1.0f;
    t->stick_pointer = -1;
    t->look_pointer = -1;
    t->style = -1;
    for (int i = 0; i < UI_TOUCH_BTN_COUNT; i++) t->btn_pointer[i] = -1;
}

static void touch_release_stick(UiTouch *t, bool preserve_lock) {
    if (!preserve_lock) t->sprint_locked = false;
    t->stick_pointer = -1;
    t->forward_hold = 0.0f;
    t->knob_dx = 0.0f;
    t->knob_dy = t->sprint_locked ? -t->stick_r * 0.92f : 0.0f;
    t->axis_x = 0;
    t->axis_y = t->sprint_locked ? -1 : 0;
}

/* 同 base：(可用宽高-控件边) 百分比定左上 */
void ui_touch_begin(UiTouch *t, UiContext *ui, bool enabled, float opacity,
                    const float lay[4][3]) {
    if (!t) return;
    if (!enabled) {
        ui_touch_reset(t);
        return;
    }
    int style = touch_style();
    if (t->style != style) ui_touch_reset(t);
    t->enabled = true;
    t->style = style;
    t->opacity = qlcg_clampf(opacity, 0.1f, 1.0f);

    float def[4][3];
    if (!lay) { qlcg_touch_layout_default(def); lay = def; }

    float z  = ui->met.size;
    float sw = ui->win_w - ui->safe_edges[0] - ui->safe_edges[2];
    float sh = ui->win_h - ui->safe_edges[1] - ui->safe_edges[3];
    float ox = ui->safe_edges[0];
    float oy = ui->safe_edges[1];

    /* 触屏控件跟 UI 缩放，但设上限：过大时反而互相压住 */
    float stick_box = qlcg_clampf(168.0f * z, 150.0f, 210.0f) * lay[UI_TOUCH_ITEM_MOVE][2];
#ifdef __ANDROID__
    stick_box=fmaxf(stick_box,150*ui->device_density);
#endif
    t->stick_r  = stick_box * 0.5f;
    t->stick_cx = ox + lay[UI_TOUCH_ITEM_MOVE][0] * (sw - stick_box) + t->stick_r;
    t->stick_cy = oy + lay[UI_TOUCH_ITEM_MOVE][1] * (sh - stick_box) + t->stick_r;

    float btn_box0 = qlcg_clampf(62.0f * z, 56.0f, 78.0f);
#ifdef __ANDROID__
    btn_box0=fmaxf(btn_box0,48*ui->device_density);
#endif
    for (int i = 0; i < UI_TOUCH_BTN_COUNT; i++) {
        int item = UI_TOUCH_ITEM_SPRINT + i;
        float btn_box = btn_box0 * lay[item][2];
        t->btn_r = btn_box * 0.5f;
        t->btn_cx[i] = ox + lay[item][0] * (sw - btn_box) + t->btn_r;
        t->btn_cy[i] = oy + lay[item][1] * (sh - btn_box) + t->btn_r;
    }
    t->btn_r = btn_box0 * 0.5f;   /* 命中半径取基准，避免逐键跳动 */

    if (style >= 6 && t->stick_pointer != -1 && t->axis_x == 0 && t->axis_y < 0) {
        t->forward_hold += ui->dt;
        if (t->forward_hold >= 0.65f) t->sprint_locked = true;
    } else {
        t->forward_hold = 0.0f;
    }
    if (t->sprint_locked && t->stick_pointer == -1) touch_release_stick(t, true);
}

/* 摇杆方向：只在超出死区时给出，按轴向拆分 */
static void touch_apply_stick(UiTouch *t) {
    float dead = t->stick_r * 0.22f;
    t->axis_x = (t->knob_dx >  dead) ?  1 : (t->knob_dx < -dead ? -1 : 0);
    t->axis_y = (t->knob_dy >  dead) ?  1 : (t->knob_dy < -dead ? -1 : 0);
}

/* 把摇杆头限制在半径内，同时记录视觉偏移 */
static void touch_track_stick(UiTouch *t, float x, float y) {
    float dx = x - t->stick_cx;
    float dy = y - t->stick_cy;
    float d  = sqrtf(dx * dx + dy * dy);
    float lim = t->stick_r * 0.92f;
    if (d > lim && d > 0.0001f) {
        dx = dx / d * lim;
        dy = dy / d * lim;
    }
    t->knob_dx = dx;
    t->knob_dy = dy;
    touch_apply_stick(t);
    if (t->style < 6) {
        t->sprint_locked = dy < -lim * 0.82f && fabsf(dx) < lim * 0.45f;
    } else if (t->axis_x != 0 || t->axis_y >= 0) {
        t->sprint_locked = false;
        t->forward_hold = 0.0f;
    }
}

bool ui_touch_down(UiTouch *t, UiContext *ui, int pointer, float x, float y) {
    (void)ui;
    if (!t || !t->enabled) return false;

    float badge = t->stick_r * 0.38f;
    float badge_x = x - t->stick_cx;
    float badge_y = y - (t->stick_cy - t->stick_r - badge * 0.10f);
    float badge_r = badge * 0.75f;
    if (t->style < 6 && badge_x * badge_x + badge_y * badge_y <= badge_r * badge_r) {
        if (t->sprint_locked) touch_release_stick(t, false);
        return true;
    }

    /* 摇杆：以中心为圆心，判定半径放宽到外圈 */
    float dx = x - t->stick_cx;
    float dy = y - t->stick_cy;
    float reach = t->stick_r * 1.25f;
    if (dx * dx + dy * dy <= reach * reach) {
        if (t->sprint_locked) {
            touch_release_stick(t, false);
            return true;
        }
        if (t->stick_pointer != -1) return true;
        t->forward_hold = 0.0f;
        t->stick_pointer = pointer;
        touch_track_stick(t, x, y);
        return true;
    }

    /* 动作键 */
    float hit_r = t->btn_r * 1.15f;
    for (int i = 0; i < UI_TOUCH_BTN_COUNT; i++) {
        float bx = x - t->btn_cx[i];
        float by = y - t->btn_cy[i];
        if (bx * bx + by * by <= hit_r * hit_r) {
            if (t->btn_pointer[i] != -1) return true;
            if (i == 0 && t->sprint_locked) {
                touch_release_stick(t, false);
                return true;
            }
            t->btn_pointer[i] = pointer;
            t->btn_down[i]    = true;
            return true;
        }
    }

    /* 其余区域留作视角拖动 */
    if (pointer == UI_TOUCH_MOUSE_POINTER || t->look_pointer != -1) return false;
    t->look_pointer = pointer;
    t->look_last_x = x;
    t->look_last_y = y;
    return false;
}

void ui_touch_move(UiTouch *t, UiContext *ui, int pointer, float x, float y) {
    (void)ui;
    if (!t || !t->enabled) return;
    if (pointer == t->stick_pointer) {
        touch_track_stick(t, x, y);
        return;
    }
    if (pointer == t->look_pointer) {
        /* 从按下位置开始累计位移 */
        t->look_dx += x - t->look_last_x;
        t->look_dy += y - t->look_last_y;
        t->look_last_x = x;
        t->look_last_y = y;
    }
}

void ui_touch_up(UiTouch *t, UiContext *ui, int pointer) {
    (void)ui;
    if (!t) return;
    if (pointer == t->stick_pointer) {
        touch_release_stick(t, true);
    }
    for (int i = 0; i < UI_TOUCH_BTN_COUNT; i++) {
        if (pointer == t->btn_pointer[i]) {
            t->btn_pointer[i] = -1;
            t->btn_down[i]    = false;
        }
    }
    if (pointer == t->look_pointer) {
        t->look_pointer = -1;
    }
}

/* 单层 sprite：按原色还原四层 */
static void touch_sprite(UiContext *ui, const char *name, float x, float y, float size) {
    const SpriteDef *sp = ui_sprite_by_name(name);
    if (!sp) return;
    ui_sprite4(ui, sp, x, y, size, size,
               tk_of(ui, QL_SURFACE),
               tk_of(ui, QL_PRIMARY),
               tk_of(ui, QL_ACCENT));
}

/* 按下方向时缩到九成，与 base 一致 */
static float touch_dir_scale(const UiTouch *t, int dx, int dy) {
    return (t->axis_x == dx && t->axis_y == dy) ? 0.90f : 1.0f;
}

/* 整框旋转：方向键只用原样层 */
static void touch_sprite_rot(UiContext *ui, const char *name, float cx, float cy,
                             float size, float rad) {
    const SpriteDef *sp = ui_sprite_by_name(name);
    if (!sp) return;
    ui_use_sprite_tex(ui, sp->tex);
    float fw = (float)sp->tex_w, fh = (float)sp->tex_h;
    float ax = (float)sp->layer_x[3], ay = (float)sp->layer_y[3];
    ui_draw_sprite_rect_rot(ui, cx, cy, size, size, rad,
                            ax / fw, ay / fh,
                            (ax + sp->w) / fw, (ay + sp->h) / fh,
                            0xFFFFFFFFu);
}

/* 八向箭头：单张素材旋转，角与边各一张 */
static void touch_dpad_arrows(UiTouch *t, UiContext *ui, const char *edge,
                              float ox, float oy, float cell, float step) {
    const float PI = 3.1415927f;
    for (int i = 0; i < 9; i++) {
        if (i == 4) continue;
        int dx = i % 3 - 1;
        int dy = i / 3 - 1;
        bool corner = (dx != 0 && dy != 0);
        float px = ox + (float)(i % 3) * step + cell * 0.5f;
        float py = oy + (float)(i / 3) * step + cell * 0.5f;
        float k  = touch_dir_scale(t, dx, dy);
        if (t->sprint_locked && dx == 0 && dy == -1)
            ui_draw_border(ui, px - cell * 0.5f, py - cell * 0.5f,
                           cell, cell, 2.0f, tk_of(ui, QL_PRIMARY));
        float base = corner ? -0.75f * PI : -0.5f * PI;
        float rad  = atan2f((float)dy, (float)dx) - base;
        touch_sprite_rot(ui, corner ? "dpad-up-left" : edge,
                         px, py, cell * k, rad);
    }
}

/* 十字向：底板 88% 居中，四正四斜按 base 比例 */
static void touch_dpad_cross(UiTouch *t, UiContext *ui, float x, float y, float box) {
    touch_sprite(ui, "dpad-cross-whole", x + box * 0.06f, y + box * 0.06f, box * 0.88f);
    touch_dpad_arrows(t, ui, "dpad-up", x + box * 0.0238f, y + box * 0.0238f,
                      box * 0.2857f, box * 0.33335f);
}

/* 八角向：3x3 网格，箭头与中心摇杆头 */
static void touch_dpad_oct(UiTouch *t, UiContext *ui, float x, float y, float box) {
    float g   = box * 0.92f;
    float ox  = x + (box - g) * 0.5f;
    float oy  = y + (box - g) * 0.5f;
    float pad = g * 0.024f;
    float gap = g * 0.024f;
    float cell = (g - pad * 2.0f - gap * 2.0f) / 3.0f;
    ui_draw_rect(ui, ox, oy, g, g, ui_rgba(248, 244, 237, 166));
    ui_draw_border(ui, ox, oy, g, g, 2.0f, ui_rgba(77, 64, 48, 255));
    touch_dpad_arrows(t, ui, "dpad-flow-n", ox + pad, oy + pad, cell, cell + gap);
    float knob = cell * 0.74f;
    touch_sprite(ui, "joy-knob-ink",
                 ox + pad + cell + gap + (cell - knob) * 0.5f,
                 oy + pad + cell + gap + (cell - knob) * 0.5f, knob);
}

void ui_touch_draw(UiTouch *t, UiContext *ui) {
    if (!t || !t->enabled) return;
    float save = ui->global_alpha;
    ui->global_alpha *= t->opacity;

    float box = t->stick_r * 2.0f;
    float bx  = t->stick_cx - box * 0.5f;
    float by  = t->stick_cy - box * 0.5f;

    int st = touch_style();
    if (st == 6) {
        touch_dpad_cross(t, ui, bx, by, box);
    } else if (st == 7) {
        touch_dpad_oct(t, ui, bx, by, box);
    } else {
        /* 摇杆底座 */
        touch_sprite(ui, STYLE_BASE[st], bx, by, box);
        /* 冲刺徽章：底座上方 19% */
        float badge = box * 0.19f;
        if (t->sprint_locked)
            ui_draw_circle(ui, t->stick_cx, by - badge * 0.10f,
                           badge * 0.65f, tk_of(ui, QL_PRIMARY_SOFT));
        touch_sprite(ui, "joy-sprint",
                     t->stick_cx - badge * 0.5f,
                     by - badge * 0.60f, badge);
        /* 摇杆头：底座 40% */
        float knob = box * 0.40f;
        touch_sprite(ui, STYLE_KNOB[st],
                     t->stick_cx + t->knob_dx - knob * 0.5f,
                     t->stick_cy + t->knob_dy - knob * 0.5f, knob);
    }

    /* 动作键 */
    float btn_box = t->btn_r * 2.0f;
    for (int i = 0; i < UI_TOUCH_BTN_COUNT; i++) {
        float scale = t->btn_down[i] ? 0.90f : 1.0f;
        float s = btn_box * scale;
        float cx = t->btn_cx[i];
        float cy = t->btn_cy[i];
        char ap[64], bi[64];
        snprintf(ap, sizeof(ap), "action-plate-%s", STYLE_ID[st]);
        snprintf(bi, sizeof(bi), "btn-%s-%s", BTN_ICON[i], STYLE_ID[st]);
        touch_sprite(ui, ap, cx - s * 0.5f, cy - s * 0.5f, s);
        float icon = s * 0.64f;
        touch_sprite(ui, bi, cx - icon * 0.5f, cy - icon * 0.5f, icon);
    }

    ui->global_alpha = save;
}
