/* Sprite9 源图逐张加载为独立纹理 */
#define COBJMACROS
#include "ui_sprite.h"
#include <wincodec.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

static SpriteDef g_sprites[SP_MAX_ITEMS];
static int       g_sprite_count = 0;
static uint64_t  g_sp_sig       = 0;
static float     g_last_poll    = 0.0f;

static const wchar_t *const SP_DIRS[] = {
    L"content\\assets\\ui\\frames",
    L"content\\assets\\ui\\icons",
    L"content\\assets\\ui\\decor",
    L"content\\assets\\ui\\control_pad"
};
enum { SP_DIR_COUNT = (int)(sizeof(SP_DIRS) / sizeof(SP_DIRS[0])) };

typedef struct { int a, b; } SpRun;

/* 颜色掩码判定：对齐素材制作工具的四象限色标 */
static inline bool sp_is_red(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return r > 200 && g < 80 && b < 80 && a > 200;
}
static inline bool sp_is_cyan(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return r < 80 && g > 200 && b > 200 && a > 200;
}
static inline bool sp_is_yellow(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return r > 200 && g > 200 && b < 80 && a > 200;
}
static inline bool sp_is_black(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return r < 32 && g < 32 && b < 32 && a > 200;
}

/* 提取一维超过半峰值的连续区间 */
static int sp_find_runs(const int *counts, int n, int max_v, SpRun *out, int max_out) {
    if (max_v <= 0) return 0;
    int nr = 0, i = 0;
    while (i < n && nr < max_out) {
        while (i < n && counts[i] * 2 <= max_v) i++;
        if (i >= n) break;
        int a = i;
        while (i < n && counts[i] * 2 > max_v) i++;
        out[nr++] = (SpRun){ a, i };
    }
    return nr;
}

/* 将模式序列压缩为连续分段表 */
static int sp_build_axis(const unsigned char *states, int n, SpriteSeg *out) {
    if (n <= 0) return 0;
    int ns = 0, a = 0;
    for (int i = 1; i <= n && ns < SP_MAX_SEG; i++) {
        if (i == n || states[i] != states[a]) {
            out[ns++] = (SpriteSeg){ (short)a, (short)i, states[a] };
            a = i;
        }
    }
    return ns;
}

/* 计算内容区：黑线优先，退化彩色段或整轴 */
static void sp_content_range(int blk0, int blk1, const SpriteSeg *seg, int nseg,
                             int len, int *c0, int *c1) {
    if (blk0 >= 0 && blk1 > blk0) {
        *c0 = blk0;
        *c1 = blk1;
        return;
    }
    int fa = -1, fb = -1;
    for (int i = 0; i < nseg; i++) {
        if (seg[i].mode != SP_MODE_FIXED) {
            if (fa < 0) fa = seg[i].a;
            fb = seg[i].b;
        }
    }
    if (fa >= 0 && fb > fa) {
        *c0 = fa;
        *c1 = fb;
        return;
    }
    *c0 = 0;
    *c1 = len;
}

/* 用 WIC 解码 PNG 为 32bpp BGRA 缓冲区 */
static uint8_t *sp_decode_bgra(IWICImagingFactory *factory, const wchar_t *path,
                               int *out_w, int *out_h) {
    uint8_t *buf = NULL;
    IWICBitmapDecoder *dec = NULL;
    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(
            factory, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) || !dec) {
        return NULL;
    }
    IWICBitmapFrameDecode *frm = NULL;
    if (SUCCEEDED(IWICBitmapDecoder_GetFrame(dec, 0, &frm)) && frm) {
        IWICFormatConverter *conv = NULL;
        if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory, &conv)) && conv) {
            if (SUCCEEDED(IWICFormatConverter_Initialize(
                    conv, (IWICBitmapSource *)frm, &GUID_WICPixelFormat32bppBGRA,
                    WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom))) {
                UINT w = 0, h = 0;
                IWICFormatConverter_GetSize(conv, &w, &h);
                if (w > 0 && h > 0 && w <= 4096 && h <= 4096) {
                    buf = (uint8_t *)malloc((size_t)w * (size_t)h * 4);
                    if (buf && SUCCEEDED(IWICBitmapSource_CopyPixels(
                            conv, NULL, w * 4, w * h * 4, buf))) {
                        *out_w = (int)w;
                        *out_h = (int)h;
                    } else {
                        free(buf);
                        buf = NULL;
                    }
                }
            }
            IWICFormatConverter_Release(conv);
        }
        IWICBitmapFrameDecode_Release(frm);
    }
    IWICBitmapDecoder_Release(dec);
    return buf;
}

/* 解析单张四宫格源图，压成一张独立纹理并填好分段表 */
static bool sp_load_one(IWICImagingFactory *factory, const wchar_t *path,
                        const char *stem, SpriteDef *sp) {
    int iw = 0, ih = 0;
    uint8_t *img = sp_decode_bgra(factory, path, &iw, &ih);
    if (!img) return false;

    int *col_red = (int *)calloc((size_t)(iw + ih), sizeof(int));
    if (!col_red) { free(img); return false; }
    int *row_red = col_red + iw;
    int max_c = 0, max_r = 0;

    for (int y = 0; y < ih; y++) {
        const uint8_t *row = img + (size_t)y * (size_t)iw * 4;
        for (int x = 0; x < iw; x++) {
            const uint8_t *p = row + x * 4;
            if (sp_is_red(p[2], p[1], p[0], p[3])) {
                if (++col_red[x] > max_c) max_c = col_red[x];
                if (++row_red[y] > max_r) max_r = row_red[y];
            }
        }
    }

    SpRun xs[4], ys[4];
    int nx = sp_find_runs(col_red, iw, max_c, xs, 4);
    int ny = sp_find_runs(row_red, ih, max_r, ys, 4);
    free(col_red);

    if (nx != 3 || ny != 3) { free(img); return false; }
    int x0 = xs[0].b, x1 = xs[1].a, W = x1 - x0;
    int y0 = ys[0].b, y1 = ys[1].a, H = y1 - y0;
    if (W <= 0 || H <= 0 || W > 1020 || H > 1020) { free(img); return false; }

    unsigned char xstate[1024], ystate[1024];
    int hblk0 = -1, hblk1 = -1, vblk0 = -1, vblk1 = -1;
    for (int i = 0; i < W; i++) {
        const uint8_t *pm = img + ((size_t)ys[0].a * (size_t)iw + (size_t)(x0 + i)) * 4;
        const uint8_t *pb = img + ((size_t)ys[1].a * (size_t)iw + (size_t)(x0 + i)) * 4;
        xstate[i] = sp_is_cyan(pm[2], pm[1], pm[0], pm[3]) ? SP_MODE_STRETCH :
                    (sp_is_yellow(pm[2], pm[1], pm[0], pm[3]) ? SP_MODE_REPEAT : SP_MODE_FIXED);
        if (sp_is_black(pb[2], pb[1], pb[0], pb[3])) {
            if (hblk0 < 0) hblk0 = i;
            hblk1 = i + 1;
        }
    }
    for (int i = 0; i < H; i++) {
        const uint8_t *pm = img + ((size_t)(y0 + i) * (size_t)iw + (size_t)xs[0].a) * 4;
        const uint8_t *pb = img + ((size_t)(y0 + i) * (size_t)iw + (size_t)xs[1].a) * 4;
        ystate[i] = sp_is_yellow(pm[2], pm[1], pm[0], pm[3]) ? SP_MODE_REPEAT :
                    (sp_is_cyan(pm[2], pm[1], pm[0], pm[3]) ? SP_MODE_STRETCH : SP_MODE_FIXED);
        if (sp_is_black(pb[2], pb[1], pb[0], pb[3])) {
            if (vblk0 < 0) vblk0 = i;
            vblk1 = i + 1;
        }
    }

    memset(sp, 0, sizeof(*sp));
    strncpy(sp->name, stem, sizeof(sp->name) - 1);
    sp->w  = W;
    sp->h  = H;
    sp->xn = sp_build_axis(xstate, W, sp->xseg);
    sp->yn = sp_build_axis(ystate, H, sp->yseg);
    sp_content_range(hblk0, hblk1, sp->xseg, sp->xn, W, &sp->cx0, &sp->cx1);
    sp_content_range(vblk0, vblk1, sp->yseg, sp->yn, H, &sp->cy0, &sp->cy1);

    /* 四层象限：前三层白化，detail 保原色 */
    static const int Q_COL[4] = { 0, 0, 1, 1 };
    static const int Q_ROW[4] = { 1, 0, 0, 1 };
    static const int Q_DX[4]  = { 0, 1, 0, 1 };
    static const int Q_DY[4]  = { 0, 0, 1, 1 };
    int tw = W * 2 + 4, th = H * 2 + 4;
    uint8_t *buf = (uint8_t *)calloc((size_t)tw * (size_t)th * 4, 1);
    if (!buf) { free(img); return false; }

    for (int L = 0; L < 4; L++) {
        int c = Q_COL[L], r = Q_ROW[L];
        int bx0 = xs[c].b, bw = xs[c + 1].a - bx0;
        int by0 = ys[r].b, bh = ys[r + 1].a - by0;
        int px0 = 2 + Q_DX[L] * (W + 2);
        int py0 = 2 + Q_DY[L] * (H + 2);
        sp->layer_x[L] = px0;
        sp->layer_y[L] = py0;
        if (bw <= 0 || bh <= 0) continue;

        for (int py = 0; py < H; py++) {
            int sy = by0 + (py * bh) / H;
            uint8_t *dst_row = buf + ((size_t)(py0 + py) * (size_t)tw + (size_t)px0) * 4;
            const uint8_t *src_row = img + (size_t)sy * (size_t)iw * 4;
            for (int px = 0; px < W; px++) {
                int sx = bx0 + (px * bw) / W;
                const uint8_t *s = src_row + sx * 4;
                uint8_t al = s[3];
                if (al > 0) {
                    uint8_t *d = dst_row + px * 4;
                    d[0] = (L == 3) ? s[0] : 255;
                    d[1] = (L == 3) ? s[1] : 255;
                    d[2] = (L == 3) ? s[2] : 255;
                    d[3] = al;
                }
            }
        }
    }

    glGenTextures(1, &sp->tex);
    glBindTexture(GL_TEXTURE_2D, sp->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_BGRA, GL_UNSIGNED_BYTE, buf);
    sp->tex_w = tw;
    sp->tex_h = th;

    free(buf);
    free(img);
    return true;
}

/* 宽字符文件名按字母序比较 */
static int sp_cmp_wname(const void *a, const void *b) {
    return _wcsicmp((const wchar_t *)a, (const wchar_t *)b);
}

/* 计算源图目录状态签名，供热重载比对 */
static uint64_t sp_calc_signature(void) {
    uint64_t h = 1469598103934665603ULL;
    for (int d = 0; d < SP_DIR_COUNT; d++) {
        wchar_t pat[MAX_PATH];
        swprintf_s(pat, MAX_PATH, L"%s\\*.png", SP_DIRS[d]);
        WIN32_FIND_DATAW fd;
        HANDLE hf = FindFirstFileW(pat, &fd);
        if (hf == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (_wcsicmp(fd.cFileName, L"qianlin_teacup.png") == 0) continue;
            for (const wchar_t *p = fd.cFileName; *p; p++) {
                h = (h ^ (uint64_t)*p) * 1099511628211ULL;
            }
            h = (h ^ (uint64_t)fd.ftLastWriteTime.dwLowDateTime) * 1099511628211ULL;
            h = (h ^ (uint64_t)fd.ftLastWriteTime.dwHighDateTime) * 1099511628211ULL;
            h = (h ^ (uint64_t)fd.nFileSizeLow) * 1099511628211ULL;
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }
    return h;
}

/* 释放全部已加载纹理 */
void ui_sprite_release(void) {
    for (int i = 0; i < g_sprite_count; i++) {
        if (g_sprites[i].tex) glDeleteTextures(1, &g_sprites[i].tex);
        g_sprites[i].tex = 0;
    }
    g_sprite_count = 0;
}

/* 运行时扫描全部源图，逐张加载 */
bool ui_sprite_load_all(UiContext *ui) {
    (void)ui;
    ui_sprite_release();

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IWICImagingFactory *factory = NULL;
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IWICImagingFactory, (void **)&factory)) || !factory) {
        return false;
    }

    int total = 0;
    wchar_t names[SP_MAX_ITEMS][MAX_PATH];

    for (int d = 0; d < SP_DIR_COUNT; d++) {
        wchar_t pat[MAX_PATH];
        swprintf_s(pat, MAX_PATH, L"%s\\*.png", SP_DIRS[d]);
        WIN32_FIND_DATAW fd;
        HANDLE hf = FindFirstFileW(pat, &fd);
        if (hf == INVALID_HANDLE_VALUE) continue;
        int n = 0;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (_wcsicmp(fd.cFileName, L"qianlin_teacup.png") == 0) continue;
            if (n < SP_MAX_ITEMS) wcscpy_s(names[n++], MAX_PATH, fd.cFileName);
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);

        if (n > 1) qsort(names, (size_t)n, sizeof(names[0]), sp_cmp_wname);

        for (int i = 0; i < n && total < SP_MAX_ITEMS; i++) {
            wchar_t full[MAX_PATH], stem_w[MAX_PATH];
            swprintf_s(full, MAX_PATH, L"%s\\%s", SP_DIRS[d], names[i]);
            wcscpy_s(stem_w, MAX_PATH, names[i]);
            wchar_t *dot = wcsrchr(stem_w, L'.');
            if (dot) *dot = L'\0';
            char stem[64] = {0};
            WideCharToMultiByte(CP_UTF8, 0, stem_w, -1, stem, sizeof(stem) - 1, NULL, NULL);

            if (sp_load_one(factory, full, stem, &g_sprites[total])) {
                printf("[Sprite] %s -> %dx%d\n", stem, g_sprites[total].w, g_sprites[total].h);
                total++;
            } else {
                printf("[Sprite] %s 加载失败\n", stem);
            }
        }
    }

    IWICImagingFactory_Release(factory);
    g_sprite_count = total;
    g_sp_sig       = sp_calc_signature();
    return total > 0;
}

void ui_sprite_poll_reload(UiContext *ui) {
    if (ui->time - g_last_poll < 0.5f) return;
    g_last_poll = ui->time;
    if (sp_calc_signature() != g_sp_sig) {
        ui_sprite_load_all(ui);
    }
}

int ui_sprite_count(void) {
    return g_sprite_count;
}

const SpriteDef *ui_sprite_at(int index) {
    if (index < 0 || index >= g_sprite_count) return NULL;
    return &g_sprites[index];
}

const struct SpriteDef *ui_sprite_by_name(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < g_sprite_count; i++) {
        if (strcmp(g_sprites[i].name, name) == 0) return &g_sprites[i];
    }
    return NULL;
}

/* 分段规划：固定段保原尺寸，弹性段分摊剩余空间 */
static void sp_plan(const SpriteSeg *seg, int n, float dst, float *len, float *off) {
    if (n <= 0) return;
    float fixed_src = 0.0f, flex_src = 0.0f;
    for (int i = 0; i < n; i++) {
        float l = (float)(seg[i].b - seg[i].a);
        if (l < 0.0f) l = 0.0f;
        if (seg[i].mode == SP_MODE_FIXED) fixed_src += l;
        else                              flex_src += l;
    }
    float flex_dst = dst - fixed_src;
    if (flex_dst < 0.0f) flex_dst = 0.0f;
    float k = (flex_src > 0.01f) ? flex_dst / flex_src : 1.0f;
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        float l = (float)(seg[i].b - seg[i].a);
        if (l < 0.0f) l = 0.0f;
        len[i] = (seg[i].mode == SP_MODE_FIXED) ? l : l * k;
        sum += len[i];
    }
    /* 目标比固定段还小则整体等比压 */
    if (sum > 0.01f) {
        float sc = dst / sum;
        if (sc < 0.0f) sc = 0.0f;
        sum = 0.0f;
        for (int i = 0; i < n; i++) { len[i] *= sc; sum += len[i]; }
    }
    float acc = 0.0f;
    for (int i = 0; i < n; i++) { off[i] = acc; acc += len[i]; }
}

/* 画一层：按 x/y 分段网格铺满目标框 */
static void sp_layer(UiContext *ui, const SpriteDef *sp, int g,
                     float dx, float dy, float dw, float dh, uint32_t tint) {
    if (sp->xn <= 0 || sp->yn <= 0 || sp->xn > SP_MAX_SEG || sp->yn > SP_MAX_SEG) return;
    ui_use_sprite_tex(ui, sp->tex);
    float xl[SP_MAX_SEG], xo[SP_MAX_SEG];
    float yl[SP_MAX_SEG], yo[SP_MAX_SEG];
    sp_plan(sp->xseg, sp->xn, dw, xl, xo);
    sp_plan(sp->yseg, sp->yn, dh, yl, yo);
    float fw = (float)sp->tex_w, fh = (float)sp->tex_h;
    float ax = (float)sp->layer_x[g];
    float ay = (float)sp->layer_y[g];
    for (int j = 0; j < sp->yn; j++) {
        if (yl[j] <= 0.0f) continue;
        float v0 = (ay + (float)sp->yseg[j].a) / fh;
        float v1 = (ay + (float)sp->yseg[j].b) / fh;
        for (int i = 0; i < sp->xn; i++) {
            if (xl[i] <= 0.0f) continue;
            float u0 = (ax + (float)sp->xseg[i].a) / fw;
            float u1 = (ax + (float)sp->xseg[i].b) / fw;
            ui_draw_sprite_rect(ui, dx + xo[i], dy + yo[j], xl[i], yl[j],
                                u0, v0, u1, v1, tint);
        }
    }
}

void ui_sprite4(UiContext *ui, const struct SpriteDef *sp, float x, float y, float w, float h,
                uint32_t c_surface, uint32_t c_primary, uint32_t c_accent) {
    if (!sp || !sp->tex || w <= 0.0f || h <= 0.0f) return;
    sp_layer(ui, sp, 0, x, y, w, h, c_surface);
    sp_layer(ui, sp, 1, x, y, w, h, c_primary);
    sp_layer(ui, sp, 2, x, y, w, h, c_accent);
    sp_layer(ui, sp, 3, x, y, w, h, 0xFFFFFFFFu);
}

void ui_sprite_plain(UiContext *ui, const struct SpriteDef *sp, float x, float y, float w, float h) {
    ui_sprite4(ui, sp, x, y, w, h, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu);
}

void ui_sprite_layer(UiContext *ui, const struct SpriteDef *sp, int layer,
                     float x, float y, float w, float h, uint32_t tint) {
    if (!sp || !sp->tex || layer < 0 || layer > 3 || w <= 0.0f || h <= 0.0f) return;
    sp_layer(ui, sp, layer, x, y, w, h, tint);
}

/* 有效内容区：未标记黑线时退化为整格 */
static void sp_content(const SpriteDef *sp, float *cx, float *cy, float *cw, float *ch) {
    *cx = (float)sp->cx0;
    *cy = (float)sp->cy0;
    *cw = (float)(sp->cx1 - sp->cx0);
    *ch = (float)(sp->cy1 - sp->cy0);
    if (*cw <= 0.5f || *ch <= 0.5f) {
        *cx = 0.0f;
        *cy = 0.0f;
        *cw = (float)sp->w;
        *ch = (float)sp->h;
    }
}

/* 图标几何：长边贴合 size，短边留白 */
static void sp_icon_box(const SpriteDef *sp, float x, float y, float size,
                        float *dx, float *dy, float *dw, float *dh,
                        float *cx, float *cy, float *cw, float *ch) {
    sp_content(sp, cx, cy, cw, ch);
    float k = size / ((*cw > *ch) ? *cw : *ch);
    *dw = *cw * k;
    *dh = *ch * k;
    *dx = x + (size - *dw) * 0.5f;
    *dy = y + (size - *dh) * 0.5f;
}

void ui_sprite_icon(UiContext *ui, const struct SpriteDef *sp, float x, float y, float size,
                    uint32_t tint) {
    if (!sp || !sp->tex || size <= 0.0f) return;
    const int g = 3;
    float dx, dy, dw, dh, cx, cy, cw, ch;
    sp_icon_box(sp, x, y, size, &dx, &dy, &dw, &dh, &cx, &cy, &cw, &ch);
    ui_use_sprite_tex(ui, sp->tex);
    float fw = (float)sp->tex_w, fh = (float)sp->tex_h;
    float ax = (float)sp->layer_x[g];
    float ay = (float)sp->layer_y[g];
    ui_draw_sprite_rect(ui, dx, dy, dw, dh,
                        (ax + cx) / fw, (ay + cy) / fh,
                        (ax + cx + cw) / fw, (ay + cy + ch) / fh, tint);
}

void ui_sprite_icon4(UiContext *ui, const struct SpriteDef *sp, float x, float y, float size,
                     uint32_t c_surface, uint32_t c_primary, uint32_t c_accent) {
    ui_sprite_icon4_rot(ui, sp, x, y, size, 0.0f, c_surface, c_primary, c_accent);
}

void ui_sprite_icon4_rot(UiContext *ui, const struct SpriteDef *sp, float x, float y, float size,
                         float rad, uint32_t c_surface, uint32_t c_primary, uint32_t c_accent) {
    if (!sp || !sp->tex || size <= 0.0f) return;
    float dx, dy, dw, dh, cx, cy, cw, ch;
    sp_icon_box(sp, x, y, size, &dx, &dy, &dw, &dh, &cx, &cy, &cw, &ch);
    ui_use_sprite_tex(ui, sp->tex);
    float fw = (float)sp->tex_w, fh = (float)sp->tex_h;
    float ccx = dx + dw * 0.5f;
    float ccy = dy + dh * 0.5f;
    const uint32_t tints[4] = { c_surface, c_primary, c_accent, 0xFFFFFFFFu };
    for (int g = 0; g < 4; g++) {
        float ax = (float)sp->layer_x[g];
        float ay = (float)sp->layer_y[g];
        ui_draw_sprite_rect_rot(ui, ccx, ccy, dw, dh, rad,
                                (ax + cx) / fw, (ay + cy) / fh,
                                (ax + cx + cw) / fw, (ay + cy + ch) / fh, tints[g]);
    }
}
