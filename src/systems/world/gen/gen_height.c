/* 高度包络：三档列数×七档尺度对数插值，按活动度收敛 */
#include "gen_height.h"
#include "../../../core/config.h"
#include "../../../core/math_util.h"
#include <math.h>

#define GEN_HEIGHT_BASE 3800.0f
#define GEN_HEIGHT_EXP  1.504f

/* 尺度档（每格多少区块） */
static const float HEIGHT_STOPS[7] = {64.0f, 100.0f, 160.0f, 250.0f, 360.0f, 480.0f, 625.0f};

/* 三张天花板表，按列数分档 */
static const float CEIL_256[7]  = {4096.0f, 4096.0f, 4096.0f,  4096.0f,  4096.0f,  4096.0f,  4096.0f};
static const float CEIL_512[7]  = {4096.0f, 4229.0f, 4374.0f,  4516.0f,  5942.0f,  7378.0f,  9000.0f};
static const float CEIL_1024[7] = {4096.0f, 5546.0f, 7632.0f, 10334.0f, 12414.0f, 14347.0f, 16384.0f};

static float log_mix(float a, float b, float t) {
    return expf(logf(a) + (logf(b) - logf(a)) * t);
}

/* 按每格区块数在同档两列之间取对数插值 */
static float ceiling_at(const float *values, float chunks) {
    if (chunks <= HEIGHT_STOPS[0]) return values[0];
    if (chunks >= HEIGHT_STOPS[6]) return values[6];
    int i = 0;
    while (chunks > HEIGHT_STOPS[i + 1]) i++;
    float t = logf(chunks / HEIGHT_STOPS[i]) / logf(HEIGHT_STOPS[i + 1] / HEIGHT_STOPS[i]);
    return log_mix(values[i], values[i + 1], t);
}

float qlcg_terrain_height_cap(const WorldCreatorOptions *opt) {
    if (!opt || opt->columns <= 0) return GEN_HEIGHT_BASE;
    float units = (float)opt->equator_chunks * (float)CHUNK_SIZE / (float)opt->columns / 64.0f;

    int lo, hi;
    float t;
    if (opt->columns <= 256)      { lo = hi = 256;  t = 0.0f; }
    else if (opt->columns >= 1024) { lo = hi = 1024; t = 0.0f; }
    else if (opt->columns <= 512) {
        lo = 256; hi = 512;
        t = logf((float)opt->columns / 256.0f) / logf(2.0f);
    } else {
        lo = 512; hi = 1024;
        t = logf((float)opt->columns / 512.0f) / logf(2.0f);
    }

    const float *tab_lo = (lo == 256) ? CEIL_256 : (lo == 512 ? CEIL_512 : CEIL_1024);
    const float *tab_hi = (hi == 256) ? CEIL_256 : (hi == 512 ? CEIL_512 : CEIL_1024);
    float low  = ceiling_at(tab_lo, units);
    float high = ceiling_at(tab_hi, units);
    float ceiling = (lo == hi) ? low : log_mix(low, high, t);

    float act = qlcg_clampf((float)opt->tectonic_activity, 0.0f, 9.0f) / 9.0f;
    return GEN_HEIGHT_BASE + (ceiling - GEN_HEIGHT_BASE) * powf(act, GEN_HEIGHT_EXP);
}

float qlcg_height_scale_for(const WorldCreatorOptions *opt) {
    return qlcg_terrain_height_cap(opt) / GEN_HEIGHT_BASE;
}
