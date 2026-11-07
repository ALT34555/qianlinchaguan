/* OKLab 混合配色，对齐 base theme.ts */
#include "ui_theme.h"
#include "../core/math_util.h"
#include <math.h>

static const QlColor QL_TRANSPARENT = {0.0f, 0.0f, 0.0f, 0.0f};

static float srgb_to_linear(float c) {
    if (c <= 0.04045f) return c / 12.92f;
    return powf((c + 0.055f) / 1.055f, 2.4f);
}

static float linear_to_srgb(float c) {
    if (c <= 0.0031308f) return c * 12.92f;
    return 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

QlColor ql_rgb(uint32_t hex) {
    QlColor c;
    c.r = (float)((hex >> 16) & 0xFF) / 255.0f;
    c.g = (float)((hex >> 8) & 0xFF) / 255.0f;
    c.b = (float)(hex & 0xFF) / 255.0f;
    c.a = 1.0f;
    return c;
}

QlColor ql_rgba_f(uint32_t hex, float a) {
    QlColor c = ql_rgb(hex);
    c.a = qlcg_clampf(a, 0.0f, 1.0f);
    return c;
}

QlColor ql_alpha(QlColor c, float a) {
    c.a = qlcg_clampf(a, 0.0f, 1.0f);
    return c;
}

/* sRGB → OKLab */
static void to_oklab(QlColor c, float out[3]) {
    float r = srgb_to_linear(c.r);
    float g = srgb_to_linear(c.g);
    float b = srgb_to_linear(c.b);
    float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;
    float l_ = cbrtf(l < 0.0f ? 0.0f : l);
    float m_ = cbrtf(m < 0.0f ? 0.0f : m);
    float s_ = cbrtf(s < 0.0f ? 0.0f : s);
    out[0] = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
    out[1] = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
    out[2] = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;
}

/* OKLab → sRGB */
static QlColor from_oklab(const float lab[3], float a) {
    float l_ = lab[0] + 0.3963377774f * lab[1] + 0.2158037573f * lab[2];
    float m_ = lab[0] - 0.1055613458f * lab[1] - 0.0638541728f * lab[2];
    float s_ = lab[0] - 0.0894841775f * lab[1] - 1.2914855480f * lab[2];
    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;
    float r =  4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    float g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    float b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
    QlColor out;
    out.r = qlcg_clampf(linear_to_srgb(r), 0.0f, 1.0f);
    out.g = qlcg_clampf(linear_to_srgb(g), 0.0f, 1.0f);
    out.b = qlcg_clampf(linear_to_srgb(b), 0.0f, 1.0f);
    out.a = qlcg_clampf(a, 0.0f, 1.0f);
    return out;
}

/* color-mix：alpha 预乘后插值 */
QlColor ql_mix(QlColor a, float wa, QlColor b, float wb) {
    float sum = wa + wb;
    if (sum <= 1e-6f) return QL_TRANSPARENT;
    wa /= sum;
    wb /= sum;
    float alpha = a.a * wa + b.a * wb;
    if (alpha <= 1e-5f) return QL_TRANSPARENT;
    float pa = a.a * wa;
    float pb = b.a * wb;
    if (pb <= 1e-6f) {
        QlColor out = {a.r, a.g, a.b, alpha};
        return out;
    }
    if (pa <= 1e-6f) {
        QlColor out = {b.r, b.g, b.b, alpha};
        return out;
    }
    float la[3], lb[3];
    to_oklab(a, la);
    to_oklab(b, lb);
    float t = pb / (pa + pb);
    float lab[3];
    lab[0] = qlcg_mixf(la[0], lb[0], t);
    lab[1] = qlcg_mixf(la[1], lb[1], t);
    lab[2] = qlcg_mixf(la[2], lb[2], t);
    return from_oklab(lab, alpha);
}

QlColor ql_lighten(QlColor c, float t) {
    return ql_mix(c, 1.0f - t, ql_rgb(0xFFFFFF), t);
}

QlColor ql_darken(QlColor c, float t) {
    return ql_mix(c, 1.0f - t, ql_rgb(0x000000), t);
}

/* WCAG 相对亮度 */
float ql_luminance(QlColor c) {
    return 0.2126f * srgb_to_linear(c.r) + 0.7152f * srgb_to_linear(c.g) + 0.0722f * srgb_to_linear(c.b);
}

float ql_contrast(QlColor a, QlColor b) {
    float la = ql_luminance(a) + 0.05f;
    float lb = ql_luminance(b) + 0.05f;
    return la > lb ? la / lb : lb / la;
}

int ql_to_rgba(QlColor c) {
    int r = (int)(qlcg_clampf(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
    int g = (int)(qlcg_clampf(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
    int b = (int)(qlcg_clampf(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
    int a = (int)(qlcg_clampf(c.a, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (a << 24) | (b << 16) | (g << 8) | r;
}

uint32_t ql_to_hex(QlColor c) {
    uint32_t r = (uint32_t)(qlcg_clampf(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
    uint32_t g = (uint32_t)(qlcg_clampf(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
    uint32_t b = (uint32_t)(qlcg_clampf(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (r << 16) | (g << 8) | b;
}

/* 主色上的文字：白 / 灰 / 黑 按对比度自动挑 */
static QlColor pick_on_primary(QlColor bg) {
    QlColor paper = ql_rgb(0xFBECDE);
    QlColor slate = ql_rgb(0x5E616D);
    QlColor ink   = ql_rgb(0x1D1B1C);
    QlColor cand[3] = {paper, slate, ink};
    float ratio[3];
    int best = 0, second = -1;
    for (int i = 0; i < 3; i++) {
        ratio[i] = ql_contrast(bg, cand[i]);
        if (ratio[i] > ratio[best]) best = i;
    }
    for (int i = 0; i < 3; i++) {
        if (i == best) continue;
        if (second < 0 || ratio[i] > ratio[second]) second = i;
    }
    if (second >= 0 && cand[second].r == slate.r && cand[second].g == slate.g &&
        cand[second].b == slate.b && ratio[second] >= 4.5f) {
        return slate;
    }
    return cand[best];
}

/* 文字色不达底限时往墨色方向压 */
static QlColor darken_until(QlColor color, QlColor bg, QlColor anchor, float floor_ratio) {
    if (ql_contrast(color, bg) >= floor_ratio) return color;
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 14; i++) {
        float mid = (lo + hi) * 0.5f;
        if (ql_contrast(ql_mix(color, 1.0f - mid, anchor, mid), bg) >= floor_ratio) hi = mid;
        else lo = mid;
    }
    return ql_mix(color, 1.0f - hi, anchor, hi);
}

/* 主色/装饰色作前景：按纸面对比度自动压深或提亮 */
QlColor ql_readable_on(QlColor color, const QlColor *bgs, int count, QlColor anchor) {
    bool ok = true;
    for (int i = 0; i < count; i++) {
        if (ql_contrast(color, bgs[i]) < 4.5f) { ok = false; break; }
    }
    if (ok) return color;
    for (int step = 1; step < 20; step++) {
        float t = (float)step / 20.0f;
        QlColor next = ql_mix(color, 1.0f - t, anchor, t);
        bool pass = true;
        for (int i = 0; i < count; i++) {
            if (ql_contrast(next, bgs[i]) < 4.5f) { pass = false; break; }
        }
        if (pass) return next;
    }
    return anchor;
}

/* 重算整张令牌表 */
void ql_tokens_build(QlTokens *t, bool dark, QlColor primary, QlColor accent) {
    QlColor white   = ql_rgb(0xFFFFFF);
    QlColor black   = ql_rgb(0x000000);
    QlColor bg      = ql_rgb(dark ? 0x1D1B1C : 0xFBECDE);
    QlColor paper   = ql_rgb(0xFDF3EA);
    QlColor deep_ink = ql_rgb(dark ? 0x000000 : 0x180D11);

    t->dark = dark;
    t->primary = primary;
    t->accent = accent;

    t->c[QL_BG] = bg;
    t->c[QL_BG_DEEP]  = dark ? ql_mix(bg, 0.78f, black, 0.22f) : ql_mix(bg, 0.88f, accent, 0.12f);
    QlColor surface  = dark ? ql_mix(bg, 0.93f, white, 0.07f) : ql_mix(bg, 0.58f, white, 0.42f);
    QlColor surface_hi = dark ? ql_mix(bg, 0.88f, white, 0.12f) : ql_mix(bg, 0.30f, white, 0.70f);
    QlColor surface_lo = dark ? ql_mix(bg, 0.82f, black, 0.18f) : ql_mix(bg, 0.88f, accent, 0.12f);
    t->c[QL_SURFACE]    = surface;
    t->c[QL_SURFACE_HI] = surface_hi;
    t->c[QL_SURFACE_LO] = surface_lo;
    t->c[QL_SURFACE_GLASS] = ql_alpha(surface, dark ? 0.74f : 0.72f);

    QlColor ink   = dark ? ql_mix(bg, 0.12f, paper, 0.88f) : ql_mix(accent, 0.68f, deep_ink, 0.32f);
    QlColor ink2  = dark ? ql_mix(bg, 0.34f, paper, 0.66f) : ql_mix(accent, 0.88f, bg, 0.12f);
    QlColor ink3  = dark ? ql_mix(bg, 0.52f, paper, 0.48f) : ql_mix(accent, 0.56f, bg, 0.44f);
    QlColor anchor_text = ql_rgb(dark ? 0xFBECDE : 0x1D1B1C);
    ink  = darken_until(ink,  bg, anchor_text, 7.0f);
    ink2 = darken_until(ink2, bg, anchor_text, 4.5f);
    ink3 = darken_until(ink3, bg, anchor_text, 2.6f);
    t->c[QL_INK]   = ink;
    t->c[QL_INK_2] = ink2;
    t->c[QL_INK_3] = ink3;
    t->c[QL_INK_INV] = ql_rgb(dark ? 0x241D1E : 0xFFF6EE);

    /* 描边由 accent_ui 派生，暗色提亮，不另设线色 */
    QlColor primary_ui = dark ? ql_mix(primary, 0.84f, white, 0.16f) : primary;
    QlColor accent_ui  = dark ? ql_mix(accent, 0.58f, ql_rgb(0xFBECDE), 0.42f) : accent;

    t->c[QL_LINE]        = ql_alpha(accent_ui, dark ? 0.30f : 0.34f);
    t->c[QL_LINE_SOFT]   = ql_alpha(accent_ui, dark ? 0.18f : 0.16f);
    t->c[QL_LINE_FAINT]  = ql_alpha(accent_ui, dark ? 0.10f : 0.09f);
    t->c[QL_LINE_PRIMARY] = ql_alpha(primary, dark ? 0.60f : 0.48f);
    t->c[QL_PRIMARY]       = primary;
    t->c[QL_PRIMARY_UI]    = primary_ui;
    t->c[QL_PRIMARY_SOFT]  = ql_alpha(primary, dark ? 0.20f : 0.10f);
    t->c[QL_PRIMARY_DEEP]  = dark ? ql_mix(primary, 0.88f, white, 0.12f) : ql_mix(primary, 0.76f, ql_rgb(0x2A0A12), 0.24f);
    t->c[QL_ON_PRIMARY]    = pick_on_primary(primary_ui);
    t->c[QL_ACCENT]        = accent;
    t->c[QL_ACCENT_UI]     = accent_ui;
    t->c[QL_ACCENT_SOFT]   = ql_alpha(accent, dark ? 0.16f : 0.08f);

    {
        QlColor primary_text = ql_mix(primary, dark ? 0.55f : 0.72f, ink, dark ? 0.45f : 0.28f);
        QlColor accent_text  = ql_mix(accent, dark ? 0.55f : 0.82f, ink, dark ? 0.45f : 0.18f);
        QlColor papers[5];
        int pc = 0;
        papers[pc++] = surface;
        papers[pc++] = surface_hi;
        papers[pc++] = bg;
        papers[pc++] = ql_mix(surface, 0.88f, primary, 0.12f);
        papers[pc++] = ql_mix(surface, 0.90f, accent, 0.10f);
        QlColor fg_anchor = ql_rgb(dark ? 0xFFFFFF : 0x1D1B1C);
        t->c[QL_PRIMARY_TEXT] = ql_readable_on(primary_text, papers, pc, fg_anchor);
        t->c[QL_ACCENT_TEXT]  = ql_readable_on(accent_text, papers, pc, fg_anchor);
    }

    t->c[QL_HIGHLIGHT]      = ql_rgb(0xC70039);
    t->c[QL_HIGHLIGHT_UI]   = dark ? ql_mix(ql_rgb(0xC70039), 0.86f, white, 0.14f) : ql_rgb(0xC70039);
    t->c[QL_HIGHLIGHT_SOFT] = ql_alpha(ql_rgb(0xC70039), dark ? 0.22f : 0.10f);

    /* 实物色：从茶盏上取，日间夜间都不变 */
    QlColor tea = ql_rgb(0x8FA97F);
    t->c[QL_TEA]      = tea;
    t->c[QL_TEA_TEXT] = ql_mix(tea, 0.50f, ink, 0.50f);
    t->c[QL_GOLD]     = ql_rgb(0xD9A94E);
    t->c[QL_CELADON]  = ql_rgb(0xA8C7C1);
    t->c[QL_WOOD]     = ql_rgb(0x4A2C1E);
    t->c[QL_PETAL]    = ql_rgb(0xF0A9B8);

    t->c[QL_NEU_LIGHT] = dark ? ql_mix(surface, 0.86f, white, 0.14f) : ql_mix(surface, 0.44f, white, 0.56f);
    t->c[QL_NEU_DARK]  = dark ? ql_mix(surface, 0.52f, black, 0.48f) : ql_mix(surface, 0.78f, accent, 0.22f);
    /* aero-tint 为固定纸色，非 surface 派生 */
    t->c[QL_AERO_TINT] = dark ? ql_rgba_f(0x2A2829, 0.58f) : ql_rgba_f(0xFDF4EC, 0.62f);
    t->c[QL_AERO_LINE] = ql_alpha(white, dark ? 0.26f : 0.78f);

    t->c[QL_SHADOW_SM] = dark ? ql_alpha(black, 0.40f) : ql_alpha(accent, 0.10f);
    t->c[QL_SHADOW]    = dark ? ql_alpha(black, 0.45f) : ql_alpha(accent, 0.13f);
    t->c[QL_SHADOW_LG] = dark ? ql_alpha(black, 0.60f) : ql_alpha(accent, 0.26f);
    t->c[QL_GLOW]      = ql_alpha(primary, dark ? 0.36f : 0.26f);
}

/* 尺寸令牌：随窗口宽取一档，再乘用户倍率 */
void ql_metrics_build(QlMetrics *m, int win_w, int win_h, float user_scale, float user_size, float hidpi) {
    (void)win_h;
    float base = 1.0f;
    if (win_w >= 1760) base = 1.08f;
    else if (win_w >= 1480) base = 1.04f;
    else if (win_w >= 1240) base = 1.02f;
    m->scale = base * (user_scale > 0.0f ? user_scale : 1.0f);
    /* 字号全额放大，控件只跟 1/4 幅度 */
    {
        float hd = (hidpi > 0.0f) ? hidpi : 1.0f;
        float us = (user_size  > 0.0f) ? user_size  : 1.0f;
        /* ut 先还原成用户原始文字倍率，避免 hidpi 被平方 */
        float ut = ((user_scale > 0.0f) ? user_scale : 1.0f) / hd;
        m->size = us * (0.75f + 0.25f * ut);
    }

    float s = m->size;
    m->ctl_h      = 36.0f * s;
    m->ctl_h_sm   = 28.0f * s;
    m->ctl_h_lg   = 44.0f * s;
    m->pad_card   = 13.0f * s;
    m->pad_sheet  = qlcg_clampf((float)win_w * 0.015f, 14.0f, 22.0f) * s;
    m->rhythm     = qlcg_clampf((float)win_w * 0.011f, 12.0f, 17.0f) * s;
    m->gap_grid   = 10.0f * s;
    m->tab_h      = 44.0f * s;
    m->sp[1] = 4.0f * s;
    m->sp[2] = 8.0f * s;
    m->sp[3] = 12.0f * s;
    m->sp[4] = 16.0f * s;
    m->sp[5] = 24.0f * s;
    m->sp[6] = 32.0f * s;
    m->sp[7] = 48.0f * s;
    m->sp[8] = 64.0f * s;
}
