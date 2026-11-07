/* 植物材料实现 */
#include "plant_material.h"
#include "plant_def.h"
#include "plant_forms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define PLANT_DATA_DIR "content/data/world/plants"
#define PLANT_FORM_COUNT (1 + PLANT_VARIANT_COUNT)

typedef struct {
    int        ready;
    int        failed;
    LpGeometry geo;
    DecorationShape shape;
} PlantGeoSlot;

static int          g_loaded = 0;
static int          g_species = 0;
static PlantGeoSlot *g_geo;

int qlcg_plant_material_init(void) {
    if (g_loaded) return g_species;
    int n = qlcg_pd_load(PLANT_DATA_DIR);
    if (n <= 0) {
        fprintf(stderr, "[plant] 植物数据装载失败，回退占位网格\n");
        g_loaded = 1;
        g_species = 0;
        return 0;
    }
    g_geo = calloc((size_t)n * PLANT_FORM_COUNT * PD_SEASON_COUNT, sizeof(*g_geo));
    if (!g_geo) {
        qlcg_pd_unload();
        return 0;
    }
    g_species = n;
    g_loaded = 1;
    return n;
}

void qlcg_plant_material_shutdown(void) {
    for (size_t i = 0; i < (size_t)g_species * PLANT_FORM_COUNT * PD_SEASON_COUNT; i++) {
        if (g_geo[i].ready) qlcg_lp_geometry_free(&g_geo[i].geo);
    }
    free(g_geo);
    g_geo = NULL;
    if (g_loaded) qlcg_pd_unload();
    g_loaded = 0;
    g_species = 0;
}

int qlcg_plant_species_count(void) {
    return g_species;
}

int qlcg_plant_species_index(const char *id) {
    if (!id || !g_species) return -1;
    const PdSpecies *sp = qlcg_pd_species_by_id(id);
    if (!sp) return -1;
    for (int i = 0; i < g_species; i++) {
        const PdSpecies *cur = qlcg_pd_species(i);
        if (cur == sp) return i;
    }
    return -1;
}

const char *qlcg_plant_species_id(int index) {
    const PdSpecies *sp = qlcg_pd_species(index);
    return sp ? sp->base.id : NULL;
}

int qlcg_plant_species_category(int index) {
    const PdSpecies *sp = qlcg_pd_species(index);
    return sp ? sp->category : -1;
}

static PlantGeoSlot *plant_slot(int species, int form, int season) {
    if (species < 0 || species >= g_species) return NULL;
    if (season < 0 || season >= PD_SEASON_COUNT) season = PD_SEASON_SUMMER;
    int variant = qlcg_plant_model_variant(form);
    if (variant && !qlcg_plant_category_has_variants(qlcg_plant_species_category(species))) variant = 0;
    int slot_form = form == VEG_FORM_SIMPLE ? 0 : 1 + variant;

    PlantGeoSlot *s = &g_geo[((size_t)species * PLANT_FORM_COUNT + slot_form) * PD_SEASON_COUNT + season];
    if (s->ready || s->failed) return s->ready ? s : NULL;

    const PdSpecies *sp = qlcg_pd_species(species);
    if (!sp || !sp->base.id) { s->failed = 1; return NULL; }

    /* 成株条目；季节决定花色与积雪 */
    if (!qlcg_plant_build(sp->base.id, season, form, &s->geo)) {
        s->failed = 1;
        return NULL;
    }
    qlcg_collision_bounds(&s->shape,s->geo.positions,s->geo.vertex_count);
    PdDef def;
    if (qlcg_pd_build_def_ex(sp->base.id,season,1,&def)) {
        DecorationShape *v=&s->shape;
        const char *a=def.archetype;
        if (variant || !qlcg_collision_stable(sp->base.id,season,v)) {
            if (!strncmp(sp->base.id,"guan.",5)) {
                v->rx*=.7f; v->rz*=.7f; v->kind=SOFT_SHRUB;
            } else if (strncmp(sp->base.id,"shui.",5) &&
                (!strcmp(a,"conifer") || !strcmp(a,"broadleaf") || !strcmp(a,"palm") ||
                 !strcmp(a,"acacia") || !strcmp(a,"baobab") || !strcmp(a,"blossomTree") ||
                 !strcmp(a,"bamboo") || !strcmp(a,"sapling"))) {
                float height=(float)pd_num(&def.params,"height",s->geo.height);
                float trunk=(float)pd_num(&def.params,"trunk",height*.55f);
                float radius=!strcmp(a,"bamboo") ? fminf(.6f,(float)pd_num(&def.params,"spread",.5)*.5f+.1f)
                    : (float)pd_num(&def.params,"trunkRadius",height*.035f)*1.3f;
                v->x=(float)pd_num(&def.params,"lean",0)*trunk*.125f; v->z=0; v->bottom=0;
                v->top=fminf(v->top,!strcmp(a,"conifer") ? trunk+(height-trunk)*.7f : !strcmp(a,"bamboo") ? height : trunk);
                v->rx=v->rz=radius; v->kind=HARD_TRUNK;
            } else {
                v->rx=v->rz=fmaxf(.08f,fminf(.6f,fmaxf(v->rx,v->rz)*.4f));
                v->top=fmaxf(v->top,v->bottom+.05f); v->kind=PLANT_TRIGGER;
            }
        }
        qlcg_pd_free_def(&def);
    }
    s->ready = 1;
    return s;
}

const LpGeometry *qlcg_plant_geometry(int species, int form, int season) {
    const PlantGeoSlot *s = plant_slot(species, form, season);
    return s ? &s->geo : NULL;
}

const DecorationShape *qlcg_plant_collision_shape(int index, int form, int season) {
    const PlantGeoSlot *s = plant_slot(index, form, season);
    return s ? &s->shape : NULL;
}
