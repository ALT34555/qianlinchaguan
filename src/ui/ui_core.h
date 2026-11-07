#ifndef QLCG_UI_CORE_H
#define QLCG_UI_CORE_H

#include "../core/config.h"
#include "../terrain3d/gl_loader.h"
#include "ui_theme.h"
#include "ui_input.h"
#include <stddef.h>

#define UI_ATLAS_W      2048
#define UI_ATLAS_H      2048
#define UI_MAX_VERTS    65536
#define UI_CACHE_SLOTS  512
#define UI_PETAL_COUNT  24

typedef enum {
    FONT_TITLE = 0,
    FONT_SUBTITLE,
    FONT_BODY,
    FONT_BOLD,
    FONT_SMALL,
    FONT_MONO,
    FONT_COUNT
} UiFontId;

/* 字体族：0 宋体标题，1 系统正文，2 等宽 */
typedef enum {
    UI_FACE_DISPLAY = 0,
    UI_FACE_BODY    = 1,
    UI_FACE_MONO    = 2
} UiFaceKind;

typedef struct {
    HFONT font;
    int   face;
    int   px;
    bool  bold;
} UiFontCache;

#define UI_FONT_CACHE 48

typedef struct {
    float    x, y;
    float    u, v;
    uint32_t color;
    float    mode;
} UiVertex;

typedef struct {
    bool     used;
    uint32_t hash;
    int      face;
    int      px;
    bool     bold;
    char     text[256];
    int      ax, ay, aw, ah;
    float    u0, v0, u1, v1;
    uint32_t last_frame;
} UiTextSlot;

/* 茶席飘落花瓣粒子，对齐 base */
typedef struct {
    float x, y;
    float vx, vy;
    float size;
    float rot, vrot;
    float alpha;
} TeaPetal;

/* 点击水波：620ms 扩散 */
typedef struct {
    bool  active;
    float x, y;
    float t;
    float radius;
} UiRipple;

/* 控件过渡状态：悬停/按下的平滑值 */
typedef struct {
    uint32_t id;
    float    hover;
    float    press;
    float    toggle;
} UiMotion;

#define UI_RIPPLE_SLOTS 12
#define UI_MOTION_SLOTS 192

/* 输入框模式：数字或文本 */
#define UI_INPUT_NUM    0
#define UI_INPUT_TEXT   1

/* 茶席配色主题（对齐日间/夜间模式） */
typedef struct {
    uint32_t bg_canvas;
    uint32_t bg_rail;
    uint32_t bg_card;
    uint32_t bg_card_hover;
    uint32_t bg_panel_glass;
    uint32_t border_soft;
    uint32_t border_gold;
    uint32_t text_main;
    uint32_t text_sub;
    uint32_t text_muted;
    uint32_t accent_tea;
    uint32_t accent_gold;
    uint32_t accent_crimson;
} UiPalette;

typedef struct UiContext {
    int        win_w, win_h;   /* UI 逻辑尺寸；物理尺寸/hidpi_mul */
    int        pix_w, pix_h;   /* 物理像素尺寸，背景 blit 用 */
    float      hidpi_mul;      /* 当前生效的 UI 放大倍率 */
    uint32_t   frame_idx;
    ThemeMode  theme;
    int        night_override;  /* 生效昼夜覆盖：-1 不覆盖 / 0 日 / 1 夜 */
    UiPalette  pal;

    /* 动态配色令牌与尺寸令牌 */
    QlTokens   tk;
    QlTokens   tk_from;      /* 主题过渡起点 */
    QlTokens   tk_to;        /* 主题过渡终点 */
    float      theme_t;      /* 过渡进度 0..1 */
    bool       tk_ready;
    QlMetrics  met;
    QlColor    primary;
    QlColor    accent;
    bool       use_profile_palette;

    /* 鼠标与键盘瞬时状态 */
    int        mouse_x, mouse_y;
    bool       mouse_down;
    bool       mouse_clicked;
    bool       right_down;
    bool       right_clicked;
    bool       mouse_dblclicked;    /* 本帧内判定为双击 */
    int        wheel_delta;
    bool       input_blocked;    /* 当前绘制层禁用输入 */
    bool       select_input;     /* 下拉自身输入作用域 */
    bool       toast_blocked;    /* 通知占用当前指针 */
    double     last_click_ms;       /* 双击判定用：上次左键时间与位置 */
    int        last_click_x, last_click_y;

    /* 触摸点：任何一根手指在屏上的位置，与虚拟摇杆无关 */
    bool       touch_active;
    float      touch_x, touch_y;

    /* 共享纹理与 GDI 字形烘焙 */
    GLuint     prog;
    GLuint     vao, vbo;
    GLuint     atlas_tex;
    GLuint     cur_sprite_tex;  /* 本批 sprite 所用纹理，0 表示尚无 */

    /* 背景磨砂：降采样后可分离高斯两趟 */
    GLuint     bd_fbo[2], bd_tex[2];   /* ping-pong 的 1/4 尺寸缓冲 */
    GLuint     bd_prog, bd_vao;
    GLuint     backdrop_tex;           /* 模糊结果，玻璃面板读它 */
    int        bd_w, bd_h;
    bool       backdrop_ready;         /* 本帧已抓取，玻璃直接采样 */
    HDC        mem_dc;
    HBITMAP    dib;
    uint32_t  *dib_bits;
    HFONT      fonts[FONT_COUNT];
    UiFontCache dyn[UI_FONT_CACHE];
    int        dyn_count;
    wchar_t    family_display[64];   /* 标题字体族，可被 mod 替换 */
    wchar_t    family_body[64];      /* 正文字体族 */
    wchar_t    family_mono[64];      /* 等宽字体族 */

    int        shelf_x, shelf_y, shelf_row_h;
    UiTextSlot slots[UI_CACHE_SLOTS];

    /* 茶杯图标在字形图集中的 UV */
    bool       has_teacup;
    float      cup_u0, cup_v0, cup_u1, cup_v1;
    int        cup_w, cup_h;

    /* 飘落花瓣粒子 */
    TeaPetal   petals[UI_PETAL_COUNT];

    /* 动效内核：时间、全局透明度与位移、水波纹 */
    float      time;
    float      dt;
    double     last_ms;
    float      global_alpha;
    float      text_fade_top, text_fade_bottom;
    float      global_dx, global_dy;
    float      page_enter;      /* 入场进度；默认 1 表示无动效 */
    float      fixed_dt;        /* >0 时用固定帧步长（截图模式） */
    float      user_scale;      /* 文字缩放，<=0 视为 1 */
    float      user_size;       /* 控件缩放，<=0 视为 1 */
    float      safe_x_pct;      /* 左右安全区百分比，0 表示无 */
    float      safe_y_pct;      /* 上下安全区百分比 */
    float      safe_x_px;       /* 每帧换算出的像素值 */
    float      safe_y_px;
    bool       reduce_motion;   /* 关闭过渡与水波 */
    int        capture_action;  /* >=0 表示正在等待按键绑定 */

    /* 下拉选择器：展开态跨帧保持，浮层在页面末尾绘制 */
    uint32_t   select_id;       /* 0 表示未展开 */
    uint32_t   select_result_id; /* 上一次选中的下拉 id，供调用方分发 */

    /* 滑块拖动：命中区锁定在按下那一刻 */
    int        slider_seq;      /* 本帧第几个滑块 */
    int        drag_slider_id;  /* 正在拖的滑块序号，-1 表示无 */
    float      drag_sx, drag_sy, drag_sw;
    uint32_t   select_frame;    /* 展开时的帧号，避免同帧被误关 */
    float      select_x, select_y, select_w;
    int        select_count, select_current;
#ifdef QLCG_DEV_TOOLS
    int        debug_open_select;  /* 强制展开指定下拉 */
#endif
    char       select_labels[12][64];
    UiRipple   ripples[UI_RIPPLE_SLOTS];
    UiMotion   motions[UI_MOTION_SLOTS];

    UiVertex   verts[UI_MAX_VERTS];
    int        vert_count;

    /* 裁剪栈：ui_clip_push/pop 可嵌套 */
    int        clip_depth;
    float      clip_stack[8][4];

    /* 输入框聚焦状态：数字与文本共用一套焦点/提交状态机 */
    uint32_t   input_id;
    uint32_t   input_focus_frame;
    int        input_sys;
    int        input_mode;      /* UI_INPUT_NUM 或 TEXT */
    size_t     input_max_bytes; /* 文本模式字节上限，0 表示不限 */
    uint32_t   input_max_chars; /* 文本模式码点上限，0 表示不限 */
    uint32_t   input_utf16_hi;  /* 代理对高位暂存 */
    char       input_buf[512];  /* UTF-8 */
    bool       input_dirty;
    bool       input_commit;
    UiEdit     edit_field;      /* 字段光标/选区 */
    UiEdit     edit_chat;       /* 聊天条光标/选区 */
    uint32_t   input_seen_frame; /* 聚焦字段最近绘制帧，用于自动失焦 */
    uint32_t   chat_seen_frame;  /* 聊天条最近绘制帧 */

    /* 输入法定位：上帧光标矩形（UI 逻辑坐标） */
    bool       ime_caret_valid;
    float      ime_caret_x, ime_caret_y, ime_caret_h;
} UiContext;

static inline uint32_t ui_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static inline uint32_t ui_rgb_hex(uint32_t hex, uint8_t a) {
    uint8_t r = (uint8_t)((hex >> 16) & 0xFF);
    uint8_t g = (uint8_t)((hex >> 8) & 0xFF);
    uint8_t b = (uint8_t)(hex & 0xFF);
    return ui_rgba(r, g, b, a);
}

/* 0xRRGGBBAA 常量转绘制色 */
static inline uint32_t ui_rgba_hex(uint32_t hex, uint8_t a) {
    return ui_rgba((uint8_t)((hex >> 24) & 0xFF), (uint8_t)((hex >> 16) & 0xFF),
                   (uint8_t)((hex >> 8) & 0xFF), a);
}

bool ui_init(UiContext *ui, int w, int h);
void ui_shutdown(UiContext *ui);
void ui_apply_theme(UiContext *ui, ThemeMode mode);
/* 昼夜覆盖：仅 THEME_AUTO 生效，-1 系统 */
void ui_set_night_override(UiContext *ui, int v);
void ui_set_palette(UiContext *ui, uint32_t primary_hex, uint32_t accent_hex);
void ui_begin_frame(UiContext *ui, int w, int h);
void ui_end_frame(UiContext *ui);
void ui_flush(UiContext *ui);

void ui_draw_rect(UiContext *ui, float x, float y, float w, float h, uint32_t color);
void ui_draw_rect_grad(UiContext *ui, float x, float y, float w, float h, uint32_t c_top, uint32_t c_bot);
/* 径向晕影：中心 50% 透明，向外渐深 */
void ui_draw_rect_radial(UiContext *ui, float x, float y, float w, float h, uint32_t color);
void ui_draw_border(UiContext *ui, float x, float y, float w, float h, float thick, uint32_t color);
void ui_draw_line(UiContext *ui, float x0, float y0, float x1, float y1, float thick, uint32_t color);
void ui_draw_circle(UiContext *ui, float cx, float cy, float r, uint32_t color);
/* 凸多边形（三角扇），用于书签形等非矩形 */
void ui_draw_poly(UiContext *ui, const float *xs, const float *ys, int n, uint32_t color);

/* 画字形图集内的任意矩形（图片元素） */
void ui_draw_atlas_rect(UiContext *ui, float x, float y, float w, float h,
                        float u0, float v0, float u1, float v1, uint32_t tint);

/* 画当前 sprite 纹理内的矩形（九宫格专用） */
void ui_draw_sprite_rect(UiContext *ui, float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1, uint32_t tint);

/* 同上，绕中心旋转 rad 弧度；正值顺时针 */
void ui_draw_sprite_rect_rot(UiContext *ui, float cx, float cy, float w, float h,
                             float rad, float u0, float v0, float u1, float v1,
                             uint32_t tint);

/* 切换 sprite 纹理；不同则先提交批次 */
void ui_use_sprite_tex(UiContext *ui, GLuint tex);

/* PNG 原尺寸上传为纹理，失败返回 false */
bool ui_load_png_texture(const wchar_t *path, GLuint *tex, int *out_w, int *out_h);

/* -- 背景磨砂：ui_backdrop.c -- */
void ui_backdrop_init(UiContext *ui);
void ui_backdrop_resize(UiContext *ui, int win_w, int win_h);
/* 抓后台缓冲并模糊；须在 UI 提交前调用 */
void ui_backdrop_capture(UiContext *ui);
/* 501：模糊整屏场景后盖回，UI 清晰 */
void ui_backdrop_blur_overlay(UiContext *ui, float radius_px);
void ui_backdrop_end_frame(UiContext *ui);
void ui_backdrop_release(UiContext *ui);
/* 磨砂玻璃矩形；无模糊结果时退化为半透明 */
void ui_draw_rect_glass(UiContext *ui, float x, float y, float w, float h, uint32_t tint);

/* 切换字体族并重建烘焙缓存；传 NULL 的项保持不动 */
void ui_set_font_family(UiContext *ui, const wchar_t *display,
                        const wchar_t *body, const wchar_t *mono);

/* 按像素宽度折行测量或绘制，返回行数 */
int ui_measure_text_wrapped(UiContext *ui, UiFaceKind face, float px, bool bold,
                            float max_w, const char *text);
int ui_draw_text_wrapped(UiContext *ui, UiFaceKind face, float px, bool bold,
                         float x, float y, float max_w, float line_h,
                         const char *text, uint32_t color);
/* 超宽单行文本自动省略号（align: 0左 1中 2右） */
void ui_draw_text_ellipsis(UiContext *ui, UiFaceKind face, float px, bool bold,
                           float x, float y, float max_w, int align,
                           const char *text, uint32_t color);
void ui_draw_teacup(UiContext *ui, float x, float y, float w, float h, float anim_t);
void ui_update_and_draw_petals(UiContext *ui, float dt, float area_x, float area_y,
                               float area_w, float area_h);

int  ui_text_width(UiContext *ui, UiFontId font, const char *text);
void ui_draw_text(UiContext *ui, UiFontId font, float x, float y, const char *text, uint32_t color);
void ui_draw_text_center(UiContext *ui, UiFontId font, float cx, float y, const char *text, uint32_t color);
void ui_draw_text_right(UiContext *ui, UiFontId font, float rx, float y, const char *text, uint32_t color);

/* 任意字号绘制（按 --q-scale 缩放） */
int  ui_text_width_px(UiContext *ui, UiFaceKind face, float px, bool bold, const char *text);
void ui_draw_text_px(UiContext *ui, UiFaceKind face, float px, bool bold, float x, float y, const char *text, uint32_t color);
void ui_draw_text_px_center(UiContext *ui, UiFaceKind face, float px, bool bold, float cx, float y, const char *text, uint32_t color);
void ui_draw_text_px_right(UiContext *ui, UiFaceKind face, float px, bool bold, float rx, float y, const char *text, uint32_t color);
float ui_line_height(UiFaceKind face, float px);

/* 按钮形态；对齐 base btn--primary 等 */
typedef enum {
    BTN_PRIMARY = 0,
    BTN_GHOST,
    BTN_QUIET,
    BTN_DANGER,
    BTN_SEGMENT,      /* 分段选项：未选中，透明底 + 细边 */
    BTN_SEGMENT_ON    /* 分段选项：选中，主色软底 + 主色边，不位移 */
} UiButtonKind;

/* 动效内核 */
float ui_ease_out(float t);
float ui_ease(float t);
float ui_ease_spring(float t);
UiMotion *ui_motion(UiContext *ui, uint32_t id);
UiMotion *ui_motion_step(UiContext *ui, uint32_t id, bool hovered, bool pressed, bool toggled);
void ui_ripple_push(UiContext *ui, float x, float y, float radius);
void ui_ripple_draw(UiContext *ui, uint32_t color);
void ui_page_enter(UiContext *ui);

/* 组件声明见 ui_widgets.h */

#endif /* QLCG_UI_CORE_H */
