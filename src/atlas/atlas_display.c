/* 图层状态、默认值与兼容修复 */
#include "../systems/world/planet_macro.h"
#include <math.h>
#include <string.h>

static const AtlasLayerSetting DEFAULT_LAYERS[LAYER_ID_COUNT] = {
    { LAYER_CIVILIZATION, false, 1.0f },
    { LAYER_CHUNKS, false, 1.0f },
    { LAYER_BASELINE, false, 1.0f },
    { LAYER_DAYLIGHT, true, 0.5f },
    { LAYER_RAIN, false, 0.55f },
    { LAYER_CONTOUR, false, 1.0f },
    { LAYER_CLIMATE, false, 0.5f },
    { LAYER_TEMPERATURE, false, 0.45f },
    { LAYER_HEIGHT, false, 0.5f },
    { LAYER_ICE, true, 1.0f },
    { LAYER_DEPTH, false, 1.0f },
    { LAYER_SOURCE_RIVERS, true, 1.0f }
};

void qlcg_init_atlas_display(AtlasDisplayState *st) {
    memset(st, 0, sizeof(*st));
    memcpy(st->layers, DEFAULT_LAYERS, sizeof(DEFAULT_LAYERS));
    st->contour_density = 2;
}

void qlcg_normalize_atlas_display(AtlasDisplayState *st) {
    AtlasDisplayState clean;
    qlcg_init_atlas_display(&clean);
    bool seen[LAYER_ID_COUNT] = { false };
    int count = 0;
    for (int i = 0; i < LAYER_ID_COUNT; i++) {
        AtlasLayerSetting layer = st->layers[i];
        int id = (int)layer.id;
        if (id < 0 || id >= LAYER_ID_COUNT || seen[id]) continue;
        seen[id] = true;
        if (!isfinite(layer.opacity)) layer.opacity = DEFAULT_LAYERS[id].opacity;
        layer.opacity = fminf(1.0f, fmaxf(0.0f, layer.opacity));
        clean.layers[count++] = layer;
    }
    for (int id = 0; id < LAYER_ID_COUNT; id++) {
        if (seen[id]) continue;
        if (id == LAYER_CIVILIZATION) {
            memmove(clean.layers + 1, clean.layers, (size_t)count * sizeof(clean.layers[0]));
            clean.layers[0] = DEFAULT_LAYERS[id];
        } else {
            clean.layers[count] = DEFAULT_LAYERS[id];
        }
        count++;
    }
    clean.contour_density = st->contour_density < 0 ? 0 :
                           (st->contour_density > 4 ? 4 : st->contour_density);
    *st = clean;
}

bool qlcg_atlas_layer_move(AtlasDisplayState *st, int from, int to) {
    if (from < 0 || from >= LAYER_ID_COUNT || to < 0 || to >= LAYER_ID_COUNT || from == to)
        return false;
    AtlasLayerSetting layer = st->layers[from];
    if (from < to) {
        memmove(st->layers + from, st->layers + from + 1, (size_t)(to - from) * sizeof(layer));
    } else {
        memmove(st->layers + to + 1, st->layers + to, (size_t)(from - to) * sizeof(layer));
    }
    st->layers[to] = layer;
    return true;
}

float qlcg_atlas_layer_opacity(const AtlasDisplayState *st, AtlasLayerId id) {
    for (int i = 0; i < LAYER_ID_COUNT; i++) {
        if (st->layers[i].id == id) return st->layers[i].on ? st->layers[i].opacity : 0.0f;
    }
    return 0.0f;
}

bool qlcg_atlas_layer_has_opacity(AtlasLayerId id) {
    return id == LAYER_SOURCE_RIVERS || id == LAYER_CIVILIZATION || id == LAYER_DAYLIGHT || id == LAYER_RAIN ||
           id == LAYER_CLIMATE || id == LAYER_TEMPERATURE || id == LAYER_HEIGHT;
}
