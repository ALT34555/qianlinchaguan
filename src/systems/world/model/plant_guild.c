/* 植物丛表装载与按区查询 */
#include "plant_guild.h"
#include "plant_def.h"
#include "rock_def.h"
#include "rock_material.h"
#include "model_json.h"
#include "../climate_zone.h"
#include "../../../core/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PG_MAX_POOL  128
#define PG_MAX_ZONE  32

static QlcgGuildDef *g_def = NULL;
static int           g_count = 0;
static int           g_cap   = 0;

/* 散生层：全部草本与水生 */
static int g_scatter[PG_MAX_POOL];
static int g_scatter_n = 0;

/* 按区预计算，查询 O(1) */
static int g_zone_pool[QLCG_ZONE_COUNT][PG_MAX_POOL];
static int g_zone_pool_n[QLCG_ZONE_COUNT];
static int *g_zone_guild;
static int g_zone_guild_n[QLCG_ZONE_COUNT];

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int species_of(const char *id) {
    if (!id) return -1;
    int n = qlcg_pd_species_count();
    for (int i = 0; i < n; i++) {
        const PdSpecies *sp = qlcg_pd_species(i);
        if (sp && sp->base.id && strcmp(sp->base.id, id) == 0) return i;
    }
    return -1;
}

static int rock_of(const char *id) {
    if (!id) return -1;
    for (int i = 0; i < qlcg_rock_count(); i++) {
        const char *candidate = qlcg_rock_id(i);
        if (candidate && strcmp(candidate, id) == 0) return i;
    }
    return -1;
}

static int tier_of(const char *s) {
    if (!s) return QLCG_GUILD_TIER_SHRUB;
    if (!strcmp(s, "ground")) return QLCG_GUILD_TIER_GROUND;
    if (!strcmp(s, "tree")) return QLCG_GUILD_TIER_TREE;
    return QLCG_GUILD_TIER_SHRUB;
}

static int layout_of(const char *s) {
    if (!s) return QLCG_GUILD_LAYOUT_SCATTER;
    if (!strcmp(s, "grid")) return QLCG_GUILD_LAYOUT_GRID;
    if (!strcmp(s, "rows")) return QLCG_GUILD_LAYOUT_ROWS;
    return QLCG_GUILD_LAYOUT_SCATTER;
}

static int ring_of(const char *s) {
    if (!s) return QLCG_GUILD_RING_ANY;
    if (!strcmp(s, "core")) return QLCG_GUILD_RING_CORE;
    if (!strcmp(s, "inner")) return QLCG_GUILD_RING_INNER;
    if (!strcmp(s, "outer")) return QLCG_GUILD_RING_OUTER;
    return QLCG_GUILD_RING_ANY;
}

static uint32_t zone_mask_of(const JsVal *arr) {
    uint32_t mask = 0;
    int n = qlcg_js_count(arr);
    for (int i = 0; i < n; i++) {
        int z = (int)qlcg_js_num(qlcg_js_at(arr, i), -1);
        if (z >= 0 && z < PG_MAX_ZONE) mask |= (uint32_t)1u << z;
    }
    return mask;
}

static float weight_of(const JsVal *weights, const char *id) {
    if (!weights || !id) return 1.0f;
    double w = qlcg_js_num(qlcg_js_get(weights, id), 1.0);
    if (w <= 0.0) return 0.0f;
    return (float)w;
}

static int parse_members(const JsVal *entry, QlcgGuildDef *def) {
    const JsVal *arr = qlcg_js_get(entry, "members");
    int n = qlcg_js_count(arr);
    if (n <= 0) return 0;
    QlcgGuildMember *m = (QlcgGuildMember *)calloc((size_t)n, sizeof(*m));
    if (!m) return 0;
    int k = 0;
    for (int i = 0; i < n; i++) {
        const JsVal *e = qlcg_js_at(arr, i);
        const char *spid = qlcg_js_str(qlcg_js_get(e, "sp"), NULL);
        int sp = species_of(spid);
        if (sp < 0) {
            fprintf(stderr, "[guild] 未知物种 %s\n", spid ? spid : "?");
            continue;
        }
        const PdSpecies *msp = qlcg_pd_species(sp);
        const char *on = qlcg_js_str(qlcg_js_get(e,"on"),NULL);
        int attachment = !on ? GUILD_GROUND : !strcmp(on,"wood") ? GUILD_ON_WOOD
                         : !strcmp(on,"plant") ? GUILD_ON_PLANT : -1;
        if (!msp || attachment < 0 ||
            (attachment == GUILD_ON_WOOD && (def->rock < 0 || msp->category != PLANT_CAT_JUN)) ||
            (attachment == GUILD_ON_PLANT && !qlcg_pd_category_can_hang(msp->category)) ||
            (msp->category != PLANT_CAT_SHU && msp->category != PLANT_CAT_GUAN &&
                     msp->category != PLANT_CAT_TENG && msp->category != PLANT_CAT_JUN &&
                     !(def->grass_tufts && msp->category == PLANT_CAT_CAO))) {
            fprintf(stderr, "[guild] 不支持的成员 %s\n", spid ? spid : "?");
            continue;
        }
        const JsVal *nv = qlcg_js_get(e, "n");
        int lo = (int)qlcg_js_num(qlcg_js_at(nv, 0), 1);
        int hi = (int)qlcg_js_num(qlcg_js_at(nv, 1), lo);
        if (lo < 0) lo = 0;
        if (hi < lo) hi = lo;
        if (lo > 4096) lo = 4096;
        if (hi > 4096) hi = 4096;
        m[k].attachment = (uint8_t)attachment;
        m[k].species = sp;
        m[k].n_min = (uint16_t)lo;
        m[k].n_max = (uint16_t)hi;
        m[k].ring = (uint8_t)ring_of(qlcg_js_str(qlcg_js_get(e, "ring"), NULL));
        m[k].clump = (uint8_t)qlcg_js_bool(qlcg_js_get(e, "clump"), 0);
        k++;
    }
    def->members = m;
    def->member_count = (int16_t)k;
    return k;
}

/* 散生层的草本和水生植物 */
static void collect_scatter(void) {
    int n = qlcg_pd_species_count();
    g_scatter_n = 0;
    for (int i = 0; i < n; i++) {
        const PdSpecies *sp = qlcg_pd_species(i);
        if (!sp) continue;
        if (sp->category != PLANT_CAT_CAO && sp->category != PLANT_CAT_SHUI) continue;
        if (g_scatter_n >= PG_MAX_POOL) break;
        g_scatter[g_scatter_n++] = i;
    }
}

static int push_def(const QlcgGuildDef *def) {
    if (g_count == g_cap) {
        int cap = g_cap ? g_cap * 2 : 32;
        QlcgGuildDef *nd = (QlcgGuildDef *)realloc(g_def, (size_t)cap * sizeof(*nd));
        if (!nd) return 0;
        g_def = nd;
        g_cap = cap;
    }
    g_def[g_count++] = *def;
    return 1;
}

static int parse_defs(const JsVal *arr, const JsVal *weights, int mono, int ambient) {
    int n = qlcg_js_count(arr);
    int added = 0;
    for (int i = 0; i < n; i++) {
        const JsVal *e = qlcg_js_at(arr, i);
        if (!e) continue;
        QlcgGuildDef def;
        memset(&def, 0, sizeof(def));
        const char *id = qlcg_js_str(qlcg_js_get(e, "id"), NULL);
        if (!id) continue;
        def.zone_mask = zone_mask_of(qlcg_js_get(e, "zones"));
        if (!def.zone_mask) continue;
        def.id = dup_str(id);
        def.name = dup_str(qlcg_js_str(qlcg_js_get(e, "name"), id));
        def.tier = (uint8_t)tier_of(qlcg_js_str(qlcg_js_get(e, "tier"), NULL));
        def.mono = (uint8_t)(mono ? 1 : 0);
        def.species = -1;
        def.rock = -1;
        def.weight = ambient ? (float)qlcg_js_num(qlcg_js_get(e, "weight"), 1.0)
                             : weight_of(weights, id);
        if (def.weight < 0.0f) def.weight = 0.0f;
        def.scale_lo = 0.9f;
        def.scale_hi = 1.15f;
        const JsVal *sc = qlcg_js_get(e, "scale");
        if (qlcg_js_count(sc) >= 2) {
            def.scale_lo = (float)qlcg_js_num(qlcg_js_at(sc, 0), 0.9);
            def.scale_hi = (float)qlcg_js_num(qlcg_js_at(sc, 1), 1.15);
        }
        if (ambient) {
            const JsVal *grass = qlcg_js_get(e, "grass");
            double tufts = qlcg_js_num(qlcg_js_get(grass, "tufts"), 0.0);
            double scale = qlcg_js_num(qlcg_js_get(grass, "scale"), 0.0);
            const char *rock = qlcg_js_str(qlcg_js_get(e, "rock"), NULL);
            int members = qlcg_js_count(qlcg_js_get(e, "members"));
            def.rock = (int16_t)rock_of(rock);
            if ((grass || ambient == 1) && (tufts < 1 || tufts > 192 || scale < 0.1 || scale > 2.0) ||
                (rock ? def.rock < 0 || (ambient == 1 && members > 0) : members <= 0)) {
                fprintf(stderr, "[guild] 氛围草配置无效 %s\n", id);
                free((void *)def.id);
                free((void *)def.name);
                continue;
            }
            def.grass_tufts = (uint8_t)tufts;
            def.grass_scale = (float)scale;
            if (members && parse_members(e, &def) != members) {
                free((void *)def.id);
                free((void *)def.name);
                free((void *)def.members);
                continue;
            }
        } else if (mono) {
            const char *spid = qlcg_js_str(qlcg_js_get(e, "sp"), NULL);
            def.species = (int16_t)species_of(spid);
            if (def.species < 0) {
                fprintf(stderr, "[guild] 优势丛物种未知 %s\n", spid ? spid : "?");
                free((void *)def.id);
                free((void *)def.name);
                continue;
            }
            def.layout = (uint8_t)layout_of(qlcg_js_str(qlcg_js_get(e, "layout"), NULL));
            def.spacing  = (float)qlcg_js_num(qlcg_js_get(e, "spacing"), 1.5);
            def.jitter   = (float)qlcg_js_num(qlcg_js_get(e, "jitter"), 0.3);
            def.row_gap  = (float)qlcg_js_num(qlcg_js_get(e, "rowGap"), 1.5);
            def.plant_gap= (float)qlcg_js_num(qlcg_js_get(e, "plantGap"), 1.0);
            def.min_gap  = (float)qlcg_js_num(qlcg_js_get(e, "minGap"), 1.0);
            const JsVal *nv = qlcg_js_get(e, "n");
            def.n_min = 0;
            def.n_max = 0;
            if (qlcg_js_count(nv) >= 2) {
                def.n_min = (int16_t)qlcg_js_num(qlcg_js_at(nv, 0), 8);
                def.n_max = (int16_t)qlcg_js_num(qlcg_js_at(nv, 1), def.n_min);
            }
            if (def.spacing < 0.3f) def.spacing = 0.3f;
            if (def.row_gap < 0.3f) def.row_gap = 0.3f;
            if (def.plant_gap < 0.2f) def.plant_gap = 0.2f;
            if (def.min_gap < 0.2f) def.min_gap = 0.2f;
            if (def.jitter < 0.0f) def.jitter = 0.0f;
            if (def.jitter > 0.6f) def.jitter = 0.6f;
        } else if (parse_members(e, &def) <= 0) {
            free((void *)def.id);
            free((void *)def.name);
            continue;
        }
        if (!mono && !ambient) {
            const JsVal *vars = qlcg_js_get(e, "variants");
            int vn = qlcg_js_count(vars);
            for (int v = 0; v < vn; v++) {
                const JsVal *ve = qlcg_js_at(vars, v);
                if (!ve) continue;
                const JsVal *bushes = qlcg_js_get(ve, "bushes");
                int bn = qlcg_js_count(bushes);
                if (bn <= 0 || def.member_count <= 0) continue;
                int cap = 1 + bn;
                QlcgGuildMember *mm = (QlcgGuildMember *)calloc(
                    (size_t)cap, sizeof(*mm));
                if (!mm) continue;
                mm[0] = def.members[0];
                int k = 1;
                for (int b = 0; b < bn; b++) {
                    int sp = species_of(qlcg_js_str(qlcg_js_at(bushes, b), NULL));
                    if (sp < 0) continue;
                    mm[k].species = sp;
                    mm[k].n_min = 2;
                    mm[k].n_max = 3;
                    mm[k].ring = (uint8_t)(k % 2 ? QLCG_GUILD_RING_INNER
                                                : QLCG_GUILD_RING_OUTER);
                    mm[k].clump = 0;
                    k++;
                }
                if (k <= 1) {
                    free(mm);
                    continue;
                }
                QlcgGuildDef var = def;
                char vid[96];
                snprintf(vid, sizeof(vid), "%s#%d", id, v + 1);
                var.id = dup_str(vid);
                var.name = def.name;
                var.members = mm;
                var.member_count = (int16_t)k;
                var.weight = def.weight * 0.8f;
                if (push_def(&var)) added++;
                else {
                    free((void *)var.id);
                    free(mm);
                }
            }
        }
        if (!push_def(&def)) {
            free((void *)def.id);
            free((void *)def.name);
            free((void *)def.members);
            continue;
        }
        added++;
    }
    return added;
}

/* 池内追加并去重 */
static void pool_add(int *pool, int *n, int species) {
    if (species < 0 || *n >= PG_MAX_POOL) return;
    const PdSpecies *sp=qlcg_pd_species(species);
    if (!sp || qlcg_pd_category_can_hang(sp->category)) return;
    for (int i = 0; i < *n; i++) if (pool[i] == species) return;
    pool[(*n)++] = species;
}

static int build_zone_tables(void) {
    g_zone_guild = malloc((size_t)QLCG_ZONE_COUNT * g_count * sizeof(*g_zone_guild));
    if (!g_zone_guild) return 0;
    for (int z = 0; z < QLCG_ZONE_COUNT; z++) {
        g_zone_pool_n[z] = 0;
        g_zone_guild_n[z] = 0;
        uint32_t bit = (uint32_t)1u << z;
        for (int i = 0; i < g_count; i++) {
            const QlcgGuildDef *d = &g_def[i];
            if (!(d->zone_mask & bit)) continue;
            g_zone_guild[z * g_count + g_zone_guild_n[z]++] = i;
            if (d->grass_tufts) continue;
            if (d->mono) {
                pool_add(g_zone_pool[z], &g_zone_pool_n[z], d->species);
            } else {
                for (int k = 0; k < d->member_count; k++)
                    pool_add(g_zone_pool[z], &g_zone_pool_n[z], d->members[k].species);
            }
        }
        const QlcgClimateZoneInfo *info = qlcg_climate_zone_info(z);
        int heat = info ? info->heat : 4;
        if (heat >= 4) continue;
        for (int i = 0; i < g_scatter_n; i++)
            pool_add(g_zone_pool[z], &g_zone_pool_n[z], g_scatter[i]);
    }
    return 1;
}

int qlcg_pg_load(const char *dir) {
    qlcg_pg_unload();
    if (!dir) return -1;
    if (qlcg_pd_species_count() <= 0) return -1;
    char path[512];
    snprintf(path, sizeof(path), "%s/guilds.json", dir);
    const char *err = NULL;
    JsVal *root = qlcg_js_load(path, &err);
    if (!root) {
        fprintf(stderr, "[guild] 装载失败 %s: %s\n", path, err ? err : "err");
        return -1;
    }
    collect_scatter();
    const JsVal *weights = qlcg_js_get(root, "weights");
    int n = parse_defs(qlcg_js_get(root, "mixed"), weights, 0, 0);
    n += parse_defs(qlcg_js_get(root, "mono"), weights, 1, 0);
    const JsVal *ambient = qlcg_js_get(root, "ambient");
    const JsVal *habitats=qlcg_js_get(root,"habitats");
    if (qlcg_js_count(ambient) > 0 || qlcg_js_count(habitats) > 0) {
        qlcg_rock_material_init();
        n += parse_defs(ambient, NULL, 0, 1);
        n += parse_defs(habitats, NULL, 0, 2);
    }
    qlcg_js_free(root);
    if (n <= 0) return -1;
    if (!build_zone_tables()) {
        qlcg_pg_unload();
        return -1;
    }
    return g_count;
}

void qlcg_pg_unload(void) {
    for (int i = 0; i < g_count; i++) {
        free((void *)g_def[i].id);
        free((void *)g_def[i].members);
        int shared = 0;
        for (int k = 0; k < i; k++)
            if (g_def[k].name == g_def[i].name) { shared = 1; break; }
        if (!shared) free((void *)g_def[i].name);
    }
    free(g_def);
    free(g_zone_guild);
    g_def = NULL;
    g_zone_guild = NULL;
    g_count = 0;
    g_cap = 0;
    g_scatter_n = 0;
    memset(g_zone_guild_n, 0, sizeof(g_zone_guild_n));
    memset(g_zone_pool_n, 0, sizeof(g_zone_pool_n));
}

int qlcg_pg_count(void) { return g_count; }

const QlcgGuildDef *qlcg_pg_guild(int index) {
    if (index < 0 || index >= g_count) return NULL;
    return &g_def[index];
}

int qlcg_pg_zone_guilds(int zone, int mono, int *out, int cap) {
    if (zone < 0 || zone >= QLCG_ZONE_COUNT || !out || cap <= 0) return 0;
    int n = 0;
    for (int i = 0; i < g_zone_guild_n[zone] && n < cap; i++) {
        int idx = g_zone_guild[zone * g_count + i];
        if (mono >= 0 && g_def[idx].mono != (uint8_t)mono) continue;
        out[n++] = idx;
    }
    return n;
}

int qlcg_pg_zone_pool(int zone, int *out, int cap) {
    if (zone < 0 || zone >= QLCG_ZONE_COUNT || !out || cap <= 0) return 0;
    int n = g_zone_pool_n[zone];
    if (n > cap) n = cap;
    memcpy(out, g_zone_pool[zone], (size_t)n * sizeof(int));
    return n;
}

int qlcg_pg_pick(int zone, int mono, uint32_t roll) {
    if (zone < 0 || zone >= QLCG_ZONE_COUNT) return -1;
    float total = 0.0f;
    for (int i = 0; i < g_zone_guild_n[zone]; i++) {
        const QlcgGuildDef *d = &g_def[g_zone_guild[zone * g_count + i]];
        if (mono >= 0 && d->mono != (uint8_t)mono) continue;
        total += d->weight;
    }
    if (total <= 0.0f) return -1;
    float target = total * (((float)(roll & 0xFFFFu) + 0.5f) / 65536.0f);
    for (int i = 0; i < g_zone_guild_n[zone]; i++) {
        int idx = g_zone_guild[zone * g_count + i];
        const QlcgGuildDef *d = &g_def[idx];
        if (mono >= 0 && d->mono != (uint8_t)mono) continue;
        target -= d->weight;
        if (target <= 0.0f) return idx;
    }
    return -1;
}
