/* 岩石材料实现 */
#include "rock_material.h"
#include "rock_arch.h"
#include "rock_def.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROCK_DATA_DIR "content/data/world/rocks"
#define ROCK_GEO_SLOTS 256

typedef struct {
    int          index;
    int          form;
    int          season;
    int          chunk;
    int          ready;
    RockGeometry geo;
    DecorationShape shape;
} RockGeoSlot;

static int         g_loaded = 0;
static int         g_count = 0;
static RockGeoSlot g_outcrop[ROCK_CATALOG_CAPACITY][4];
static RockGeoSlot g_geo[ROCK_GEO_SLOTS];
static int         g_geo_count = 0;

int qlcg_rock_material_init(void) {
    if (g_loaded) return g_count;
    int n = qlcg_rock_load(ROCK_DATA_DIR);
    if (n <= 0) {
        fprintf(stderr, "[rock] 岩石数据装载失败，沿用占位网格\n");
        g_loaded = 1;
        g_count = 0;
        return 0;
    }
    g_count = n;
    g_loaded = 1;
    return n;
}

void qlcg_rock_material_shutdown(void) {
    for (int i = 0; i < g_count; i++) {
        for (int season = 0; season < 4; season++)
            qlcg_rock_geometry_free(&g_outcrop[i][season].geo);
    }
    memset(g_outcrop, 0, sizeof(g_outcrop));
    for (int i = 0; i < g_geo_count; i++) {
        if (g_geo[i].ready) qlcg_rock_geometry_free(&g_geo[i].geo);
    }
    g_geo_count = 0;
    if (g_loaded) qlcg_rock_unload();
    g_loaded = 0;
    g_count = 0;
}

int qlcg_rock_material_count(void) { return g_count; }

static RockGeoSlot *geo_slot(int index, int form, int season, int chunk) {
    if (form == ROCK_FORM_OUTCROP && chunk == 1) return &g_outcrop[index][season];
    for (int i = 0; i < g_geo_count; i++) {
        RockGeoSlot *s = &g_geo[i];
        if (s->index == index && s->form == form && s->season == season && s->chunk == chunk)
            return s;
    }
    if (g_geo_count >= ROCK_GEO_SLOTS) return NULL;

    RockGeoSlot *s = &g_geo[g_geo_count++];
    memset(s, 0, sizeof(*s));
    s->index = index;
    s->form = form;
    s->season = season;
    s->chunk = chunk;
    return s;
}

static RockGeoSlot *rock_model(int index, int form, int season, int chunk) {
    if (index < 0 || index >= g_count) return NULL;
    if (form < 0 || form >= ROCK_FORM_COUNT) form = ROCK_FORM_OUTCROP;
    if (season < 0 || season >= 4) season = 1;
    RockGeoSlot *s = geo_slot(index, form, season, chunk);
    if (!s || s->ready < 0) return NULL;
    if (s->ready) return s;
    s->ready = -1;

    const char *id = qlcg_rock_id(index);
    if (!id) return NULL;
    RockBuildDef d;
    if (!qlcg_rock_build_def(id, season, form, chunk, &d)) return NULL;
    int ok = qlcg_rock_arch_build(&d, &s->geo);
    qlcg_rock_free_def(&d);
    if (!ok) return NULL;
    qlcg_collision_bounds(&s->shape,s->geo.positions,s->geo.vertex_count);
    s->ready = 1;
    return s;
}

const RockGeometry *qlcg_rock_geometry(int index, int form, int season, int chunk) {
    RockGeoSlot *s = rock_model(index, form, season, chunk);
    return s ? &s->geo : NULL;
}

const DecorationShape *qlcg_rock_collision_shape(int index, int form, int season, int chunk) {
    RockGeoSlot *s = rock_model(index, form, season, chunk);
    return s ? &s->shape : NULL;
}

float qlcg_rock_sprite_span(const RockGeometry *g) {
    return g ? fmaxf(0.2f, 1.05f * (float)fmax(g->height, 2.0 * g->radius)) : 1.05f;
}
