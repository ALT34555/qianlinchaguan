/* 舆图雨云：读降水场出纹理 */
#include "atlas_rain.h"
#include "../systems/world/precip.h"
#include "../core/math_util.h"
#include <math.h>
#include <stdlib.h>

#define RAIN_MIN_MS  200.0
#define RAIN_STAMP_MS 600000.0   /* 10 游戏分钟 */

void qlcg_atlas_rain_sync(AtlasRenderer *ar, const MacroWorld *w, const CalendarClock *clk) {
    if (!ar || !w || w->columns <= 0 || w->rows <= 0) return;
    if (qlcg_atlas_layer_opacity(&ar->display, LAYER_RAIN) <= 0.0f) return;
    double now_ms = qlcg_time_ms();
    if (ar->rain_key_day != UINT32_MAX && now_ms - ar->rain_last_ms < RAIN_MIN_MS) return;

    double unix_ms = clk ? clk->unix_ms : 0.0;
    int tw = w->rows > 128 ? w->rows : 128;
    if (tw > 512) tw = 512;
    int th = tw / 2;
    qlcg_precip_sync((MacroWorld *)w, unix_ms);
    const PrecipField *f = w->precip;
    if (!f || !f->ready) return;
    float season_s = qlcg_precip_season_s(w->plant_calendar);
    uint32_t stamp = (uint32_t)(unix_ms / RAIN_STAMP_MS);
    if (ar->rain_tex && ar->rain_key_day == (uint32_t)f->day && ar->rain_key_ver == f->version &&
        ar->rain_key_stamp == stamp && ar->rain_tex_w == tw) return;

    uint8_t *buf = (uint8_t *)malloc((size_t)tw * (size_t)th * 4);
    if (!buf) return;

    float cell_w = w->circumference / (float)w->columns;
    float cell_h = qlcg_world_depth(w) / (float)w->rows;
    float foot_x = cell_w * (float)w->columns / (float)tw;
    float foot_y = cell_h * (float)w->rows / (float)th;
    float footprint = foot_x > foot_y ? foot_x : foot_y;

    for (int y = 0; y < th; y++) {
        float lat = (0.5f - ((float)y + 0.5f) / (float)th) * 3.1415926f;
        int row = (int)((0.5f - lat / 3.1415926f) * (float)w->rows);
        if (row < 0) row = 0;
        if (row >= w->rows) row = w->rows - 1;
        for (int x = 0; x < tw; x++) {
            float lon = (((float)x + 0.5f) / (float)tw - 0.5f) * 6.2831853f;
            int col = (int)((lon / 6.2831853f + 0.5f) * (float)w->columns);
            if (col < 0) col = 0;
            if (col >= w->columns) col = w->columns - 1;
            float temp = qlcg_precip_season_temp_s(w, row * w->columns + col, season_s);
            PrecipSample ps;
            qlcg_precip_sample(f, unix_ms, lon, lat, footprint, temp, &ps);

            float r, g, b, a;
            if (ps.intensity > 0.01f) {
                if (ps.precipitation == 1) {
                    if (ps.id == 103 || ps.id == 104) { r = 44; g = 78; b = 150; }
                    else { r = 88; g = 155; b = 218; }
                } else if (ps.precipitation == 2) {
                    r = 176; g = 216; b = 232;
                } else {
                    r = 168; g = 155; b = 207;
                }
                a = 0.12f + 0.88f * ps.intensity;
            } else {
                r = 222; g = 228; b = 232;
                a = (ps.cloud_cover - 0.42f) * 0.30f;
                if (a < 0.0f) a = 0.0f;
            }
            if (a > 1.0f) a = 1.0f;
            uint8_t *px = buf + ((size_t)y * (size_t)tw + (size_t)x) * 4;
            px[0] = (uint8_t)qlcg_clampi((int)(r + 0.5f), 0, 255);
            px[1] = (uint8_t)qlcg_clampi((int)(g + 0.5f), 0, 255);
            px[2] = (uint8_t)qlcg_clampi((int)(b + 0.5f), 0, 255);
            px[3] = (uint8_t)qlcg_clampi((int)(a * 255.0f + 0.5f), 0, 255);
        }
    }

    if (!ar->rain_tex) glGenTextures(1, &ar->rain_tex);
    glBindTexture(GL_TEXTURE_2D, ar->rain_tex);
    if (ar->rain_tex_w == tw && ar->rain_tex_h == th)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tw, th, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    free(buf);

    ar->rain_key_day = (uint32_t)f->day;
    ar->rain_key_ver = f->version;
    ar->rain_key_stamp = stamp;
    ar->rain_tex_w = tw;
    ar->rain_tex_h = th;
    ar->rain_last_ms = now_ms;
}
