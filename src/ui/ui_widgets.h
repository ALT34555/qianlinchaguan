#ifndef QLCG_UI_WIDGETS_H
#define QLCG_UI_WIDGETS_H

#include "ui_core.h"
#include "../core/math_util.h"

/* 令牌取色 */
static inline uint32_t tk_of(const UiContext *ui, QlToken id) {
    return (uint32_t)ql_to_rgba(ui->tk.c[id]);
}

/* 改透明度 */
static inline uint32_t with_alpha(uint32_t color, float a) {
    uint32_t av = (uint32_t)(qlcg_clampf(a, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (color & 0x00FFFFFFu) | (av << 24);
}

/* 阴影与浮雕 */
void ui_shadow_offset(UiContext *ui, float x, float y, float w, float h, float blur, float dx, float dy, uint32_t color);
void ui_shadow_soft(UiContext *ui, float x, float y, float w, float h, float blur, float dy, uint32_t color);
void ui_neu(UiContext *ui, float x, float y, float w, float h, float blur, uint32_t dark, uint32_t light);
/* 内凹阴影：裁进矩形内部，用于输入框 */
void ui_neu_in(UiContext *ui, float x, float y, float w, float h, float blur, uint32_t dark, uint32_t light);
void ui_panel(UiContext *ui, float x, float y, float w, float h, float blur, uint32_t bg, uint32_t border, uint32_t shadow);
void ui_edge_light(UiContext *ui, float x, float y, float w, float h, uint32_t light, uint32_t dark);

/* 虚线边框：四边按 dash/gap 交替 */
void ui_draw_dashed_border(UiContext *ui, float x, float y, float w, float h,
                           float thick, float dash, float gap, uint32_t color);

/* 裁剪区 */
void ui_clip_push(UiContext *ui, float x, float y, float w, float h);
void ui_clip_pop(UiContext *ui);

/* 组件高度测算（随字号自动撑高） */
float ui_ctl_height(float base_h, float font_px);
float ui_checkbox_height(const UiContext *ui);
float ui_select_height(const UiContext *ui, float base_h);

/* 组件 */
bool ui_hit(const UiContext *ui, float x, float y, float w, float h);
bool ui_pointer_in_rect(const UiContext *ui, float x, float y, float w, float h);
bool ui_pointer_blocked(const UiContext *ui);
void ui_pointer_consume(UiContext *ui);
bool ui_btn(UiContext *ui, float x, float y, float w, float h, const char *label,
            UiButtonKind kind, bool align_left, float font_px, bool bold);
bool ui_button(UiContext *ui, float x, float y, float w, float h, const char *label, bool primary, bool active);
bool ui_pill(UiContext *ui, float x, float y, float w, float h, const char *label, bool active);
bool ui_checkbox(UiContext *ui, float x, float y, const char *label, bool *val);
/* 标签+数值+轨道，轨道随标签行高下移 */
bool ui_slider_float(UiContext *ui, float x, float y, float w, const char *label,
                     float *val, float vmin, float vmax, const char *fmt_val);
/* 只画轨道与滑钮，ty 为轨道顶边；带拖动锁 */
bool ui_slider_at(UiContext *ui, float x, float ty, float w, float *val, float vmin, float vmax);
bool ui_slider_at_ex(UiContext *ui, float x, float ty, float w, float *val,
                     float vmin, float vmax, int steps, float limit_max,
                     const char *const *step_labels);
bool ui_slider_float_ex(UiContext *ui, float x, float y, float w, const char *label,
                        float *val, float vmin, float vmax, int steps, float limit_max,
                        const char *const *step_labels, const char *fmt_val);
/* 一个完整滑块占的高度，供流程布局推进 */
float ui_slider_height(UiContext *ui);
bool ui_fold_header(UiContext *ui, float x, float y, float w, const char *title, bool *expanded);
void ui_draw_crosshair(UiContext *ui, CrosshairStyle style);

/* 下拉：ui_select 收起态，popup 画浮层 */
bool ui_select(UiContext *ui, uint32_t id, float x, float y, float w, float h,
               const char *const *items, int count, int current);
/* 指定字号版：px 只作用于收起态标签 */
bool ui_select_sized(UiContext *ui, uint32_t id, float x, float y, float w, float h,
                     const char *const *items, int count, int current, float px);
int  ui_select_popup(UiContext *ui);

/* 身高输入框：键盘+步进，按度量衡换算 */
bool ui_height_input(UiContext *ui, uint32_t id, float x, float y, float w, float h,
                     MeasurementSystem sys, int *height_cm, int min_cm, int max_cm);

/* 浮点数字输入框：键盘+步进，带范围和小数位 */
bool ui_number_input(UiContext *ui, uint32_t id, float x, float y, float w, float h,
                     float *value, float min_value, float max_value, float step,
                     int decimals, const char *suffix);

/* 文本输入框：UTF-8，光标/选区/剪贴板 */
bool ui_text_input(UiContext *ui, uint32_t id, float x, float y, float w, float h,
                   char *buf, size_t cap, uint32_t max_chars, const char *placeholder);

/* 准心形状：设置页预览与游戏内共用 */
void ui_crosshair_glyph(UiContext *ui, float cx, float cy, float size, int kind, uint32_t solid);

/* 空状态：开口朝下虚线半圆，圆心 (cx,cy) */
void ui_empty_arc(UiContext *ui, float cx, float cy, float r, uint32_t color);

/* 书签：frame-tab-bookmark.svg */
void ui_bookmark(UiContext *ui, float x, float y, float w, float h,
                 uint32_t wood, uint32_t wood_deep, uint32_t paper, uint32_t accent);

/* 横向书签：side.svg，尖角与绳珠朝右 */
void ui_bookmark_side(UiContext *ui, float x, float y, float w, float h,
                      uint32_t wood, uint32_t wood_deep, uint32_t paper, uint32_t accent);

/* 书签页签模板：三态，axis 选抽轴 */
typedef enum {
    UI_TAB_AXIS_Y = 0,   /* 从框体上沿插入，向上抽出 */
    UI_TAB_AXIS_X        /* 从框体侧沿插入，向外侧抽出 */
} UiTabAxis;

typedef struct {
    uint32_t    id;       /* 过渡状态指纹，同一按钮多次调用需稳定 */
    const char *label;
    int         icon;     /* 0 房子 / 1 书 / 2 齿轮 / -1 无图标 */
} UiTabItem;

/* 选中态延后到框体之后绘制 */
typedef struct {
    bool        valid;
    UiTabItem   item;
    float       x, y, w, h;   /* 槽 */
    float       overhang;     /* 伸出框体（尖角+流苏）的长度 */
    float       slip;         /* 抽出进度 0..1，1=完全抽出 */
    UiTabAxis   axis;
    float       font_px;
} UiTabPending;

#endif /* QLCG_UI_WIDGETS_H */
