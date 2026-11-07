#ifndef QLCG_UI_LAYOUT_H
#define QLCG_UI_LAYOUT_H

#include "ui_core.h"

/* 仿 CSS 流式/弹性盒布局器 */
typedef struct {
    UiContext *ui;
    float      x;
    float      y;
    float      w;
    float      cursor;
    float      indent;
    float      label_ratio;
    float      row_gap;
} UiLayout;

typedef struct {
    float x, y, w, h;
    float label_x, label_w;
    float ctl_x, ctl_w;
    float text_px;
    float inner_h;
} UiRow;

/* 自适应高度卡片容器（延迟发射背景顶点） */
typedef struct {
    float x, y, w;
    float pad_x, pad_y;
    float min_h;
    float saved_x, saved_w, saved_indent;
    int   v_mark;
} UiCard;

void  ui_layout_begin(UiLayout *L, UiContext *ui, float x, float y, float w);
float ui_layout_end(UiLayout *L);

void  ui_layout_gap(UiLayout *L, float h);
void  ui_layout_divider(UiLayout *L);
float ui_layout_note(UiLayout *L, const char *text, bool dim);
float ui_layout_banner(UiLayout *L, const char *text);

/* 卡片容器：内容画完后自动撑开高度并回填背景 */
void  ui_card_begin(UiLayout *L, UiCard *c, float pad_x, float pad_y, float min_h);
void  ui_card_end(UiLayout *L, UiCard *c, float blur, uint32_t bg, uint32_t border, uint32_t shadow,
                  float *out_x, float *out_y, float *out_w, float *out_h);

/* 表单行：支持固定比例或按最小控件宽自动折行 */
UiRow ui_row(UiLayout *L, float want_h);
UiRow ui_row_auto(UiLayout *L, const char *label, bool dim, float min_ctl_w, float want_ctl_h);
void  ui_row_label(const UiLayout *L, const UiRow *r, const char *text, bool dim);

/* flex-wrap 分段按钮与胶囊组，返回索引或 -1 */
int   ui_flex_segments(UiContext *ui, float x, float *io_y, float w, float base_h, float gap,
                       const char *const *labels, int count, int active_idx, float font_px);
int   ui_flex_pills(UiContext *ui, float x, float *io_y, float w, float base_h, float gap,
                    const char *const *labels, int count, int active_idx, bool grow);
int   ui_layout_segments(UiLayout *L, const char *const *labels, int count, int active_idx, float font_px);
int   ui_layout_labeled_segments(UiLayout *L, const char *label, const char *const *items,
                                 int count, int active_idx);

/* 常用复合行：勾选、下拉、滑块、只读信息、颜色编辑 */
bool  ui_layout_checkbox(UiLayout *L, const char *label, bool *val);
bool  ui_layout_row_checkbox(UiLayout *L, const char *row_label, const char *box_label, bool *val);
bool  ui_layout_select(UiLayout *L, uint32_t id, const char *label,
                       const char *const *items, int count, int current);
bool  ui_layout_height_input(UiLayout *L, uint32_t id, const char *label,
                             MeasurementSystem sys, int *height_cm, int min_cm, int max_cm);
bool  ui_layout_number_input(UiLayout *L, uint32_t id, const char *label,
                             float *value, float min_value, float max_value, float step,
                             int decimals, const char *suffix);
bool  ui_layout_text_input(UiLayout *L, uint32_t id, const char *label,
                           char *buf, size_t cap, uint32_t max_chars,
                           const char *placeholder);

/* 控件贴右：返回实际 (x, w)，不足时靠右 */
void  ui_row_fit_right(const UiRow *r, float want_w, float *out_x, float *out_w);

/* 准心样例：展示五种样式，点击返回新值或 -1 */
int   ui_layout_crosshair(UiLayout *L, const char *label, int current);
bool  ui_layout_slider(UiLayout *L, const char *label, float *val,
                       float vmin, float vmax, const char *fmt);
bool  ui_layout_slider_steps(UiLayout *L, const char *label, float *val,
                             float vmin, float vmax, int steps, float limit_max,
                             const char *const *step_labels, const char *fmt);
void  ui_layout_kv(UiLayout *L, const char *label, const char *value);
bool  ui_layout_color(UiLayout *L, const char *title, uint32_t *color);

/* 折叠组与缩进 */
bool  ui_layout_fold(UiLayout *L, uint32_t id, const char *title, bool *expanded);
void  ui_layout_indent_push(UiLayout *L);
void  ui_layout_indent_pop(UiLayout *L);

/* 页头左右分栏（超宽自动换行）并返回底线下方 y */
float ui_layout_split_head(UiContext *ui, float x, float y, float w,
                           float title_px, const char *title,
                           float sub_px, const char *subtitle);

#endif /* QLCG_UI_LAYOUT_H */
