/* 岩石数据层实现 */
#include "rock_def.h"
#include "model_json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const ROCK_FILES[] = {
    "rock.boulder.json", "rock.block.json", "rock.ledge.json",
    "rock.slab.json", "rock.shard.json", "rock.spire.json",
    "rock.stub.json", "rock.rubble.json", "rock.floe.json",
};

static const char *const SIZE_FILES[ROCK_SIZE_COUNT] = {
    "rock.small.json", "rock.medium.json", "rock.large.json", "rock.giant.json",
    "rock.mega.json",
};

static const char *const SEASON_NAMES[4] = { "spring", "summer", "autumn", "winter" };
static const char *const FORM_NAMES[ROCK_FORM_COUNT] = { "outcrop", "buried", "stacked", "crusted" };
static const char *const MAT_NAMES[ROCK_MAT_COUNT] = { "solid", "moss", "snow", "ice" };

const char *qlcg_rock_form_name(int form) {
    if (form < 0 || form >= ROCK_FORM_COUNT) return "outcrop";
    return FORM_NAMES[form];
}

const char *qlcg_rock_mat_name(int mat) {
    if (mat < 0 || mat >= ROCK_MAT_COUNT) return "solid";
    return MAT_NAMES[mat];
}

int qlcg_rock_mat_index(const char *name) {
    if (!name) return ROCK_MAT_SOLID;
    for (int i = 0; i < ROCK_MAT_COUNT; i++) if (strcmp(name, MAT_NAMES[i]) == 0) return i;
    return ROCK_MAT_SOLID;
}

/* ---------- 色板 ---------- */

typedef struct {
    char *key;
    char *def;
    char *season[4];
} RockPal;

static RockPal *g_pal = NULL;
static int g_pal_count = 0;
static int g_pal_cap = 0;

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *out = (char *)malloc(n);
    if (out) memcpy(out, s, n);
    return out;
}

static RockPal *pal_find(const char *key) {
    if (!key) return NULL;
    for (int i = 0; i < g_pal_count; i++)
        if (strcmp(g_pal[i].key, key) == 0) return &g_pal[i];
    return NULL;
}

static void pal_add_group(const JsVal *group) {
    if (!group || group->type != JS_OBJ) return;
    for (int i = 0; i < group->count; i++) {
        const char *key = group->keys[i];
        const JsVal *e = group->items[i];
        if (!key || !e || pal_find(key)) continue;
        if (g_pal_count >= g_pal_cap) {
            int next = g_pal_cap ? g_pal_cap * 2 : 32;
            RockPal *np = (RockPal *)realloc(g_pal, (size_t)next * sizeof(RockPal));
            if (!np) return;
            g_pal = np;
            g_pal_cap = next;
        }
        RockPal *p = &g_pal[g_pal_count++];
        memset(p, 0, sizeof(*p));
        p->key = dup_str(key);
        p->def = dup_str(qlcg_js_str(qlcg_js_get(e, "default"), "#000000"));
        for (int s = 0; s < 4; s++) {
            const char *v = qlcg_js_str(qlcg_js_get(e, SEASON_NAMES[s]), NULL);
            if (v) p->season[s] = dup_str(v);
        }
    }
}

static const char *pal_color(const char *key, int season) {
    RockPal *p = pal_find(key);
    if (!p) return NULL;
    if (season >= 0 && season < 4 && p->season[season]) return p->season[season];
    return p->def;
}

/* ---------- 单位表 ---------- */

typedef struct {
    char  *id;
    char  *archetype;
    char  *name;
    char  *latin;
    char  *family;
    double seed;
    double height_m;
    int size_class;
    PdDict params;
    PdDict palette;        /* 槽 -> 色板键 */
    PdDict chunk_palette;  /* "区块.槽" -> 色板键 */
} RockDef;

static RockDef *g_defs = NULL;
static int g_defs_count = 0;
static int g_defs_cap = 0;
static int g_loaded = 0;
static int g_size_indices[ROCK_SIZE_COUNT][ROCKS_PER_SIZE];
static int g_size_counts[ROCK_SIZE_COUNT];

int qlcg_rock_count(void) { return g_defs_count; }

const char *qlcg_rock_id(int index) {
    if (index < 0 || index >= g_defs_count) return NULL;
    return g_defs[index].id;
}

const char *qlcg_rock_name(int index) {
    return qlcg_rock_id(index) ? g_defs[index].name : NULL;
}

int qlcg_rock_size_class(int index) {
    return qlcg_rock_id(index) ? g_defs[index].size_class : ROCK_SIZE_LEGACY;
}

int qlcg_rock_pick(int size_class, unsigned roll) {
    if (size_class < 0 || size_class >= ROCK_SIZE_COUNT) return -1;
    int count = g_size_counts[size_class];
    return count ? g_size_indices[size_class][roll % (unsigned)count] : -1;
}

static RockDef *rock_find(const char *id) {
    if (!id) return NULL;
    for (int i = 0; i < g_defs_count; i++)
        if (g_defs[i].id && strcmp(g_defs[i].id, id) == 0) return &g_defs[i];
    return NULL;
}

static void dict_from_obj(PdDict *dst, const JsVal *obj) {
    if (!obj || obj->type != JS_OBJ) return;
    for (int i = 0; i < obj->count; i++) {
        const char *k = obj->keys[i];
        const JsVal *v = obj->items[i];
        if (!k || !v) continue;
        if (v->type == JS_NUM) pd_set_num(dst, k, v->num);
        else if (v->type == JS_BOOL) pd_set_num(dst, k, v->bval ? 1.0 : 0.0);
        else if (v->type == JS_STR) pd_set_str(dst, k, v->str);
    }
}

static void load_rock_file(const char *path, int size_class) {
    const char *err = NULL;
    JsVal *root = qlcg_js_load(path, &err);
    if (!root) {
        fprintf(stderr, "[rock] %s: %s\n", path, err ? err : "parse error");
        return;
    }
    const JsVal *list = qlcg_js_get(root, "rocks");
    if (!list || list->type != JS_ARR) { qlcg_js_free(root); return; }

    for (int i = 0; i < list->count; i++) {
        const JsVal *raw = list->items[i];
        if (!raw || raw->type != JS_OBJ) continue;
        const char *id = qlcg_js_str(qlcg_js_get(raw, "id"), NULL);
        if (!id || rock_find(id) || g_defs_count >= ROCK_CATALOG_CAPACITY) continue;
        double height_m = qlcg_js_num(qlcg_js_get(raw, "heightMeters"), 0.0);
        if (size_class != ROCK_SIZE_LEGACY &&
            (!(height_m > 0.0) || g_size_counts[size_class] >= ROCKS_PER_SIZE)) continue;
        if (g_defs_count >= g_defs_cap) {
            int next = g_defs_cap ? g_defs_cap * 2 : 16;
            RockDef *nd = (RockDef *)realloc(g_defs, (size_t)next * sizeof(RockDef));
            if (!nd) break;
            g_defs = nd;
            g_defs_cap = next;
        }
        int index = g_defs_count++;
        RockDef *d = &g_defs[index];
        memset(d, 0, sizeof(*d));
        pd_init(&d->params);
        pd_init(&d->palette);
        pd_init(&d->chunk_palette);
        d->id = dup_str(id);
        d->archetype = dup_str(qlcg_js_str(qlcg_js_get(raw, "archetype"), "boulder"));
        d->name = dup_str(qlcg_js_str(qlcg_js_get(raw, "name"), ""));
        d->latin = dup_str(qlcg_js_str(qlcg_js_get(raw, "latin"), ""));
        d->family = dup_str(qlcg_js_str(qlcg_js_get(raw, "family"), "igneous"));
        d->seed = qlcg_js_num(qlcg_js_get(raw, "seed"), 0.0);
        d->height_m = height_m;
        d->size_class = size_class;
        if (size_class != ROCK_SIZE_LEGACY)
            g_size_indices[size_class][g_size_counts[size_class]++] = index;
        dict_from_obj(&d->params, qlcg_js_get(raw, "params"));
        dict_from_obj(&d->palette, qlcg_js_get(raw, "palette"));

        const JsVal *cp = qlcg_js_get(raw, "chunkPalette");
        if (cp && cp->type == JS_OBJ) {
            for (int c = 0; c < cp->count; c++) {
                const char *ck = cp->keys[c];
                const JsVal *sub = cp->items[c];
                if (!ck || !sub || sub->type != JS_OBJ) continue;
                for (int s = 0; s < sub->count; s++) {
                    const char *slot = sub->keys[s];
                    const char *val = qlcg_js_str(sub->items[s], NULL);
                    if (!slot || !val) continue;
                    char key[96];
                    snprintf(key, sizeof(key), "%s.%s", ck, slot);
                    pd_set_str(&d->chunk_palette, key, val);
                }
            }
        }
    }
    qlcg_js_free(root);
}

int qlcg_rock_load(const char *dir) {
    if (g_loaded) return g_defs_count;
    char path[512];
    snprintf(path, sizeof(path), "%s/palettes.json", dir);
    const char *err = NULL;
    JsVal *pal = qlcg_js_load(path, &err);
    if (pal) {
        pal_add_group(qlcg_js_get(pal, "stone"));
        pal_add_group(qlcg_js_get(pal, "cover"));
        qlcg_js_free(pal);
    } else {
        fprintf(stderr, "[rock] 色板缺失 %s\n", path);
    }
    for (int i = 0; i < (int)(sizeof(ROCK_FILES) / sizeof(ROCK_FILES[0])); i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, ROCK_FILES[i]);
        load_rock_file(path, ROCK_SIZE_LEGACY);
    }
    for (int i = 0; i < ROCK_SIZE_COUNT; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, SIZE_FILES[i]);
        load_rock_file(path, i);
    }
    g_loaded = 1;
    return g_defs_count;
}

void qlcg_rock_unload(void) {
    for (int i = 0; i < g_defs_count; i++) {
        RockDef *d = &g_defs[i];
        free(d->id); free(d->archetype); free(d->name); free(d->latin); free(d->family);
        pd_free(&d->params);
        pd_free(&d->palette);
        pd_free(&d->chunk_palette);
    }
    free(g_defs);
    g_defs = NULL;
    g_defs_count = 0;
    g_defs_cap = 0;
    memset(g_size_counts, 0, sizeof(g_size_counts));
    for (int i = 0; i < g_pal_count; i++) {
        free(g_pal[i].key);
        free(g_pal[i].def);
        for (int s = 0; s < 4; s++) free(g_pal[i].season[s]);
    }
    free(g_pal);
    g_pal = NULL;
    g_pal_count = 0;
    g_pal_cap = 0;
    g_loaded = 0;
}

/* ---------- 参数表 ---------- */

typedef struct { const char *key; double v; } RK;
typedef struct { const char *name; const RK *pairs; int count; } RKTable;

#define RKN(k, v) { k, v }

static const RK T_BOULDER[] = {
    RKN("diameter",2.1), RKN("aspect",0.78), RKN("sides",7), RKN("shells",5),
    RKN("shellTaper",0.87), RKN("shellSkew",0.12), RKN("shellTwist",0.5),
    RKN("baseFlatten",0.95), RKN("jitter",0.22), RKN("sink",0.14),
};
static const RK T_BLOCK[] = {
    RKN("diameter",1.9), RKN("aspect",0.9), RKN("sides",6), RKN("shells",4),
    RKN("shellTaper",0.91), RKN("shellSkew",0.13), RKN("shellTwist",0.7),
    RKN("jitter",0.15), RKN("sink",0.1),
};
static const RK T_RUBBLE[] = {
    RKN("diameter",1.5), RKN("aspect",0.56), RKN("sides",5), RKN("chunks",6),
    RKN("spread",0.85), RKN("jitter",0.26), RKN("sink",0.02),
};
static const RK T_SLAB[] = {
    RKN("diameter",2.2), RKN("aspect",0.4), RKN("sides",6), RKN("plates",4),
    RKN("plateThickness",0.1), RKN("plateSkew",0.16), RKN("plateTilt",0.16),
    RKN("jitter",0.17), RKN("sink",0.08),
};
static const RK T_LEDGE[] = {
    RKN("diameter",2.9), RKN("aspect",0.52), RKN("sides",7), RKN("jitter",0.17), RKN("sink",0.2),
};
static const RK T_SHARD[] = {
    RKN("diameter",1.9), RKN("aspect",0.6), RKN("sides",5), RKN("plates",3),
    RKN("jitter",0.23), RKN("sink",0.1),
};
static const RK T_SPIRE[] = {
    RKN("diameter",1.05), RKN("aspect",2.2), RKN("sides",5), RKN("segments",3),
    RKN("tipRatio",0), RKN("baseRatio",0.6), RKN("nodeSkew",0.16), RKN("nodeTwist",0.4),
    RKN("jitter",0.22), RKN("sink",0.06),
};
static const RK T_STUB[] = {
    RKN("diameter",1.5), RKN("aspect",0.85), RKN("sides",6), RKN("segments",2),
    RKN("tipRatio",0.72), RKN("baseRatio",0.74), RKN("nodeSkew",0.1), RKN("nodeTwist",0.26),
    RKN("jitter",0.18), RKN("sink",0.16),
};
static const RK T_FLOE[] = {
    RKN("diameter",2.3), RKN("aspect",0.5), RKN("sides",7), RKN("shells",3),
    RKN("shellTaper",0.86), RKN("plates",2), RKN("jitter",0.16), RKN("sink",0.22),
};

static const RKTable ARCH_DEFAULTS[] = {
    { "boulder", T_BOULDER, 10 }, { "block", T_BLOCK, 9 }, { "rubble", T_RUBBLE, 7 },
    { "slab", T_SLAB, 9 }, { "ledge", T_LEDGE, 5 }, { "shard", T_SHARD, 6 },
    { "spire", T_SPIRE, 10 }, { "stub", T_STUB, 10 }, { "floe", T_FLOE, 8 },
};

static const RK GLOBAL_DEFAULTS[] = {
    RKN("aspect",0.62), RKN("sides",6), RKN("jitter",0.18),
    RKN("crustSlope",0.42), RKN("variant",1),
};

/* 岩性默认色板 */
typedef struct { const char *slot; const char *key; } RKv;
static const RKv FAM_IGNEOUS[]   = { {"body","granite"},{"band","graniteDark"},{"cap","granitePale"},{"base","soil"},{"crust","lichen"} };
static const RKv FAM_SEDIMENT[]  = { {"body","sandstone"},{"band","sandstoneDark"},{"cap","sandstonePale"},{"base","soil"},{"crust","dryGrass"} };
static const RKv FAM_METAMORPH[] = { {"body","slate"},{"band","slateDark"},{"cap","slatePale"},{"base","soil"},{"crust","moss"} };
static const RKv FAM_CLASTIC[]   = { {"body","gravel"},{"band","gravelDark"},{"cap","scree"},{"base","soil"},{"crust","dryGrass"} };
static const RKv FAM_ICE[]       = { {"body","ice"},{"band","iceDark"},{"cap","ice"},{"base","iceDark"},{"crust","snowCap"} };

typedef struct { const char *name; const RKv *pairs; int count; } FamTable;
static const FamTable FAMILIES[] = {
    { "igneous", FAM_IGNEOUS, 5 }, { "sedimentary", FAM_SEDIMENT, 5 },
    { "metamorphic", FAM_METAMORPH, 5 }, { "clastic", FAM_CLASTIC, 5 }, { "ice", FAM_ICE, 5 },
};

static const char *const PALETTE_SLOTS[] = { "body", "band", "cap", "base", "crust" };

const char *qlcg_rock_family_palette(const char *family, const char *slot) {
    if (!family || !slot) return NULL;
    for (int i = 0; i < (int)(sizeof(FAMILIES) / sizeof(FAMILIES[0])); i++) {
        if (strcmp(FAMILIES[i].name, family) != 0) continue;
        for (int k = 0; k < FAMILIES[i].count; k++)
            if (strcmp(FAMILIES[i].pairs[k].slot, slot) == 0) return FAMILIES[i].pairs[k].key;
    }
    return NULL;
}

static const RKTable *arch_defaults(const char *arch) {
    if (!arch) return NULL;
    for (int i = 0; i < (int)(sizeof(ARCH_DEFAULTS) / sizeof(ARCH_DEFAULTS[0])); i++)
        if (strcmp(ARCH_DEFAULTS[i].name, arch) == 0) return &ARCH_DEFAULTS[i];
    return NULL;
}

static double pnum(const PdDict *d, const char *k, double dflt) {
    double v = pd_num(d, k, NAN);
    return isnan(v) ? dflt : v;
}

/* 覆被键 -> 材质槽 */
static int crust_material_of(const char *key, const char *family) {
    if (!key) return ROCK_MAT_MOSS;
    if (strcmp(key, "snowCap") == 0 || strcmp(key, "snow") == 0) return ROCK_MAT_SNOW;
    if (strcmp(key, "ice") == 0 || strcmp(key, "iceDark") == 0) return ROCK_MAT_ICE;
    if (strcmp(key, "moss") == 0) return (family && strcmp(family, "ice") == 0) ? ROCK_MAT_ICE : ROCK_MAT_MOSS;
    return ROCK_MAT_MOSS;
}

static const char *default_crust_key(const RockDef *d) {
    const char *k = pd_str(&d->palette, "crust", NULL);
    if (k) return k;
    const char *f = qlcg_rock_family_palette(d->family, "crust");
    return f ? f : "moss";
}

static double default_coverage(const RockDef *d, int season) {
    double base = 0.14;
    if (strcmp(d->family, "metamorphic") == 0) base = 0.3;
    else if (strcmp(d->family, "igneous") == 0) base = 0.22;
    else if (strcmp(d->family, "clastic") == 0) base = 0.18;
    double w = (season == 3) ? 0.24 : ((season == 2) ? 0.12 : 0.0);
    double v = base + w;
    return v > 0.85 ? 0.85 : v;
}

int qlcg_rock_build_def(const char *id, int season, int form, int chunk, RockBuildDef *out) {
    memset(out, 0, sizeof(*out));
    if (!g_loaded) return 0;
    RockDef *d = rock_find(id);
    if (!d) return 0;
    if (season < 0 || season >= 4) season = 1;
    if (form < 0 || form >= ROCK_FORM_COUNT) form = ROCK_FORM_OUTCROP;

    out->id = dup_str(d->id);
    out->archetype = dup_str(d->archetype);
    out->name = dup_str(d->name);
    out->latin = dup_str(d->latin);
    out->family = dup_str(d->family);
    out->seed = d->seed;
    out->height_m = d->height_m;
    out->form = form;
    out->shape_seed = 0;
    out->season = season;
    pd_init(&out->params);
    pd_init(&out->palette);

    /* 形态档派生 */
    const char *crust_key = default_crust_key(d);
    int crust_mat = crust_material_of(crust_key, d->family);
    const char *patch_crust = NULL;
    double patch_nums[8][2];
    int patch_n = 0;
    switch (form) {
    case ROCK_FORM_BURIED: {
        double sink = pnum(&d->params, "sink", 0.12) + 0.15;
        patch_nums[patch_n][0] = 0; patch_nums[patch_n][1] = 1.18; patch_n++;   /* spreadScale */
        patch_nums[patch_n][0] = 1; patch_nums[patch_n][1] = 0.82; patch_n++;   /* sizeScale */
        patch_nums[patch_n][0] = 2; patch_nums[patch_n][1] = sink; patch_n++;   /* sink */
        break;
    }
    case ROCK_FORM_STACKED:
        out->crust_material = crust_mat;
        snprintf(out->crust_color, sizeof(out->crust_color), "%s", crust_key);
        out->has_crust_color = 1;
        break;
    case ROCK_FORM_CRUSTED:
        patch_nums[patch_n][0] = 1; patch_nums[patch_n][1] = 0.94; patch_n++;
        patch_nums[patch_n][0] = 3; patch_nums[patch_n][1] = 0.85; patch_n++;   /* crust */
        out->crust_material = crust_mat;
        snprintf(out->crust_color, sizeof(out->crust_color), "%s", crust_key);
        out->has_crust_color = 1;
        break;
    default:
        patch_nums[patch_n][0] = 3; patch_nums[patch_n][1] = default_coverage(d, season); patch_n++;
        out->crust_material = crust_mat;
        snprintf(out->crust_color, sizeof(out->crust_color), "%s", crust_key);
        out->has_crust_color = 1;
        break;
    }
    if (form == ROCK_FORM_BURIED) {
        out->crust_material = crust_mat;
        snprintf(out->crust_color, sizeof(out->crust_color), "%s", crust_key);
        out->has_crust_color = 1;
    }
    (void)patch_crust;

    /* 参数四层合并：全局<原型<单位<形态 */
    for (int i = 0; i < (int)(sizeof(GLOBAL_DEFAULTS) / sizeof(GLOBAL_DEFAULTS[0])); i++)
        pd_set_num(&out->params, GLOBAL_DEFAULTS[i].key, GLOBAL_DEFAULTS[i].v);
    const RKTable *ad = arch_defaults(d->archetype);
    if (ad) for (int i = 0; i < ad->count; i++) pd_set_num(&out->params, ad->pairs[i].key, ad->pairs[i].v);
    pd_merge(&out->params, &d->params);
    static const char *const PATCH_KEYS[4] = { "spreadScale", "sizeScale", "sink", "crust" };
    for (int i = 0; i < patch_n; i++)
        pd_set_num(&out->params, PATCH_KEYS[(int)patch_nums[i][0]], patch_nums[i][1]);

    /* 色板：族 <- 单位 <- 区块覆盖 */
    for (int i = 0; i < 5; i++) {
        const char *slot = PALETTE_SLOTS[i];
        const char *key = qlcg_rock_family_palette(d->family, slot);
        if (!key) continue;
        const char *override = pd_str(&d->palette, slot, NULL);
        if (override) key = override;
        char ck[96];
        snprintf(ck, sizeof(ck), "%d.%s", chunk, slot);
        const char *chunk_key = pd_str(&d->chunk_palette, ck, NULL);
        if (chunk_key) key = chunk_key;
        const char *hex = pal_color(key, season);
        if (hex) pd_set_str(&out->palette, slot, hex);
    }

    /* 覆被色 */
    if (out->has_crust_color) {
        const char *hex = pal_color(out->crust_color, season);
        if (hex) {
            out->crust_rgb = qlcg_lp_hex(hex, 255, 0, 255);
        }
    }

    /* buildSeed 哈希 id:seed */
    char seedbuf[40], key[160];
    if (d->seed == floor(d->seed) && fabs(d->seed) < 1e15) snprintf(seedbuf, sizeof(seedbuf), "%.0f", d->seed);
    else snprintf(seedbuf, sizeof(seedbuf), "%g", d->seed);
    snprintf(key, sizeof(key), "%s:%s:%s", d->archetype, d->id, seedbuf);
    out->build_seed = qlcg_lp_hash_string(key);

    return 1;
}

void qlcg_rock_free_def(RockBuildDef *d) {
    if (!d) return;
    free(d->id); free(d->archetype); free(d->name); free(d->latin); free(d->family);
    pd_free(&d->params);
    pd_free(&d->palette);
    memset(d, 0, sizeof(*d));
}
