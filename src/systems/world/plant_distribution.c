/* 自然植物分布：基质、气候与 tag 适宜度 */
#include "plant_distribution.h"
#include "planet_macro.h"
#include "model/plant_def.h"
#include "model/plant_material.h"
#include "model/shore_models.h"
#include "gen/gen_noise.h"
#include "../../core/math_util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int   category;
    int   zone;
    float height;
    float temp_center;
    float temp_sigma;
    float cold, heat, salt, dry, wetlog;
    float anch, canopy, floating, submerged;
    int marine, fresh;
    float wet_pref, wet_sigma;
    float slope_sigma;
    float substrate[5];
} PlantProfile;

static PlantProfile *g_profile = NULL;
static int           g_profile_count = 0;
static const PdSpecies *g_profile_first = NULL;
static int g_shore_species[SHORE_PLANT_COUNT], g_shore_count;

/* 四档气候带中心与基础宽度 */
static const float ZONE_CENTER[4] = { 1.0f, 11.0f, 20.0f, 27.0f };
static const float ZONE_SIGMA[4]  = { 9.0f, 8.0f, 7.5f, 6.5f };
static const float BASE_WET[4]    = { .55f, .52f, .60f, .65f };

/* 选种候选上限 */
#define PD_PICK_MAX 256

static float clamp01(float v) {
    return qlcg_clampf(v, 0.0f, 1.0f);
}

static float smooth01(float a, float b, float v) {
    float t = clamp01((v - a) / ((b - a) != 0.0f ? (b - a) : 1.0f));
    return t * t * (3.0f - 2.0f * t);
}

static float gauss(float v, float mu, float sigma) {
    float d = v - mu;
    sigma = fmaxf(sigma, 0.05f);
    return expf(-0.5f * (d / sigma) * (d / sigma));
}

static int zone_of(const char *climate) {
    if (!climate) return 1;
    if (strcmp(climate, "cold") == 0) return 0;
    if (strcmp(climate, "temperate") == 0) return 1;
    if (strcmp(climate, "subtropical") == 0) return 2;
    if (strcmp(climate, "tropical") == 0) return 3;
    return 1;
}

static float tag_power(const PdSpecies *sp, const char *prefix) {
    int level = qlcg_pd_tag_level(sp, prefix);
    return level < 0 ? 0.0f : 1.0f - 0.25f * (float)level;
}

static int has_tag(const PdSpecies *sp, const char *tag) {
    return qlcg_pd_has_tag(sp, tag) ? 1 : 0;
}

static void build_profile(const PdSpecies *sp, PlantProfile *p) {
    memset(p, 0, sizeof(*p));
    p->category = sp->category;
    p->zone = zone_of(sp->base.climate);
    p->height = (float)pd_num(&sp->base.params, "height", 1.0);
    if (p->height < 0.05f) p->height = 0.05f;

    p->temp_center = ZONE_CENTER[p->zone];
    p->temp_sigma  = ZONE_SIGMA[p->zone];
    p->cold   = tag_power(sp, "naihan");
    p->heat   = tag_power(sp, "naire");
    p->salt   = tag_power(sp, "naiyan");
    p->dry    = tag_power(sp, "naigan");
    p->wetlog = tag_power(sp, "nailao");
    p->anch    = has_tag(sp, "guzhuo");
    p->canopy  = has_tag(sp, "shuguan");
    p->floating = has_tag(sp, "fushui");
    p->submerged = has_tag(sp, "chenshui");
    p->marine = has_tag(sp, "haiyang");
    p->fresh = has_tag(sp, "danshui");

    p->wet_pref  = BASE_WET[p->zone] - .26f * p->dry + .12f * p->wetlog;
    p->wet_sigma = .20f + .16f * p->dry + .14f * p->wetlog;

    float low = 1.0f - smooth01(.8f, 3.0f, p->height);
    p->slope_sigma = .70f + .40f * (1.0f - smooth01(2.0f, 6.0f, p->height))
                   + .25f * p->anch;

    p->substrate[0] = clamp01(.15f + .55f * low * (.5f + .5f * p->cold)
                            + .15f * p->anch);
    p->substrate[1] = clamp01(.30f + .20f * p->wetlog + .20f * p->canopy
                            + .25f * smooth01(1.0f, 8.0f, p->height));
    p->substrate[2] = clamp01(.10f + .80f * p->salt + .25f * p->dry);
    p->substrate[3] = clamp01(.10f + .65f * p->dry + .25f * p->heat
                            + .20f * (p->zone == 3));
    p->substrate[4] = clamp01(.15f + .35f * p->dry + .20f * low);
    for (int i = 0; i < 5; i++) {
        if (p->substrate[i] < .05f) p->substrate[i] = .05f;
    }
}

int qlcg_plant_habitat_prepare(void) {
    int n = qlcg_plant_species_count();
    const PdSpecies *first = n > 0 ? qlcg_pd_species(0) : NULL;
    if (n > 0 && n == g_profile_count && first == g_profile_first && g_profile)
        return n;
    free(g_profile);
    g_profile = NULL;
    g_profile_count = 0;
    g_profile_first = NULL;
    g_shore_count = 0;
    if (n <= 0) return 0;
    g_profile = (PlantProfile *)calloc((size_t)n, sizeof(PlantProfile));
    if (!g_profile) return 0;
    for (int i = 0; i < n; i++) {
        const PdSpecies *sp = qlcg_pd_species(i);
        if (!sp) continue;
        build_profile(sp, &g_profile[i]);
        if (qlcg_shore_plant_index(sp->base.id) >= 0 && g_shore_count < SHORE_PLANT_COUNT)
            g_shore_species[g_shore_count++] = i;
    }
    g_profile_count = n;
    g_profile_first = first;
    return n;
}

int qlcg_plant_pick_shore(const PlantSite *site, uint32_t roll) {
    return qlcg_plant_pick_from(site, roll, g_shore_species, g_shore_count);
}

float qlcg_plant_suitability(int species, const PlantSite *site) {
    if (!site || species < 0 || species >= g_profile_count || !g_profile)
        return 0.0f;
    const PlantProfile *p = &g_profile[species];
    if (p->category < 0) return 0.0f;   /* 类别筛选交给候选池 */
    int material = site->material;
    if (material < 1 || material > 5) return 0.0f;

    float d = site->temperature - p->temp_center;
    float sigma = p->temp_sigma * (d < 0.0f ? 1.0f + .85f * p->cold
                                            : 1.0f + .85f * p->heat);
    float temp_fit = gauss(site->temperature, p->temp_center, sigma);
    if (temp_fit < .0001f) return 0.0f;

    float salt_site = clamp01(site->saline);
    float salt_fit = 1.0f - salt_site * (1.0f - p->salt);

    if (site->water_depth > .15f) {
        if (p->category != PLANT_CAT_SHUI) return 0.0f;
        if (p->marine && site->water_kind != WATER_KIND_SEA) return 0.0f;
        if (p->fresh && (site->water_kind == WATER_KIND_SEA || salt_site >= .5f)) return 0.0f;
        float depth_fit;
        if (p->floating) {
            if (site->water_kind == WATER_KIND_RIVER ||
                site->water_kind == WATER_KIND_FALL) return 0.0f;
            depth_fit = smooth01(.15f, .25f, site->water_depth)
                      * (1.0f - smooth01(1.5f, 2.0f, site->water_depth));
        } else if (p->submerged) {
            depth_fit = p->marine
                ? smooth01(.35f, .75f, site->water_depth) * (1.0f - smooth01(5.8f, 8.0f, site->water_depth))
                : smooth01(.25f, .35f, site->water_depth) * (1.0f - smooth01(1.8f, 2.2f, site->water_depth));
            if (site->flow > 1.2f && !p->anch) return 0.0f;
            if (site->water_kind == WATER_KIND_FALL && !p->anch) return 0.0f;
        } else {
            depth_fit = smooth01(.15f, .22f, site->water_depth)
                      * (1.0f - smooth01(.30f, .35f, site->water_depth));
            if (site->flow > .8f && !p->anch) return 0.0f;
            if (site->water_kind == WATER_KIND_FALL && !p->anch) return 0.0f;
        }
        if (depth_fit <= 0.0f) return 0.0f;
        float substrate_fit = p->substrate[material - 1];
        float score = temp_fit * salt_fit * depth_fit
                    * (.5f + .5f * substrate_fit);
        return clamp01(score);
    }

    if (p->category == PLANT_CAT_SHUI) return 0.0f;

    float wet_eff = clamp01(site->wetness * (1.0f - .35f * site->desert));
    float wet_fit = gauss(wet_eff, p->wet_pref, p->wet_sigma);
    if (wet_fit < .0001f) return 0.0f;

    float substrate_fit = p->substrate[material - 1];
    float slope_fit = gauss(site->slope, 0.0f, p->slope_sigma);
    float tall = smooth01(3.0f, 9.0f, p->height);
    float canopy_site = clamp01(.55f * site->wetness + .55f * site->forest
                              - .55f * site->desert);
    float stratum = .30f + .70f * (tall * canopy_site
                                 + (1.0f - tall) * (1.0f - .65f * canopy_site));
    float score = temp_fit * wet_fit * substrate_fit
                * slope_fit * stratum * salt_fit;
    if (!isfinite(score)) return 0.0f;
    return clamp01(score);
}

/* 群落噪声每个散布格只算一次 */
void qlcg_plant_patch(double x, double z, uint32_t seed, PlantPatch *patch) {
    float broad = (float)qlcg_base_noise_d(x / 96.0, 0, z / 96.0, seed ^ 0x70617431u);
    float fine = (float)qlcg_base_noise_d(x / 24.0, 0, z / 24.0, seed ^ 0x70617432u);
    float stone = (float)qlcg_base_noise_d(x / 40.0, 0, z / 40.0, seed ^ 0x726f636bu);
    patch->density = qlcg_clampf(1.0f + .85f * broad + 1.15f * fine, .20f, 1.80f);
    patch->rocks = qlcg_clampf(1.0f + 1.70f * stone, .15f, 1.70f);
    patch->warp_x = fine * 8.0f;
    patch->warp_z = broad * 12.0f;
    patch->seed = seed;
}

/* 给定候选池按适宜度平方加权选种 */
int qlcg_plant_pick_from(const PlantSite *site, uint32_t roll,
                         const int *pool, int pool_count) {
    if (!site || !g_profile || g_profile_count <= 0) return -1;
    if (!pool) pool_count = g_profile_count;
    if (pool_count <= 0) return -1;
    if (pool_count > PD_PICK_MAX) pool_count = PD_PICK_MAX;

    int cand[PD_PICK_MAX];
    float weight[PD_PICK_MAX];
    int n = 0;
    float total = 0.0f;
    for (int i = 0; i < pool_count; i++) {
        int sp = pool ? pool[i] : i;
        if (sp < 0 || sp >= g_profile_count || qlcg_pd_category_can_hang(g_profile[sp].category)) continue;
        float s = qlcg_plant_suitability(sp, site);
        if (s < .01f) continue;
        cand[n] = sp;
        weight[n] = s * s;
        total += weight[n];
        n++;
    }
    if (n <= 0 || !(total > 1e-8f)) return -1;

    float target = total * (((float)(roll & 0xFFFFu) + .5f) / 65536.0f);
    for (int i = 0; i < n; i++) {
        target -= weight[i];
        if (target <= 0.0f) return cand[i];
    }
    return cand[n - 1];
}

int qlcg_plant_pick(const PlantSite *site, uint32_t roll) {
    int pool[PD_PICK_MAX];
    int n = 0;
    for (int i = 0; i < g_profile_count && n < PD_PICK_MAX; i++) {
        if (g_profile[i].category == PLANT_CAT_HUA ||
            g_profile[i].category == PLANT_CAT_NONG ||
            g_profile[i].category == PLANT_CAT_YAO) continue;
        pool[n++] = i;
    }
    return qlcg_plant_pick_from(site, roll, pool, n);
}
