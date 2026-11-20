#include "shore_models.h"
#include "plant_forms.h"
#include "rock_def.h"

#include <string.h>

typedef struct {
    const int16_t *xyz;
    const uint8_t *rgb;
    int triangles;
    double height, radius;
} ShoreMesh;

typedef struct {
    const char *id;
    ShoreMesh forms[SHORE_FORM_COUNT];
} ShorePlant;

typedef struct {
    CoralModel model;
    ShoreMesh forms[SHORE_FORM_COUNT];
} ShoreCoral;

#include "shore_mesh_data.inc"

#define ARRAY_LEN(a) (sizeof(a)/sizeof((a)[0]))
#define SHORE_COORD_UNIT 4096.0
_Static_assert(ARRAY_LEN(corals) == CORAL_MODEL_COUNT, "coral catalog count");
_Static_assert(ARRAY_LEN(water_plants) == SHORE_PLANT_COUNT, "water plants catalog count");
_Static_assert((int)CORAL_ARCH_BASE >= (int)ROCK_CATALOG_CAPACITY && CORAL_ARCH_BASE + CORAL_MODEL_COUNT <= 256,
               "coral archetypes must fit in the prop index");
static RockGeometry coral_cache[CORAL_MODEL_COUNT][SHORE_FORM_COUNT][CORAL_PALETTE_COUNT];

int qlcg_shore_model_form(int plant_form) {
    if (plant_form == VEG_FORM_SIMPLE) return SHORE_SIMPLE;
    return qlcg_plant_model_variant(plant_form);
}

int qlcg_shore_plant_index(const char *id) {
    if (!id || strncmp(id, "shui.", 5)) return -1;
    size_t len = strcspn(id, "@");
    for (int i = 0; i < (int)ARRAY_LEN(water_plants); i++) {
        const char *key = water_plants[i].id;
        if (strlen(key) == len && !memcmp(id, key, len)) return i;
    }
    return -1;
}

static void face_xyz(const ShoreMesh *mesh, int face, double p[3][3]) {
    for (int j = 0; j < 3; j++)
        for (int k = 0; k < 3; k++)
            p[j][k] = mesh->xyz[face*9+j*3+k] / SHORE_COORD_UNIT;
}

int qlcg_shore_plant_geometry(int index, int form, LpGeometry *out) {
    if ((unsigned)index >= ARRAY_LEN(water_plants) || form < 0 || form >= SHORE_FORM_COUNT) return 0;
    const ShoreMesh *mesh = &water_plants[index].forms[form];
    LpBuilder builder;
    qlcg_lp_init(&builder);
    for (int i = 0; i < mesh->triangles; i++) {
        double p[3][3];
        face_xyz(mesh, i, p);
        const uint8_t *rgb = mesh->rgb + 3*i;
        int before = builder.count;
        qlcg_lp_triangle(&builder, p[0], p[1], p[2], qlcg_lp_rgb(rgb[0],rgb[1],rgb[2]), LP_MAT_SOLID);
        if (builder.count > before) memcpy(builder.faces[before].col, rgb, 3);
    }
    qlcg_lp_build(&builder, out);
    qlcg_lp_free(&builder);
    if (out->triangle_count == mesh->triangles) return 1;
    qlcg_lp_geometry_free(out);
    return 0;
}

const CoralModel *qlcg_coral_model(int index) {
    return (unsigned)index < CORAL_MODEL_COUNT ? &corals[index].model : NULL;
}

int qlcg_coral_index(int archetype) {
    return archetype >= CORAL_ARCH_BASE && archetype < CORAL_ARCH_BASE + CORAL_MODEL_COUNT
        ? archetype - CORAL_ARCH_BASE : -1;
}

float qlcg_coral_height(int index) {
    return (unsigned)index < CORAL_MODEL_COUNT ? (float)corals[index].forms[SHORE_BASE].height : 0.0f;
}

const RockGeometry *qlcg_coral_geometry(int index, int form, int palette) {
    if (!qlcg_coral_model(index) || (unsigned)form >= SHORE_FORM_COUNT || (unsigned)palette >= CORAL_PALETTE_COUNT)
        return NULL;
    RockGeometry *geometry = &coral_cache[index][form][palette];
    if (geometry->triangle_count) return geometry;
    const ShoreMesh *mesh = &corals[index].forms[form];
    const uint8_t *colors = mesh->rgb + 3*mesh->triangles*palette;
    RockBuilder builder;
    qlcg_rock_builder_init(&builder, NULL);
    for (int i = 0; i < mesh->triangles; i++) {
        double p[3][3];
        face_xyz(mesh, i, p);
        const uint8_t *rgb = colors + 3*i;
        double normal[3];
        qlcg_lp_face_normal(p[0], p[1], p[2], normal);
        qlcg_rock_push(&builder, p[0], 3, qlcg_lp_rgb(rgb[0],rgb[1],rgb[2]), normal, ROCK_MAT_SOLID, 1);
    }
    qlcg_rock_build(&builder, geometry);
    qlcg_rock_builder_free(&builder);
    if (geometry->triangle_count == mesh->triangles) {
        geometry->radius = mesh->radius;
        return geometry;
    }
    qlcg_rock_geometry_free(geometry);
    return NULL;
}

void qlcg_coral_shutdown(void) {
    for (int i = 0; i < CORAL_MODEL_COUNT; i++)
        for (int form = 0; form < SHORE_FORM_COUNT; form++)
            for (int palette = 0; palette < CORAL_PALETTE_COUNT; palette++)
                qlcg_rock_geometry_free(&coral_cache[i][form][palette]);
}
