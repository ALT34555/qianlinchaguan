#ifdef _WIN32
#include "platform/win32/native.h"
#include "platform/ui_backend.h"
#include "ui/ui_core.h"
#include "core/math_util.h"
#include <stdlib.h>
#include <string.h>
typedef struct { HFONT font; int face, px; bool bold; } QlFontCache;
struct QlTextBackend {
    HDC mem_dc;
    HBITMAP dib;
    uint32_t *dib_bits;
    HFONT fonts[FONT_COUNT];
    QlFontCache dyn[UI_FONT_CACHE];
    int dyn_count;
};
void qlcg_platform_font_rebuild(UiContext *ui) {
    for (int i = 0; i < FONT_COUNT; i++) {
        if (ui->text_backend->fonts[i]) { DeleteObject(ui->text_backend->fonts[i]); ui->text_backend->fonts[i] = NULL; }
    }
    const wchar_t *fams[FONT_COUNT] = {
        ui->family_display, ui->family_display, ui->family_body,
        ui->family_body,    ui->family_body,    ui->family_mono
    };
    static const int   pxs[FONT_COUNT]   = { 34, 20, 15, 15, 13, 13 };
    static const int   wts[FONT_COUNT]   = { FW_BOLD, FW_SEMIBOLD, FW_NORMAL, FW_BOLD, FW_NORMAL, FW_NORMAL };
    for (int i = 0; i < FONT_COUNT; i++) {
        ui->text_backend->fonts[i] = CreateFontW(-pxs[i], 0, 0, 0, wts[i], FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   ANTIALIASED_QUALITY, DEFAULT_PITCH, fams[i]);
    }
    for (int i = 0; i < ui->text_backend->dyn_count; i++) {
        if (ui->text_backend->dyn[i].font) DeleteObject(ui->text_backend->dyn[i].font);
    }
    ui->text_backend->dyn_count = 0;
    for (int i = 0; i < UI_CACHE_SLOTS; i++) ui->slots[i].used = false;
}
static HFONT ui_font_get(UiContext *ui, int face, int px, bool bold) {
    if (px < 6) px = 6;
    if (px > 160) px = 160;   /* 上限160：4倍text_scale约110px */
    for (int i = 0; i < ui->text_backend->dyn_count; i++) {
        if (ui->text_backend->dyn[i].face == face && ui->text_backend->dyn[i].px == px && ui->text_backend->dyn[i].bold == bold) {
            return ui->text_backend->dyn[i].font;
        }
    }
    if (ui->text_backend->dyn_count >= UI_FONT_CACHE) return ui->text_backend->fonts[FONT_BODY];
    const wchar_t *family = (face == UI_FACE_DISPLAY) ? ui->family_display
                          : (face == UI_FACE_MONO ? ui->family_mono : ui->family_body);
    HFONT f = CreateFontW(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY, DEFAULT_PITCH, family);
    ui->text_backend->dyn[ui->text_backend->dyn_count].font = f;
    ui->text_backend->dyn[ui->text_backend->dyn_count].face = face;
    ui->text_backend->dyn[ui->text_backend->dyn_count].px   = px;
    ui->text_backend->dyn[ui->text_backend->dyn_count].bold = bold;
    ui->text_backend->dyn_count++;
    return f;
}
bool qlcg_platform_font_init(UiContext *ui) {
    ui->text_backend = calloc(1, sizeof(*ui->text_backend));
    if (!ui->text_backend) return false;
    ui->text_backend->mem_dc = CreateCompatibleDC(NULL);
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = 1024;
    bmi.bmiHeader.biHeight      = -224;   /* 大字号烘焙需要更高的画布 */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    ui->text_backend->dib = CreateDIBSection(ui->text_backend->mem_dc, &bmi, DIB_RGB_COLORS, (void **)&ui->text_backend->dib_bits, NULL, 0);
    SelectObject(ui->text_backend->mem_dc, ui->text_backend->dib);

    wcscpy_s(ui->family_display, 64, L"Microsoft YaHei UI");
    wcscpy_s(ui->family_body,    64, L"Microsoft YaHei UI");
    wcscpy_s(ui->family_mono,    64, L"Consolas");
    qlcg_platform_font_rebuild(ui);

    return ui->text_backend->mem_dc && ui->text_backend->dib && ui->text_backend->dib_bits;
}

void qlcg_platform_font_shutdown(UiContext *ui) {
    if (!ui->text_backend) return;
    if (ui->text_backend->mem_dc) DeleteDC(ui->text_backend->mem_dc);
    for (int i = 0; i < FONT_COUNT; i++) {
        if (ui->text_backend->fonts[i]) DeleteObject(ui->text_backend->fonts[i]);
    }
    for (int i = 0; i < ui->text_backend->dyn_count; i++) {
        if (ui->text_backend->dyn[i].font) DeleteObject(ui->text_backend->dyn[i].font);
    }
    ui->text_backend->dyn_count = 0;
    if (ui->text_backend->dib) DeleteObject(ui->text_backend->dib);
    free(ui->text_backend);
    ui->text_backend = NULL;
}

bool qlcg_platform_text_raster(UiContext *ui, int face, int px, bool bold, const char *text, uint8_t **pixels, int *width, int *height) {
    if (!ui->text_backend) return false;
    wchar_t wbuf[256];
    /* 先按字节截断4000再转宽字符，防返0 */
    int nbytes = (int)strlen(text);
    if (nbytes > 4000) nbytes = 4000;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, nbytes, wbuf, 255);
    if (wlen <= 0) return false;
    wbuf[wlen] = L'\0';

    SelectObject(ui->text_backend->mem_dc, ui_font_get(ui, face, px, bold));
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(ui->text_backend->mem_dc, wbuf, wlen, &sz);
    int tw = qlcg_clampi(sz.cx + 4, 4, 1020);
    int th = qlcg_clampi(sz.cy + 2, 4, 216);   /* 上限对齐 DIB 高度 */

    for (int y = 0; y < th; y++) {
        memset(ui->text_backend->dib_bits + y * 1024, 0, (size_t)tw * 4);
    }
    SetBkMode(ui->text_backend->mem_dc, TRANSPARENT);
    SetTextColor(ui->text_backend->mem_dc, RGB(255, 255, 255));
    RECT rc = {1, 1, tw, th};
    DrawTextW(ui->text_backend->mem_dc, wbuf, wlen, &rc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    uint8_t *upload = (uint8_t *)malloc((size_t)tw * (size_t)th * 4);
    if (!upload) return false;
    for (int y = 0; y < th; y++) {
        uint32_t *src = ui->text_backend->dib_bits + y * 1024;
        uint8_t  *dst = upload + y * tw * 4;
        for (int x = 0; x < tw; x++) {
            uint32_t pix = src[x];
            uint8_t r = (uint8_t)((pix >> 16) & 0xFF);
            uint8_t g = (uint8_t)((pix >> 8) & 0xFF);
            uint8_t b = (uint8_t)(pix & 0xFF);
            uint8_t lum = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
            dst[x * 4 + 0] = 255;
            dst[x * 4 + 1] = 255;
            dst[x * 4 + 2] = 255;
            dst[x * 4 + 3] = lum;
        }
    }

    *pixels = upload; *width = tw; *height = th;
    return true;
}
#endif
