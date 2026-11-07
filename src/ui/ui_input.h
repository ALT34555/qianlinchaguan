/* 单行编辑内核：光标、选区、剪贴板、UTF-8 */
#ifndef QLCG_UI_INPUT_H
#define QLCG_UI_INPUT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* 单行编辑状态：字节下标，恒在码点边界 */
typedef struct {
    uint32_t caret;      /* 光标字节下标 */
    uint32_t anchor;     /* 选区锚点；与 caret 相等表示无选区 */
    float    scroll;     /* 水平滚动像素 */
    float    hold;       /* 击键后光标常亮余量（秒） */
    bool     dragging;   /* 鼠标拖选中 */
} UiEdit;

/* 编辑目标：缓冲、容量(含结尾0)、码点上限 */
typedef struct {
    char    *buf;
    size_t   cap;
    uint32_t max_chars;
    UiEdit  *ed;
} UiEditField;

/* ---- UTF-8 边界工具 ---- */
size_t   ui_u8_len(const char *s);
uint32_t ui_u8_next(const char *s, uint32_t i);
uint32_t ui_u8_prev(const char *s, uint32_t i);
uint32_t ui_u8_count(const char *s);
uint32_t ui_u8_floor(const char *s, uint32_t i);
int      ui_u8_step(const char *p);
/* 截断到完整码点，绝不留下残缺尾序列 */
void     ui_u8_copy(char *dst, size_t cap, const char *src);
/* 就地去掉末尾残缺序列 */
void     ui_u8_trim(char *s);

/* -- 剪贴板钩子 app_clipboard.c -- */
bool ui_clip_get_utf8(char *dst, size_t cap);
bool ui_clip_set_utf8(const char *utf8);

/* ---- 编辑原语：返回是否改动了文本 ---- */
void     ui_edit_reset(UiEditField *f);
bool     ui_edit_has_sel(const UiEditField *f);
void     ui_edit_sel(const UiEditField *f, uint32_t *a, uint32_t *b);
void     ui_edit_touch(UiEditField *f);
bool     ui_edit_insert_cp(UiEditField *f, uint32_t cp);
bool     ui_edit_insert_utf8(UiEditField *f, const char *text);
bool     ui_edit_backspace(UiEditField *f);
bool     ui_edit_delete_forward(UiEditField *f);
bool     ui_edit_move(UiEditField *f, int dir, bool extend, bool by_word);
bool     ui_edit_home(UiEditField *f, bool extend);
bool     ui_edit_end(UiEditField *f, bool extend);
bool     ui_edit_select_all(UiEditField *f);
bool     ui_edit_copy(const UiEditField *f);
bool     ui_edit_cut(UiEditField *f);
bool     ui_edit_paste(UiEditField *f);

/* ---- 单行版式：逐字形累加度量 ---- */

struct UiContext;

/* 按缓冲重建度量（同帧重复调用命中缓存） */
void     ui_text_metrics(struct UiContext *ui, int face, float px, bool bold, const char *buf);
float    ui_text_metrics_x(uint32_t byte_idx);
uint32_t ui_text_metrics_at_x(float rel_x);
float    ui_text_metrics_total(void);

/* 画一行可编辑文本；空缓冲返回 false */
bool     ui_edit_draw_line(struct UiContext *ui, UiEditField *f, float x, float y,
                           float tw, float h, int face, float px, bool bold,
                           bool focused, bool active,
                           uint32_t col_text, uint32_t col_ph,
                           uint32_t col_sel, uint32_t col_caret,
                           const char *placeholder);

/* 鼠标 x → 最近码点边界；滚动量内部处理 */
uint32_t ui_edit_hit_line(struct UiContext *ui, const UiEditField *f, float tw,
                          int face, float px, bool bold, float rel_x);

#endif /* QLCG_UI_INPUT_H */
