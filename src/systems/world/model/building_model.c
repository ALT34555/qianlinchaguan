#include "building_model.h"
#include <string.h>

#include "building_model_data.inc"

int qlcg_building_model_count(void) {
    return (int)(sizeof(building_models) / sizeof(building_models[0]));
}

const QlcgBuildingModel *qlcg_building_model_at(int index) {
    return index >= 0 && index < qlcg_building_model_count() ? &building_models[index] : NULL;
}

const QlcgBuildingModel *qlcg_building_model_find(const char *id) {
    if (!id) return NULL;
    for (int i = 0; i < qlcg_building_model_count(); i++)
        if (strcmp(building_models[i].id, id) == 0) return &building_models[i];
    return NULL;
}
