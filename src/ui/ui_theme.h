#ifndef QLCG_UI_THEME_H
#define QLCG_UI_THEME_H

#include <stdint.h>
#include <stdbool.h>

/* 动态配色：对齐 base theme.ts */
typedef struct { float r, g, b, a; } QlColor;

/* 颜色令牌（顺序与 base tokens.csv 一致） */
typedef enum {
    QL_BG = 0,
    QL_BG_DEEP,
    QL_SURFACE,
    QL_SURFACE_HI,
    QL_SURFACE_LO,
    QL_SURFACE_GLASS,
    QL_INK,
    QL_INK_2,
    QL_INK_3,
    QL_INK_INV,
    QL_LINE,
    QL_LINE_SOFT,
    QL_LINE_FAINT,
    QL_LINE_PRIMARY,
    QL_PRIMARY,
    QL_PRIMARY_UI,
    QL_PRIMARY_TEXT,
    QL_PRIMARY_SOFT,
    QL_PRIMARY_DEEP,
    QL_ON_PRIMARY,
    QL_ACCENT,
    QL_ACCENT_UI,
    QL_ACCENT_TEXT,
    QL_ACCENT_SOFT,
    QL_HIGHLIGHT,
    QL_HIGHLIGHT_UI,
    QL_HIGHLIGHT_SOFT,
    QL_TEA,
    QL_TEA_TEXT,
    QL_GOLD,
    QL_CELADON,
    QL_WOOD,
    QL_PETAL,
    QL_NEU_LIGHT,
    QL_NEU_DARK,
    QL_AERO_TINT,
    QL_AERO_LINE,
    QL_SHADOW_SM,
    QL_SHADOW,
    QL_SHADOW_LG,
    QL_GLOW,
    QL_TOKEN_COUNT
} QlToken;

typedef struct {
    QlColor c[QL_TOKEN_COUNT];
    bool    dark;
    QlColor primary;
    QlColor accent;
} QlTokens;

/* 尺寸令牌：对齐 base 的 --q-size 与间距 */
typedef struct {
    float scale;
    float size;
    float ctl_h, ctl_h_sm, ctl_h_lg;
    float pad_card, pad_sheet, rhythm, gap_grid, tab_h;
    float sp[9];
} QlMetrics;

QlColor  ql_rgb(uint32_t hex);
QlColor  ql_rgba_f(uint32_t hex, float a);
QlColor  ql_mix(QlColor a, float wa, QlColor b, float wb);
QlColor  ql_alpha(QlColor c, float a);
QlColor  ql_lighten(QlColor c, float t);
QlColor  ql_darken(QlColor c, float t);
float    ql_luminance(QlColor c);
float    ql_contrast(QlColor a, QlColor b);
int      ql_to_rgba(QlColor c);
uint32_t ql_to_hex(QlColor c);
/* 前景色按给定底色的对比度自动压深/提亮，返回可用色 */
QlColor  ql_readable_on(QlColor color, const QlColor *bgs, int count, QlColor anchor);

void ql_tokens_build(QlTokens *t, bool dark, QlColor primary, QlColor accent);
void ql_metrics_build(QlMetrics *m, int win_w, int win_h, float user_scale, float user_size, float hidpi);

#endif /* QLCG_UI_THEME_H */
