#include "cliff_rock.h"
#include "rock_arch.h"
#include "model_json.h"
#include "../surface_macro.h"

#include <math.h>
#include <stdio.h>

enum { MODELS_PER_SET = 8 };
static const char *const COLOR_KEYS[CLIFF_COLOR_COUNT] = {
    "blue", "coolGray", "warmGray", "coolWhite", "warmWhite", "black"
};
static const char *const SLOTS[] = {"body", "band", "cap", "base"};

typedef struct {
    const char *path, *key;
    char *archetype;
    JsVal *catalog;
    RockGeometry geometry[MODELS_PER_SET][CLIFF_COLOR_COUNT];
    bool loaded;
} StoneModelSet;

static StoneModelSet g_cliffs = {
    .path = "content/data/world/cliffs/cliffs.json", .key = "cliffs", .archetype = "escarpment"
};
static StoneModelSet g_stone_forest = {
    .path = "content/data/world/cliffs/stone-forest.json", .key = "blocks", .archetype = "stoneForest"
};

static const JsVal *load_catalog(StoneModelSet *set) {
    if (!set->loaded) {
        set->loaded = true;
        set->catalog = qlcg_js_load(set->path, NULL);
    }
    return set->catalog;
}

static const JsVal *models(StoneModelSet *set) {
    return qlcg_js_get(load_catalog(set), set->key);
}

static int model_count(StoneModelSet *set) {
    int count = qlcg_js_count(models(set));
    return count <= MODELS_PER_SET ? count : MODELS_PER_SET;
}

static const char *model_id(StoneModelSet *set, int index) {
    if (index < 0 || index >= model_count(set)) return NULL;
    return qlcg_js_str(qlcg_js_get(qlcg_js_at(models(set), index), "id"), NULL);
}

CliffRockColor qlcg_cliff_color_at(const MacroWorld *w, double wx, double wz) {
    if (!w || w->columns < 1 || w->rows < 1 || !(w->circumference > 0) ||
        !isfinite(wx) || !isfinite(wz)) return CLIFF_WARM_GRAY;
    SurfaceMacroContext c = qlcg_macro_context(w, wx, wz);
    double rift = w->rift ? qlcg_read_macro(w, &c, w->rift) : 0.0;
    double shear = w->shear ? qlcg_read_macro(w, &c, w->shear) : 0.0;
    double uplift = w->uplift ? qlcg_read_macro(w, &c, w->uplift) : 0.0;
    double basin = w->basin ? qlcg_read_macro(w, &c, w->basin) : 0.0;
    if (rift >= .32) return CLIFF_BLACK;
    if (shear >= .34) return CLIFF_BLUE;
    if (uplift >= .44) return shear >= .14 ? CLIFF_COOL_WHITE : CLIFF_COOL_GRAY;
    return basin >= .38 ? CLIFF_WARM_WHITE : CLIFF_WARM_GRAY;
}

static const RockGeometry *model_geometry(StoneModelSet *set, int index, CliffRockColor color) {
    const char *id = model_id(set, index);
    if (!id || (unsigned)color >= CLIFF_COLOR_COUNT) return NULL;
    RockGeometry *geometry = &set->geometry[index][color];
    if (geometry->vertex_count) return geometry;

    const JsVal *palette = qlcg_js_get(qlcg_js_get(load_catalog(&g_cliffs), "colors"), COLOR_KEYS[color]);
    const char *hex[sizeof(SLOTS) / sizeof(SLOTS[0])];
    for (int i = 0; i < (int)(sizeof(SLOTS) / sizeof(SLOTS[0])); i++) {
        hex[i] = qlcg_js_str(qlcg_js_get(palette, SLOTS[i]), NULL);
        if (!hex[i]) return NULL;
    }
    const JsVal *raw = qlcg_js_at(models(set), index);
    double height = qlcg_js_num(qlcg_js_get(raw, "heightMeters"), 0.0);
    if (!(height > 0.0) || !isfinite(height)) return NULL;
    RockBuildDef def = {.archetype = set->archetype, .height_m = height, .form = ROCK_FORM_OUTCROP};
    char key[160];
    double seed = qlcg_js_num(qlcg_js_get(raw, "seed"), 0.0);
    snprintf(key, sizeof(key), "%s:%s:%.0f", set->archetype, id, seed);
    def.build_seed = qlcg_lp_hash_string(key);
    const JsVal *params = qlcg_js_get(raw, "params");
    if (params && params->type == JS_OBJ)
        for (int i = 0; i < params->count; i++)
            if (params->items[i]->type == JS_NUM)
                pd_set_num(&def.params, params->keys[i], params->items[i]->num);
    for (int i = 0; i < (int)(sizeof(SLOTS) / sizeof(SLOTS[0])); i++)
        pd_set_str(&def.palette, SLOTS[i], hex[i]);

    int ok = qlcg_rock_arch_build(&def, geometry);
    pd_free(&def.params);
    pd_free(&def.palette);
    return ok ? geometry : NULL;
}

int qlcg_cliff_count(void) { return model_count(&g_cliffs); }
const char *qlcg_cliff_id(int index) { return model_id(&g_cliffs, index); }
const RockGeometry *qlcg_cliff_geometry(int index, CliffRockColor color) {
    return model_geometry(&g_cliffs, index, color);
}

const RockGeometry *qlcg_cliff_geometry_at(int index, const MacroWorld *w, double wx, double wz) {
    return qlcg_cliff_geometry(index, qlcg_cliff_color_at(w, wx, wz));
}

int qlcg_stone_forest_count(void) { return model_count(&g_stone_forest); }
const char *qlcg_stone_forest_id(int index) { return model_id(&g_stone_forest, index); }
const RockGeometry *qlcg_stone_forest_geometry(int index, CliffRockColor color) {
    return model_geometry(&g_stone_forest, index, color);
}

const RockGeometry *qlcg_stone_forest_geometry_at(int index, const MacroWorld *w, double wx, double wz) {
    return qlcg_stone_forest_geometry(index, qlcg_cliff_color_at(w, wx, wz));
}

static void unload_catalog(StoneModelSet *set) {
    for (int i = 0; i < MODELS_PER_SET; i++)
        for (int color = 0; color < CLIFF_COLOR_COUNT; color++)
            qlcg_rock_geometry_free(&set->geometry[i][color]);
    qlcg_js_free(set->catalog);
    set->catalog = NULL;
    set->loaded = false;
}

void qlcg_cliff_shutdown(void) {
    unload_catalog(&g_stone_forest);
    unload_catalog(&g_cliffs);
}
