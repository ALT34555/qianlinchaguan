/* 植物物种表与生成定义实现 */
#include "plant_def.h"
#include "lp_math.h"
#include "model_json.h"
#include "../../../core/config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- 常量表 ---------- */

typedef struct { const char *key; double v; } PdNum;
typedef struct { const char *name; const PdNum *pairs; int count; } PdNumTable;

#define N(k, v) { k, v }

static const PdNum T_CONIFER[]  = { N("height",12), N("trunk",2.6), N("canopy",3.1), N("layers",5), N("sides",6), N("jitter",0.14), N("layerStep",0.82), N("spikeCount",1) };
static const PdNum T_BROADLEAF[]= { N("height",10), N("trunk",4.6), N("canopy",3.6), N("blobs",7), N("sides",6), N("jitter",0.18), N("limbCount",3), N("branchAngle",0.75), N("canopyRatio",0.42) };
static const PdNum T_PALM[]     = { N("height",11), N("trunk",9.4), N("canopy",4.6), N("fronds",9), N("frondLength",4.6), N("droop",0.55), N("segments",7), N("sides",6), N("lean",0.16) };
static const PdNum T_ACACIA[]   = { N("height",8), N("trunk",4.1), N("canopy",4), N("limbCount",4), N("sides",6), N("jitter",0.16), N("branchAngle",0.62) };
static const PdNum T_BAOBAB[]   = { N("height",9), N("trunk",5), N("trunkRadius",1.17), N("canopy",3), N("limbCount",6), N("sides",6), N("jitter",0.2), N("branchAngle",0.85) };
static const PdNum T_SHRUB[]    = { N("height",1.6), N("canopy",0.9), N("blobs",4), N("sides",6), N("jitter",0.22), N("stems",3), N("aspect",1.15) };
static const PdNum T_SUCCULENT[]= { N("height",1.2), N("layers",3), N("blobs",5), N("spread",0.6), N("bloomCount",3) };
static const PdNum T_BRAMBLE[]  = { N("height",1.25), N("stems",6), N("blobs",4), N("spread",1.1), N("sides",5), N("jitter",0.3), N("thorns",1), N("bloomCount",0) };
static const PdNum T_GRASSCLUMP[]={N("height",0.72), N("blades",7), N("spread",0.36), N("petals",2) };
static const PdNum T_SAPLING[]  = { N("height",1.1), N("trunk",0.55), N("canopy",0.3), N("blobs",3) };
static const PdNum T_SPROUT[]   = { N("height",0.5), N("trunk",0.26), N("canopy",0.16), N("blobs",2), N("sides",4), N("jitter",0.16) };
static const PdNum T_WITHERED[] = { N("height",4.5), N("trunk",1.9), N("trunkRadius",0.2), N("limbCount",5), N("sides",4), N("jitter",0.24), N("blobs",4) };
static const PdNum T_BLOSSOM[]  = { N("height",3.4), N("trunk",1.2), N("trunkRadius",0.16), N("limbCount",5), N("blobs",4), N("sides",5), N("jitter",0.22), N("lean",0.18), N("petals",5) };
static const PdNum T_BAMBOO[]   = { N("height",6), N("culms",4), N("trunkRadius",0.12), N("spread",1), N("nodeStep",1.1), N("blades",14), N("frondLength",1), N("sides",5), N("jitter",0.1), N("lean",0.08), N("petals",3) };
static const PdNum T_ORCHID[]   = { N("height",0.6), N("blades",9), N("frondLength",0.66), N("spread",0.2), N("bloomCount",5), N("sides",5), N("petals",6) };
static const PdNum T_REED[]     = { N("height",2.8), N("culms",6), N("spread",0.6), N("nodeStep",0.55), N("blades",5), N("bloomCount",4), N("petals",6) };
static const PdNum T_WATERWEED[]= { N("height",0.9), N("blades",10), N("spread",0.4) };
static const PdNum T_FLOATING[] = { N("height",0.35), N("blobs",7), N("spread",0.65), N("bloomCount",2), N("petals",5) };

static const PdNumTable ARCHETYPE_DEFAULTS[] = {
    { "conifer", T_CONIFER, 8 }, { "broadleaf", T_BROADLEAF, 9 }, { "palm", T_PALM, 9 },
    { "acacia", T_ACACIA, 7 }, { "baobab", T_BAOBAB, 8 }, { "shrub", T_SHRUB, 7 },
    { "succulent", T_SUCCULENT, 5 }, { "bramble", T_BRAMBLE, 8 }, { "grassClump", T_GRASSCLUMP, 4 },
    { "sapling", T_SAPLING, 4 }, { "sprout", T_SPROUT, 6 }, { "withered", T_WITHERED, 7 },
    { "blossomTree", T_BLOSSOM, 9 }, { "bamboo", T_BAMBOO, 11 }, { "orchid", T_ORCHID, 7 },
    { "reed", T_REED, 7 }, { "waterweed", T_WATERWEED, 3 }, { "floatingPad", T_FLOATING, 5 },
};

static const PdNum C_TROPICAL[]    = { N("height",13), N("canopy",4.4), N("droop",0.6), N("layers",6), N("blobs",9) };
static const PdNum C_SUBTROPICAL[] = { N("height",11.5), N("canopy",3.9), N("layers",5), N("blobs",8) };
static const PdNum C_TEMPERATE[]   = { N("height",10), N("canopy",3.6), N("layers",5), N("blobs",7) };
static const PdNum C_COLD[]        = { N("height",8.5), N("canopy",2.7), N("layers",4), N("blobs",5), N("droop",0.7) };

static const PdNumTable CLIMATE_DEFAULTS[] = {
    { "tropical", C_TROPICAL, 5 }, { "subtropical", C_SUBTROPICAL, 5 },
    { "temperate", C_TEMPERATE, 5 }, { "cold", C_COLD, 5 },
};

static const PdNum GLOBAL_DEFAULTS[] = { N("sides",5), N("jitter",0.15), N("blobJitter",0.2), N("sizeScale",1) };

static const char *const SCALED_PARAM_KEYS[] = {
    "height", "trunk", "trunkRadius", "trunkTopRadius", "limbLength", "canopy", "layerRadius",
    "frondLength", "nodeStep", "spread", "bloomRadius", "fruitRadius", "snow",
};
#define SCALED_KEY_COUNT ((int)(sizeof(SCALED_PARAM_KEYS) / sizeof(SCALED_PARAM_KEYS[0])))

static const char *const WITHERED_PALETTE_KEYS[] = { "withered", "witheredDark", "witheredStem", "witheredNeedle" };

static const char *const BLOOM_ARCHETYPES[] = {
    "broadleaf", "palm", "acacia", "baobab", "shrub", "succulent", "bramble", "grassClump",
    "sapling", "blossomTree", "bamboo", "orchid", "conifer", "reed", "waterweed", "floatingPad",
};
static const char *const FRUIT_ARCHETYPES[] = {
    "broadleaf", "palm", "acacia", "baobab", "shrub", "succulent", "bramble", "grassClump",
    "reed", "waterweed", "floatingPad",
};

static const char *const APK_CONIFER[]  = { "bark","barkWarm","foliage","foliageDark","pollen","snow" };
static const char *const APK_BROADLEAF[]= { "bark","barkLight","barkDark","foliage","foliageDark","canopy","bloom","pollen","fruit","snow" };
static const char *const APK_PALM[]     = { "bark","barkLight","foliageDark","palmFrond","palmFrondDry","bloom","pollen","fruit","snow" };
static const char *const APK_ACACIA[]   = { "bark","barkLight","foliage","foliageDark","bloom","pollen","fruit","snow" };
static const char *const APK_BAOBAB[]   = { "bark","barkLight","foliage","foliageDark","bloom","pollen","fruit","snow" };
static const char *const APK_SHRUB[]    = { "barkLight","foliage","foliageDark","bloom","pollen","fruit","snow" };
static const char *const APK_SUCCULENT[]= { "foliage","foliageDark","thorn","bloom","pollen","snow" };
static const char *const APK_BRAMBLE[]  = { "barkLight","foliage","foliageDark","thorn","bloom","pollen","fruit","snow" };
static const char *const APK_GRASSCLUMP[]={ "foliage","foliageDark","bloom","pollen","fruit","grain","grassDry","snow" };
static const char *const APK_SAPLING[]  = { "barkLight","foliage","foliageDark","bloom","pollen","snow" };
static const char *const APK_SPROUT[]   = { "barkLight","foliage","foliageDark","bloom","pollen","fruit","snow" };
static const char *const APK_WITHERED[] = { "barkLight","foliage","foliageDark","withered","witheredDark","witheredStem","witheredNeedle" };
static const char *const APK_BLOSSOM[]  = { "barkDark","barkLight","foliage","foliageDark","bloom","pollen","snow" };
static const char *const APK_BAMBOO[]   = { "bamboo","bambooCulm","foliageDeep","bloom","pollen","snow" };
static const char *const APK_ORCHID[]   = { "bamboo","foliage","foliageDeep","bloom","pollen" };
static const char *const APK_REED[]     = { "reedCulm","reedPlume","foliage","foliageDark","foliageDeep","bloom","pollen","snow","withered","witheredStem" };
static const char *const APK_WATERWEED[]= { "waterweed","waterweedPale","foliage","foliageDark","foliageDeep","spadix","bloom","pollen","fruit","soil","snow" };
static const char *const APK_FLOATING[] = { "foliage","foliageDark","foliageDeep","bloom","bloomGold","pollen","soil","snow" };

#define APK(n, arr) { n, arr, (int)(sizeof(arr) / sizeof(arr[0])) }
typedef struct { const char *name; const char *const *keys; int count; } PdKeyList;
static const PdKeyList ARCHETYPE_PALETTE_KEYS[] = {
    APK("conifer", APK_CONIFER), APK("broadleaf", APK_BROADLEAF), APK("palm", APK_PALM),
    APK("acacia", APK_ACACIA), APK("baobab", APK_BAOBAB), APK("shrub", APK_SHRUB),
    APK("succulent", APK_SUCCULENT), APK("bramble", APK_BRAMBLE), APK("grassClump", APK_GRASSCLUMP),
    APK("sapling", APK_SAPLING), APK("sprout", APK_SPROUT), APK("withered", APK_WITHERED),
    APK("blossomTree", APK_BLOSSOM), APK("bamboo", APK_BAMBOO), APK("orchid", APK_ORCHID),
    APK("reed", APK_REED), APK("waterweed", APK_WATERWEED), APK("floatingPad", APK_FLOATING),
};

/* 状态派生：archetype / 尺寸倍率 / 参数补丁 */
enum { BM_NONE = 0, BM_SPECIES, BM_FORCE };
typedef struct {
    const char *archetype;
    const char *state_form;
    double      param_scale;
    int         has_scale;
    const PdNum *patch;
    int         patch_count;
    int         bloom_mode;
    int         fruit_mode;
    int         modeless;
    const char *fallback;
} PdStateRule;

static const PdNum SP_SEEDLING[] = { N("trunkRadius",0.85), N("blobs",2), N("canopyRatio",0.55) };
static const PdNum SP_SUBADULT[] = { N("fruitScale",0.7) };
static const PdNum SP_FLOWERING[]= { N("bloomCount",7) };
static const PdNum SP_FRUITING[] = { N("fruitCount",4), N("fruitScale",1.18) };
static const PdNum SP_WITHERED[] = { N("snow",0) };

static const PdStateRule STATE_RULES[PD_STATE_COUNT] = {
    /* planted */   { NULL, NULL, 1.0, 0, NULL, 0, BM_NONE, BM_NONE, 1, "normal" },
    /* normal */    { NULL, NULL, 1.0, 0, NULL, 0, BM_SPECIES, BM_SPECIES, 0, NULL },
    /* seedling */  { "sprout", "sprout", 0.24, 1, SP_SEEDLING, 3, BM_NONE, BM_NONE, 0, NULL },
    /* subadult */  { NULL, NULL, 0.55, 1, SP_SUBADULT, 1, BM_NONE, BM_NONE, 0, NULL },
    /* flowering */ { NULL, NULL, 1.0, 0, SP_FLOWERING, 1, BM_FORCE, BM_NONE, 0, NULL },
    /* fruiting */  { NULL, NULL, 1.0, 0, SP_FRUITING, 2, BM_NONE, BM_FORCE, 0, NULL },
    /* withered */  { "withered", "withered", 0.82, 1, SP_WITHERED, 1, BM_NONE, BM_NONE, 0, NULL },
};

static const char *STATE_NAMES[PD_STATE_COUNT] = {
    "planted", "normal", "seedling", "subadult", "flowering", "fruiting", "withered"
};
static const char *SEASON_NAMES[PD_SEASON_COUNT] = { "spring", "summer", "autumn", "winter" };

const char *qlcg_pd_state_name(int state) {
    if (state < 0 || state >= PD_STATE_COUNT) return "normal";
    return STATE_NAMES[state];
}

int qlcg_pd_state_of(const char *name) {
    if (!name) return PD_STATE_NORMAL;
    for (int i = 0; i < PD_STATE_COUNT; i++) if (strcmp(name, STATE_NAMES[i]) == 0) return i;
    return PD_STATE_NORMAL;
}

const char *qlcg_pd_season_name(int season) {
    if (season < 0 || season >= PD_SEASON_COUNT) return "summer";
    return SEASON_NAMES[season];
}

/* ---------- 色板 ---------- */

typedef struct {
    char *key;
    char *def;
    char *season[PD_SEASON_COUNT];
} PdPaletteEntry;

static PdPaletteEntry *g_pal = NULL;
static int g_pal_count = 0;
static int g_pal_cap = 0;

static PdPaletteEntry *pal_find(const char *key) {
    if (!key) return NULL;
    for (int i = 0; i < g_pal_count; i++)
        if (strcmp(g_pal[i].key, key) == 0) return &g_pal[i];
    return NULL;
}

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *out = (char *)malloc(n);
    if (out) memcpy(out, s, n);
    return out;
}

static void pal_add(const JsVal *obj, const char *group) {
    (void)group;
    if (!obj || obj->type != JS_OBJ) return;
    for (int i = 0; i < obj->count; i++) {
        const char *key = obj->keys[i];
        const JsVal *entry = obj->items[i];
        if (!key || !entry) continue;
        if (pal_find(key)) continue;
        if (g_pal_count >= g_pal_cap) {
            int next = g_pal_cap ? g_pal_cap * 2 : 32;
            PdPaletteEntry *np = (PdPaletteEntry *)realloc(g_pal, (size_t)next * sizeof(PdPaletteEntry));
            if (!np) return;
            g_pal = np;
            g_pal_cap = next;
        }
        PdPaletteEntry *e = &g_pal[g_pal_count++];
        memset(e, 0, sizeof(*e));
        e->key = dup_str(key);
        e->def = dup_str(qlcg_js_str(qlcg_js_get(entry, "default"), "#000000"));
        for (int s = 0; s < PD_SEASON_COUNT; s++) {
            const char *v = qlcg_js_str(qlcg_js_get(entry, SEASON_NAMES[s]), NULL);
            if (v) e->season[s] = dup_str(v);
        }
    }
}

static const char *pal_value(const PdPaletteEntry *e, int season) {
    if (!e) return NULL;
    if (season >= 0 && season < PD_SEASON_COUNT && e->season[season]) return e->season[season];
    return e->def;
}

/* ---------- 物种表 ---------- */

static PdSpecies *g_species = NULL;
static int g_species_count = 0;
static int g_species_cap = 0;

int qlcg_pd_species_count(void) { return g_species_count; }

const PdSpecies *qlcg_pd_species(int i) {
    if (i < 0 || i >= g_species_count) return NULL;
    return &g_species[i];
}

const PdSpecies *qlcg_pd_species_by_id(const char *id) {
    if (!id) return NULL;
    for (int i = 0; i < g_species_count; i++)
        if (g_species[i].base.id && strcmp(g_species[i].base.id, id) == 0) return &g_species[i];
    return NULL;
}

/* ---------- 六类分类 ---------- */

static const char *const CATEGORY_KEYS[PLANT_CAT_COUNT] = {
    "cao", "guan", "hua", "nong", "shu", "shui"
};

int qlcg_pd_category_code(const char *key) {
    if (!key) return -1;
    for (int i = 0; i < PLANT_CAT_COUNT; i++)
        if (strcmp(CATEGORY_KEYS[i], key) == 0) return i;
    return -1;
}

int qlcg_pd_has_tag(const PdSpecies *sp, const char *tag) {
    if (!sp || !tag) return 0;
    for (int i = 0; i < sp->base.tag_count; i++)
        if (sp->base.tags[i] && strcmp(sp->base.tags[i], tag) == 0) return 1;
    return 0;
}

int qlcg_pd_tag_level(const PdSpecies *sp, const char *prefix) {
    if (!sp || !prefix) return -1;
    size_t n = strlen(prefix);
    for (int i = 0; i < sp->base.tag_count; i++) {
        const char *tag = sp->base.tags[i];
        if (!tag || strlen(tag) != n + 2 || strncmp(tag, prefix, n) != 0 || tag[n] != '_')
            continue;
        if (tag[n + 1] < '0' || tag[n + 1] > '3') continue;
        return tag[n + 1] - '0';
    }
    return -1;
}

/* 由 id 前缀取类别码 */
static int category_from_id(const char *id) {
    if (!id) return -1;
    const char *dot = strchr(id, '.');
    if (!dot || dot == id) return -1;
    char key[16];
    size_t n = (size_t)(dot - id);
    if (n >= sizeof(key)) n = sizeof(key) - 1;
    memcpy(key, id, n);
    key[n] = '\0';
    return qlcg_pd_category_code(key);
}

static void variant_free(PdVariant *v) {
    free(v->id); free(v->name); free(v->latin); free(v->climate); free(v->archetype);
    pd_free(&v->params);
    pd_free(&v->palette);
    for (int i = 0; i < v->tag_count; i++) free(v->tags[i]);
    free(v->tags);
    memset(v, 0, sizeof(*v));
}

static void variant_copy(PdVariant *dst, const PdVariant *src) {
    memset(dst, 0, sizeof(*dst));
    dst->id = dup_str(src->id);
    dst->name = dup_str(src->name);
    dst->latin = dup_str(src->latin);
    dst->climate = dup_str(src->climate);
    dst->archetype = dup_str(src->archetype);
    dst->seed = src->seed;
    dst->has_seed = src->has_seed;
    dst->has_tags = src->has_tags;
    dst->scale = src->scale;
    pd_init(&dst->params);
    pd_merge(&dst->params, &src->params);
    pd_init(&dst->palette);
    pd_merge(&dst->palette, &src->palette);
    if (src->tag_count > 0) {
        dst->tags = (char **)malloc((size_t)src->tag_count * sizeof(char *));
        if (dst->tags) {
            for (int i = 0; i < src->tag_count; i++) dst->tags[i] = dup_str(src->tags[i]);
            dst->tag_count = src->tag_count;
        }
    }
}

/* 读取一个 variant 对象（成株或状态条目） */
static void read_variant(const JsVal *src, const char *file, PdVariant *out) {
    memset(out, 0, sizeof(*out));
    pd_init(&out->params);
    pd_init(&out->palette);
    const char *id = qlcg_js_str(qlcg_js_get(src, "id"), NULL);
    out->id = dup_str(id ? id : "unknown");
    out->name = dup_str(qlcg_js_str(qlcg_js_get(src, "name"), NULL));
    out->latin = dup_str(qlcg_js_str(qlcg_js_get(src, "latin"), NULL));
    out->climate = dup_str(qlcg_js_str(qlcg_js_get(src, "climate"), NULL));
    out->archetype = dup_str(qlcg_js_str(qlcg_js_get(src, "archetype"), NULL));
    const JsVal *seed_v = qlcg_js_get(src, "seed");
    out->has_seed = seed_v != NULL && seed_v->type == JS_NUM;
    out->seed = qlcg_js_num(seed_v, 0.0);
    out->scale = qlcg_js_num(qlcg_js_get(src, "scale"), 1.0);
    (void)file;

    const JsVal *params = qlcg_js_get(src, "params");
    if (params && params->type == JS_OBJ) {
        for (int i = 0; i < params->count; i++) {
            const char *k = params->keys[i];
            const JsVal *v = params->items[i];
            if (!k || !v) continue;
            if (v->type == JS_NUM) pd_set_num(&out->params, k, v->num);
            else if (v->type == JS_BOOL) pd_set_num(&out->params, k, v->bval ? 1.0 : 0.0);
            else if (v->type == JS_STR) pd_set_str(&out->params, k, v->str);
            else if (v->type == JS_ARR) {
                /* 字符串数组压成逗号串，如 snowOn */
                char buf[256];
                size_t at = 0;
                buf[0] = '\0';
                for (int j = 0; j < v->count && at + 2 < sizeof(buf); j++) {
                    const char *s = qlcg_js_str(v->items[j], NULL);
                    if (!s) continue;
                    if (at) buf[at++] = ',';
                    while (*s && at + 1 < sizeof(buf)) buf[at++] = *s++;
                    buf[at] = '\0';
                }
                pd_set_str(&out->params, k, buf);
            }
        }
    }
    const JsVal *pal = qlcg_js_get(src, "palette");
    if (pal && pal->type == JS_OBJ) {
        for (int i = 0; i < pal->count; i++) {
            const char *k = pal->keys[i];
            const JsVal *v = pal->items[i];
            if (k && v && v->type == JS_STR) pd_set_str(&out->palette, k, v->str);
        }
    }
    const JsVal *tags = qlcg_js_get(src, "tags");
    if (tags && tags->type == JS_ARR) {
        out->has_tags = 1;
        if (tags->count > 0) {
            out->tags = (char **)malloc((size_t)tags->count * sizeof(char *));
            if (out->tags) {
                int n = 0;
                for (int i = 0; i < tags->count; i++) {
                    const char *t = qlcg_js_str(tags->items[i], NULL);
                    if (t) out->tags[n++] = dup_str(t);
                }
                out->tag_count = n;
            }
        }
    }
}

/* 去掉 @状态 后缀 */
static void base_id_of(const char *id, char *out, size_t cap) {
    size_t n = strlen(id);
    size_t cut = n;
    for (size_t i = n; i > 0; i--) {
        if (id[i - 1] == '@') { cut = i - 1; break; }
    }
    if (cut >= cap) cut = cap - 1;
    memcpy(out, id, cut);
    out[cut] = '\0';
}

static PdSpecies *put_species(const char *id) {
    if (g_species_count >= g_species_cap) {
        int next = g_species_cap ? g_species_cap * 2 : 32;
        PdSpecies *ns = (PdSpecies *)realloc(g_species, (size_t)next * sizeof(PdSpecies));
        if (!ns) return NULL;
        g_species = ns;
        g_species_cap = next;
    }
    PdSpecies *sp = &g_species[g_species_count++];
    memset(sp, 0, sizeof(*sp));
    pd_init(&sp->base.params);
    pd_init(&sp->base.palette);
    sp->base.id = dup_str(id);
    sp->category = -1;
    return sp;
}

static int load_plant_file(const char *path) {
    const char *err = NULL;
    JsVal *root = qlcg_js_load(path, &err);
    if (!root) {
        fprintf(stderr, "[plant] %s: %s\n", path, err ? err : "parse error");
        return 0;
    }
    const JsVal *list = qlcg_js_get(root, "plants");
    if (!list || list->type != JS_ARR || list->count == 0) {
        qlcg_js_free(root);
        return 0;
    }
    /* 先成株，再状态条目 */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < list->count; i++) {
            const JsVal *p = list->items[i];
            if (!p || p->type != JS_OBJ) continue;
            const char *st = qlcg_js_str(qlcg_js_get(p, "state"), NULL);
            int is_state = st != NULL;
            if ((pass == 0 && is_state) || (pass == 1 && !is_state)) continue;
            PdVariant v;
            read_variant(p, path, &v);
            if (pass == 0) {
                if (qlcg_pd_species_by_id(v.id)) { variant_free(&v); continue; }
                PdSpecies *sp = put_species(v.id);
                if (!sp) { variant_free(&v); continue; }
                variant_free(&sp->base);
                sp->base = v;
                /* 记录成株声明的状态清单 */
                const JsVal *states = qlcg_js_get(p, "states");
                if (states && states->type == JS_ARR) {
                    for (int k = 0; k < states->count && sp->declared_count < PD_STATE_COUNT; k++) {
                        const char *nm = qlcg_js_str(states->items[k], NULL);
                        if (!nm) continue;
                        if (qlcg_pd_state_of(nm) == PD_STATE_NORMAL && strcmp(nm, "normal") != 0) continue;
                        sp->declared[sp->declared_count++] = dup_str(nm);
                    }
                }
            } else {
                char base_id[128];
                base_id_of(v.id, base_id, sizeof(base_id));
                PdSpecies *sp = (PdSpecies *)qlcg_pd_species_by_id(base_id);
                if (!sp) { variant_free(&v); continue; }
                int code = qlcg_pd_state_of(st);
                if (code <= PD_STATE_PLANTED || sp->has_state[code]) { variant_free(&v); continue; }
                /* 继承成株：字段缺省回填 */
                PdVariant merged;
                variant_copy(&merged, &sp->base);
                if (v.name) { free(merged.name); merged.name = dup_str(v.name); }
                if (v.latin) { free(merged.latin); merged.latin = dup_str(v.latin); }
                if (v.climate) { free(merged.climate); merged.climate = dup_str(v.climate); }
                if (v.archetype) { free(merged.archetype); merged.archetype = dup_str(v.archetype); }
                if (v.has_seed) merged.seed = v.seed;
                pd_merge(&merged.params, &v.params);
                pd_merge(&merged.palette, &v.palette);
                if (v.has_tags) {
                    for (int t = 0; t < merged.tag_count; t++) free(merged.tags[t]);
                    free(merged.tags);
                    merged.tags = v.tags;
                    merged.tag_count = v.tag_count;
                    v.tags = NULL;
                    v.tag_count = 0;
                }
                free(merged.id);
                merged.id = dup_str(v.id);
                variant_free(&v);
                sp->state[code] = merged;
                sp->has_state[code] = 1;
            }
        }
    }
    qlcg_js_free(root);
    return 1;
}

/* 从 plants.json 读取文件清单 */
static char **g_files = NULL;
static int g_file_count = 0;

static void add_file(const char *rel) {
    if (!rel) return;
    for (int i = 0; i < g_file_count; i++) if (strcmp(g_files[i], rel) == 0) return;
    char **nf = (char **)realloc(g_files, (size_t)(g_file_count + 1) * sizeof(char *));
    if (!nf) return;
    g_files = nf;
    g_files[g_file_count++] = dup_str(rel);
}

/* 读 categories.json 并写入物种分类 */
static void load_categories(const char *dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/categories.json", dir);
    const char *err = NULL;
    JsVal *root = qlcg_js_load(path, &err);
    if (!root) {
        fprintf(stderr, "[plant] 分类表缺失 %s\n", path);
        return;
    }
    const JsVal *list = qlcg_js_get(root, "categories");
    if (list && list->type == JS_ARR) {
        for (int i = 0; i < list->count; i++) {
            const JsVal *c = list->items[i];
            if (!c || c->type != JS_OBJ) continue;
            const char *key = qlcg_js_str(qlcg_js_get(c, "id"), NULL);
            int code = qlcg_pd_category_code(key);
            if (code < 0) {
                fprintf(stderr, "[plant] 分类未知: %s\n", key ? key : "?");
                continue;
            }
            const JsVal *ids = qlcg_js_get(c, "species");
            if (!ids || ids->type != JS_ARR) continue;
            for (int k = 0; k < ids->count; k++) {
                const char *sid = qlcg_js_str(ids->items[k], NULL);
                PdSpecies *sp = (PdSpecies *)qlcg_pd_species_by_id(sid);
                if (!sp) {
                    fprintf(stderr, "[plant] 分类物种未知: %s\n", sid ? sid : "?");
                    continue;
                }
                sp->category = code;
            }
        }
    }
    qlcg_js_free(root);
    for (int i = 0; i < g_species_count; i++) {
        PdSpecies *sp = &g_species[i];
        if (sp->category >= 0) continue;
        int prefix = category_from_id(sp->base.id);
        if (prefix >= 0) sp->category = prefix;
        fprintf(stderr, "[plant] 分类表缺项: %s\n", sp->base.id);
    }
}

int qlcg_pd_load(const char *dir) {
    qlcg_pd_unload();
    char path[512];

    snprintf(path, sizeof(path), "%s/palettes.json", dir);
    const char *err = NULL;
    JsVal *pal = qlcg_js_load(path, &err);
    if (!pal) {
        fprintf(stderr, "[plant] 色板缺失 %s: %s\n", path, err ? err : "err");
        return -1;
    }
    pal_add(qlcg_js_get(pal, "bark"), "bark");
    pal_add(qlcg_js_get(pal, "greenery"), "greenery");
    qlcg_js_free(pal);

    snprintf(path, sizeof(path), "%s/plants.json", dir);
    JsVal *index = qlcg_js_load(path, &err);
    if (index) {
        const JsVal *list = qlcg_js_get(index, "species");
        if (list && list->type == JS_ARR) {
            for (int i = 0; i < list->count; i++) {
                const char *rel = qlcg_js_str(qlcg_js_get(list->items[i], "file"), NULL);
                add_file(rel);
            }
        }
        qlcg_js_free(index);
    }
    add_file("additional.json");

    for (int i = 0; i < g_file_count; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, g_files[i]);
        load_plant_file(path);
    }
    load_categories(dir);
    return g_species_count;
}

void qlcg_pd_unload(void) {
    for (int i = 0; i < g_species_count; i++) {
        variant_free(&g_species[i].base);
        for (int s = 0; s < PD_STATE_COUNT; s++) {
            if (g_species[i].has_state[s]) variant_free(&g_species[i].state[s]);
            free(g_species[i].declared[s]);
        }
    }
    free(g_species);
    g_species = NULL;
    g_species_count = 0;
    g_species_cap = 0;
    for (int i = 0; i < g_pal_count; i++) {
        free(g_pal[i].key);
        free(g_pal[i].def);
        for (int s = 0; s < PD_SEASON_COUNT; s++) free(g_pal[i].season[s]);
    }
    free(g_pal);
    g_pal = NULL;
    g_pal_count = 0;
    g_pal_cap = 0;
    for (int i = 0; i < g_file_count; i++) free(g_files[i]);
    free(g_files);
    g_files = NULL;
    g_file_count = 0;
}

/* ---------- 状态派生 ---------- */

static int in_list(const char *const *list, int n, const char *v) {
    if (!v) return 0;
    for (int i = 0; i < n; i++) if (strcmp(list[i], v) == 0) return 1;
    return 0;
}

static int can_bloom(const char *arch) {
    return in_list(BLOOM_ARCHETYPES, (int)(sizeof(BLOOM_ARCHETYPES) / sizeof(BLOOM_ARCHETYPES[0])), arch);
}
static int can_fruit(const char *arch) {
    return in_list(FRUIT_ARCHETYPES, (int)(sizeof(FRUIT_ARCHETYPES) / sizeof(FRUIT_ARCHETYPES[0])), arch);
}

static const PdKeyList *apk_of(const char *arch) {
    if (!arch) return NULL;
    for (int i = 0; i < (int)(sizeof(ARCHETYPE_PALETTE_KEYS) / sizeof(ARCHETYPE_PALETTE_KEYS[0])); i++)
        if (strcmp(ARCHETYPE_PALETTE_KEYS[i].name, arch) == 0) return &ARCHETYPE_PALETTE_KEYS[i];
    return NULL;
}

static const PdNumTable *arch_defaults(const char *arch) {
    if (!arch) return NULL;
    for (int i = 0; i < (int)(sizeof(ARCHETYPE_DEFAULTS) / sizeof(ARCHETYPE_DEFAULTS[0])); i++)
        if (strcmp(ARCHETYPE_DEFAULTS[i].name, arch) == 0) return &ARCHETYPE_DEFAULTS[i];
    return NULL;
}

static const PdNumTable *climate_defaults(const char *climate) {
    if (!climate) return NULL;
    for (int i = 0; i < (int)(sizeof(CLIMATE_DEFAULTS) / sizeof(CLIMATE_DEFAULTS[0])); i++)
        if (strcmp(CLIMATE_DEFAULTS[i].name, climate) == 0) return &CLIMATE_DEFAULTS[i];
    return NULL;
}

/* 状态派生结果 */
typedef struct {
    char   *archetype;
    PdDict  params;
    int     bloom;
    int     fruit;
} PdResolved;

static void resolved_free(PdResolved *r) {
    free(r->archetype);
    pd_free(&r->params);
    memset(r, 0, sizeof(*r));
}

static void resolve_state(const PdVariant *v, int state, PdResolved *out) {
    const PdStateRule *rule = &STATE_RULES[state];
    memset(out, 0, sizeof(*out));
    pd_init(&out->params);
    out->archetype = dup_str(rule->archetype ? rule->archetype : v->archetype);

    pd_merge(&out->params, &v->params);
    pd_set_str(&out->params, "baseArchetype", v->archetype ? v->archetype : "");

    if (rule->has_scale && rule->param_scale != 1.0)
        pd_scale_keys(&out->params, SCALED_PARAM_KEYS, SCALED_KEY_COUNT, rule->param_scale);
    for (int i = 0; i < rule->patch_count; i++)
        pd_set_num(&out->params, rule->patch[i].key, rule->patch[i].v);
    if (rule->state_form)
        pd_set_str(&out->params, "stateForm", rule->state_form);
    if (state == PD_STATE_WITHERED)
        pd_set_num(&out->params, "snow", 0.0);

    int species_bloom = floor(pd_num(&out->params, "bloomCount", 0.0) + 0.5) > 0.0;
    int species_fruit = floor(pd_num(&out->params, "fruitCount", 0.0) + 0.5) > 0.0;
    double flowerless = pd_num(&out->params, "flowerless", 0.0);
    double fruitless = pd_num(&out->params, "fruitless", 0.0);
    out->bloom = (flowerless == 0.0)
        && (rule->bloom_mode == BM_FORCE || (rule->bloom_mode == BM_SPECIES && species_bloom))
        && can_bloom(out->archetype);
    out->fruit = (fruitless == 0.0)
        && (rule->fruit_mode == BM_FORCE || (rule->fruit_mode == BM_SPECIES && species_fruit))
        && can_fruit(out->archetype);
}

static void merge_params(const PdVariant *v, int state, const PdResolved *res, PdDict *out) {
    pd_init(out);
    for (int i = 0; i < (int)(sizeof(GLOBAL_DEFAULTS) / sizeof(GLOBAL_DEFAULTS[0])); i++)
        pd_set_num(out, GLOBAL_DEFAULTS[i].key, GLOBAL_DEFAULTS[i].v);
    const PdNumTable *cl = climate_defaults(v->climate);
    if (cl) for (int i = 0; i < cl->count; i++) pd_set_num(out, cl->pairs[i].key, cl->pairs[i].v);
    const PdNumTable *ad = arch_defaults(res->archetype);
    if (ad) for (int i = 0; i < ad->count; i++) pd_set_num(out, ad->pairs[i].key, ad->pairs[i].v);
    pd_merge(out, &res->params);
    (void)state;
}

/* 色板键集合，保持插入顺序 */
typedef struct { const char *items[64]; int count; } PdKeySet;

static void ks_add(PdKeySet *s, const char *k) {
    if (!k) return;
    for (int i = 0; i < s->count; i++) if (strcmp(s->items[i], k) == 0) return;
    if (s->count < 64) s->items[s->count++] = k;
}

static void resolve_palette(const PdVariant *v, int season, int state, const PdResolved *res, PdDict *out) {
    PdKeySet keys;
    memset(&keys, 0, sizeof(keys));
    const PdKeyList *apk = apk_of(res->archetype);
    if (apk) for (int i = 0; i < apk->count; i++) ks_add(&keys, apk->keys[i]);
    for (int i = 0; i < v->palette.count; i++) {
        const PdEntry *e = &v->palette.e[i];
        if (!e->is_num && e->str) ks_add(&keys, e->str);
    }
    if (res->bloom) ks_add(&keys, "bloom");
    if (res->fruit) ks_add(&keys, "fruit");
    if (state == PD_STATE_WITHERED)
        for (int i = 0; i < (int)(sizeof(WITHERED_PALETTE_KEYS) / sizeof(WITHERED_PALETTE_KEYS[0])); i++)
            ks_add(&keys, WITHERED_PALETTE_KEYS[i]);

    pd_init(out);
    for (int i = 0; i < keys.count; i++) {
        const char *key = keys.items[i];
        char spot_key[128], whole_key[128];
        snprintf(spot_key, sizeof(spot_key), "%s.%s", key, SEASON_NAMES[season]);
        snprintf(whole_key, sizeof(whole_key), "%s", key);
        const char *spot = pd_str(&v->palette, spot_key, NULL);
        const char *whole = pd_str(&v->palette, whole_key, NULL);
        const char *value = NULL;
        if (spot) {
            const PdPaletteEntry *e = pal_find(spot);
            value = pal_value(e, season);
        } else if (whole) {
            const PdPaletteEntry *e = pal_find(whole);
            value = pal_value(e, season);
        } else {
            const PdPaletteEntry *e = pal_find(key);
            value = pal_value(e, season);
        }
        if (value) pd_set_str(out, key, value);
    }
}

/* ---------- 生成定义 ---------- */

static void format_seed(double seed, char *buf, size_t cap) {
    if (seed == floor(seed) && fabs(seed) < 1e15) snprintf(buf, cap, "%.0f", seed);
    else snprintf(buf, cap, "%g", seed);
}

void qlcg_pd_free_def(PdDef *def) {
    if (!def) return;
    free(def->id); free(def->archetype); free(def->base_archetype);
    free(def->climate); free(def->name);
    pd_free(&def->params);
    pd_free(&def->palette);
    memset(def, 0, sizeof(*def));
}

/* 物种声明清单是否含该状态；空清单视为六态俱全 */
static int state_allowed(const PdSpecies *sp, int state) {
    if (state == PD_STATE_NORMAL) return 1;
    if (sp->declared_count == 0) return state < PD_STATE_COUNT;
    for (int i = 0; i < sp->declared_count; i++)
        if (sp->declared[i] && strcmp(sp->declared[i], STATE_NAMES[state]) == 0) return 1;
    return 0;
}

int qlcg_pd_build_def(const char *id, int season, PdDef *out) {
    return qlcg_pd_build_def_ex(id, season, 0, out);
}

int qlcg_pd_build_def_ex(const char *id, int season, int force_normal, PdDef *out) {    memset(out, 0, sizeof(*out));
    if (!id || season < 0 || season >= PD_SEASON_COUNT) return 0;

    /* 拆分 @状态 后缀 */
    char base_id[128];
    base_id_of(id, base_id, sizeof(base_id));
    const char *suffix = id + strlen(base_id);
    if (*suffix == '@') suffix++; else suffix = "";

    int state = PD_STATE_NORMAL;
    if (*suffix) {
        int s = qlcg_pd_state_of(suffix);
        if (s > PD_STATE_PLANTED) state = s;
    }

    const PdSpecies *sp = qlcg_pd_species_by_id(base_id);
    if (!sp) return 0;

    const PdVariant *variant = (state == PD_STATE_NORMAL) ? &sp->base
                             : (sp->has_state[state] ? &sp->state[state] : &sp->base);
    int declared = force_normal ? PD_STATE_NORMAL
                 : (state_allowed(sp, state) ? state : PD_STATE_NORMAL);

    /* 同 toPlantDef：无花/果时退回普通态 */
    PdResolved res;
    resolve_state(variant, declared, &res);
    int seeds = sp->category == qlcg_pd_category_code("hua") && pd_num(&res.params, "seedCount", 0) > 0;
    int effective = declared;
    if (declared == PD_STATE_FLOWERING && !res.bloom) effective = PD_STATE_NORMAL;
    else if (declared == PD_STATE_FRUITING && !res.fruit && !seeds) effective = PD_STATE_NORMAL;
    if (effective != declared) {
        resolved_free(&res);
        resolve_state(variant, effective, &res);
    }

    PdDict params;
    merge_params(variant, effective, &res, &params);
    PdDict palette;
    resolve_palette(variant, season, effective, &res, &palette);

    out->id = dup_str(variant->id);
    out->archetype = dup_str(res.archetype);
    out->base_archetype = dup_str(sp->base.archetype);
    out->climate = dup_str(variant->climate);
    out->name = dup_str(variant->name);
    out->seed = variant->seed;
    out->params = params;
    out->palette = palette;
    out->category = sp->category;
    out->state = effective;
    out->scale = variant->scale;
    out->tags = variant->tags;
    out->tag_count = variant->tag_count;
    out->snow = ((season == PD_SEASON_WINTER || season == PD_SEASON_AUTUMN) && effective != PD_STATE_WITHERED);
    out->bloom = res.bloom && can_bloom(res.archetype);
    out->fruit = res.fruit && !seeds;

    /* archetype:baseId:seed 哈希 */
    char seed_buf[64];
    char key[256];
    format_seed(variant->seed, seed_buf, sizeof(seed_buf));
    snprintf(key, sizeof(key), "%s:%s:%s", res.archetype ? res.archetype : "", base_id, seed_buf);
    out->build_seed = qlcg_lp_hash_string(key);

    resolved_free(&res);
    return 1;
}
