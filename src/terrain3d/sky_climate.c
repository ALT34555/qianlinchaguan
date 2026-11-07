/* 天空气候调色板，对齐 base daylightAt */
#include "sky_climate.h"
#include "core/dev_tools.h"
#include "../core/math_util.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* 基色（sRGB 0..1） */
static const float C_NIGHT_HORSE[3] = { 0.067f, 0.110f, 0.204f };  /* #111c34 */
static const float C_FOG_DAY[3]      = { 0.663f, 0.827f, 1.000f };  /* #a9d3ff */
static const float C_FOG_DUSK[3]     = { 0.784f, 0.561f, 0.510f };  /* #c88f82 */
static const float C_ZEN_NIGHT[3]    = { 0.051f, 0.090f, 0.188f };  /* #0d1730 */
static const float C_ZEN_DAY[3]      = { 0.239f, 0.471f, 0.824f };  /* #3d78d2 */
static const float C_CLOUD_NIGHT[3]  = { 0.220f, 0.267f, 0.373f };  /* #38445f */
static const float C_CLOUD_DAY[3]    = { 1.000f, 1.000f, 1.000f };
static const float C_CLOUD_WARM[3]   = { 1.000f, 0.698f, 0.467f };  /* #ffb277 */
static const float C_CLOUD_RAIN[3]   = { 0.408f, 0.447f, 0.486f };  /* #68727c */
static const float C_GLOW_DAY[3]     = { 1.000f, 0.949f, 0.812f };  /* #fff2cf */
static const float C_GLOW_DUSK[3]    = { 1.000f, 0.604f, 0.298f };  /* #ff9a4c */
static const float C_FLASH[3]        = { 0.902f, 0.929f, 1.000f };  /* #e6edff */
static const float C_STORM[3]        = { 0.408f, 0.447f, 0.486f };  /* #68727c */

/* 四季色调（base SEASON_TINTS） */
static const float SEASON_TINTS[4][3] = {
    { 0.902f, 1.000f, 0.902f },
    { 1.000f, 0.933f, 0.902f },
    { 1.000f, 0.949f, 0.839f },
    { 0.902f, 0.933f, 1.000f }
};

static float smoothstep01(float a, float b, float x) {
    float t = qlcg_clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static void mix3(const float *a, const float *b, float t, float *out) {
    out[0] = a[0] + (b[0] - a[0]) * t;
    out[1] = a[1] + (b[1] - a[1]) * t;
    out[2] = a[2] + (b[2] - a[2]) * t;
}

static void scale3(const float *a, float k, float *out) {
    out[0] = a[0] * k;
    out[1] = a[1] * k;
    out[2] = a[2] * k;
}

/* 天气压暗天色（base WorldLighting） */
static void apply_weather(const WeatherProfile *wp, bool atmosphere, float daylight,
                          float precip, float *horizon, float *zenith) {
    if (!atmosphere || !wp) return;
    float dim = qlcg_clampf(wp->dim, 0.0f, 1.0f);
    float wc[3] = {
        (float)((wp->color >> 24) & 0xFF) / 255.0f,
        (float)((wp->color >> 16) & 0xFF) / 255.0f,
        (float)((wp->color >> 8) & 0xFF) / 255.0f
    };
    float tinted[3];
    scale3(wc, 0.25f + daylight * 0.75f, tinted);
    float wt = qlcg_maxf(dim, 1.0f - qlcg_clampf(wp->fog, 0.0f, 1.0f));
    mix3(horizon, tinted, wt, horizon);
    mix3(zenith, horizon, dim, zenith);

    float storm[3];
    scale3(C_STORM, 0.2f + daylight * 0.8f, storm);
    mix3(horizon, storm, precip * 0.65f, horizon);
    mix3(zenith, storm, precip * 0.85f, zenith);
}

void qlcg_sky_climate_astra(const CalendarClock *clk, int utc_offset_min, int season,
                      const WeatherProfile *wp, bool atmosphere, float sim_time,
                      float precip_override, const AstraFrame *astra, SkyClimate *out) {
    memset(out, 0, sizeof(*out));
    int wid = wp ? wp->id : 0;

    /* 民用时间→太阳高度（base daylightAt） */
    float hour = clk ? qlcg_local_sun_hours(clk->unix_ms, utc_offset_min) : 10.5f;
    hour = qlcg_dev_hour(hour);
    float day_ratio = hour / 24.0f;
    if (wid == 900 || wid == 901) day_ratio = 0.5f;   /* 日月同耀 / 极昼 */
    else if (wid == 902) day_ratio = 0.0f;            /* 极夜 */

    float sun_h = astra && wid < 900 ? astra->sun[1] : -cosf(day_ratio * 6.2831853f);
    float daylight = smoothstep01(-0.12f, 0.30f, sun_h);
    float dusk = qlcg_maxf(0.0f, 1.0f - fabsf(sun_h) / 0.35f);
    float night = 1.0f - smoothstep01(-0.18f, 0.08f, sun_h);
    float eclipse = astra && wid < 900 ? astra->solar_cover : 0;
    daylight *= 1.0f-eclipse*.97f;
    night = qlcg_maxf(night, smoothstep01(.95f,1.0f,eclipse)*.9f);
    dusk *= 1.0f-eclipse;
    if (wid == 901) { daylight = 1.0f; dusk = 0.0f; night = 0.0f; }
    else if (wid == 902) { daylight = 0.0f; dusk = 0.0f; night = 1.0f; }

    if (wid == 900) {
        /* 日月同耀：日月分居两侧且都在地平线上 */
        out->sun_dir[0] = 0.4f; out->sun_dir[1] = 0.55f; out->sun_dir[2] = -0.75f;
        qlcg_vec3_norm(&out->sun_dir[0], &out->sun_dir[1], &out->sun_dir[2]);
        out->moon_dir[0] = -0.4f; out->moon_dir[1] = 0.55f; out->moon_dir[2] = -0.75f;
        qlcg_vec3_norm(&out->moon_dir[0], &out->moon_dir[1], &out->moon_dir[2]);
    } else {
        float angle = (day_ratio + 0.5f) * 6.2831853f;
        out->sun_dir[0] = sinf(angle);
        out->sun_dir[1] = sun_h;
        out->sun_dir[2] = 0.12f;
        qlcg_vec3_norm(&out->sun_dir[0], &out->sun_dir[1], &out->sun_dir[2]);
        out->moon_dir[0] = -out->sun_dir[0];
        out->moon_dir[1] = -out->sun_dir[1];
        out->moon_dir[2] = -out->sun_dir[2];
    }
    out->moon = (wid == 900) ? 1.0f : ((night > 0.5f) ? night : 0.0f);
    if (astra && wid < 900) {
        memcpy(out->sun_dir, astra->sun, sizeof(out->sun_dir));
        memcpy(out->moon_dir, astra->moon, sizeof(out->moon_dir));
        out->moon = smoothstep01(-.04f, .06f, astra->moon[1]) * (1.0f - .65f * daylight);
    }

    /* 地平线 = 雾色：夜蓝 → 日雾 → 黄昏暖 */
    mix3(C_NIGHT_HORSE, C_FOG_DAY, daylight, out->horizon);
    mix3(out->horizon, C_FOG_DUSK, dusk * 0.38f, out->horizon);

    /* 天顶：夜蓝 → 日蓝 → 天气压暗 */
    mix3(C_ZEN_NIGHT, C_ZEN_DAY, daylight, out->zenith);

    float precip = (precip_override >= 0.0f)
                 ? qlcg_clampf(precip_override, 0.0f, 1.0f)
                 : qlcg_clampf(wp ? (wp->rain + wp->snow + wp->hail) : 0.0f, 0.0f, 1.0f);
    apply_weather(wp, atmosphere, daylight, precip, out->horizon, out->zenith);

    /* 日月同耀：紫雾 */
    if (atmosphere && wid == 900) {
        out->horizon[0] = 0.431f; out->horizon[1] = 0.302f; out->horizon[2] = 0.494f;
        out->zenith[0] = out->horizon[0];
        out->zenith[1] = out->horizon[1];
        out->zenith[2] = out->horizon[2];
    }

    /* 辉光：白天弱、黄昏强；雨雾天收敛 */
    mix3(C_GLOW_DAY, C_GLOW_DUSK, dusk, out->glow);
    float dim = atmosphere && wp ? qlcg_clampf(wp->dim, 0.0f, 1.0f) : 0.0f;
    out->glow_amt = (0.2f * daylight + 1.35f * dusk) * (1.0f - dim) * (1.0f - precip);
    if (wid == 900) out->glow_amt = 0.0f;

    /* 云色：夜灰 → 白 → 微暖 → 雨灰 → 压暗 */
    mix3(C_CLOUD_NIGHT, C_CLOUD_DAY, daylight, out->cloud);
    mix3(out->cloud, C_CLOUD_WARM, dusk * 0.55f, out->cloud);
    float rain_col[3];
    scale3(C_CLOUD_RAIN, 0.2f + daylight * 0.8f, rain_col);
    mix3(out->cloud, rain_col, precip, out->cloud);
    float dimk = 1.0f - dim * 0.6f;
    scale3(out->cloud, dimk, out->cloud);

    /* 雨云内壳：灰蓝随昼夜压暗（base Sky） */
    scale3(C_CLOUD_RAIN, 0.2f + daylight * 0.8f, out->rain_cloud);
    out->rain_alpha = precip * 0.92f;

    /* 星空与云量 */
    out->star = powf(night, 1.4f) * 0.95f * (1.0f - dim);
    out->cloud_cov = atmosphere && wp ? qlcg_clampf(wp->clouds, 0.0f, 3.0f) : 0.35f;
    out->dim = dim;

    /* 季节色调：白向季节色靠 12%；夜里转冷 */
    const float *st = SEASON_TINTS[qlcg_clampi(season, 0, 3)];
    float white[3] = { 1.0f, 1.0f, 1.0f };
    mix3(white, st, 0.12f, out->tint);
    {
        const float cold[3] = { 0.62f, 0.72f, 0.95f };
        mix3(out->tint, cold, night * 0.70f, out->tint);
    }

    /* 雷电闪光（只在大气效果开启时） */
    if (atmosphere && wp && wp->lightning) {
        float p = fmodf(sim_time * 0.9f, 8.0f);
        if (p < 0.08f) out->flash = 1.0f - p / 0.08f;
    }
    if (out->flash > 0.0f) {
        mix3(out->horizon, C_FLASH, out->flash * 0.55f, out->horizon);
        mix3(out->zenith, C_FLASH, out->flash * 0.35f, out->zenith);
    }

    /* 天气对实体光照的衰减 + 夜间微光 + 闪光提亮 */
    out->light_scale = (1.0f - dim * 0.55f) * (1.0f + night * 0.30f) + out->flash * 0.25f;

    out->light_scale *= 1.0f-eclipse*.86f;
    out->glow_amt *= 1.0f-eclipse;
    out->daylight = daylight;
    out->dusk = dusk;
    out->night = night;
}

void qlcg_sky_climate(const CalendarClock *clk, int utc_offset_min, int season,
    const WeatherProfile *wp, bool atmosphere, float sim_time, float precip_override, SkyClimate *out) {
    qlcg_sky_climate_astra(clk, utc_offset_min, season, wp, atmosphere, sim_time, precip_override, NULL, out);
}

/* 天气能见度换算线性雾距，对齐 base */
void qlcg_fog_range(float view_m, const WeatherProfile *wp, bool atmosphere,
                    float *near_m, float *far_m) {
    float vis = (atmosphere && wp) ? qlcg_clampf(wp->fog, 0.05f, 1.0f) : 1.0f;
    float n = view_m * 0.55f * vis;
    float f = view_m * 0.95f * vis;
    if (f < n + 1.0f) f = n + 1.0f;
    if (near_m) *near_m = n;
    if (far_m)  *far_m  = f;
}
