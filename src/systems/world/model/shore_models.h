#ifndef QLCG_SHORE_MODELS_H
#define QLCG_SHORE_MODELS_H

#include "rock_builder.h"

enum { SHORE_BASE, SHORE_VARIANT_1, SHORE_VARIANT_2, SHORE_SIMPLE, SHORE_FORM_COUNT };
enum { SHORE_PLANT_COUNT = 8 };
enum { CORAL_SMALL, CORAL_MEDIUM, CORAL_LARGE };
enum { CORAL_MODEL_COUNT = 24, CORAL_PALETTE_COUNT = 6, CORAL_WITHERED = 5, CORAL_ARCH_BASE = 128 };

typedef struct {
    const char *id, *name;
    int size;
} CoralModel;

int qlcg_shore_model_form(int plant_form);
int qlcg_shore_plant_index(const char *id);
int qlcg_shore_plant_geometry(int index, int form, LpGeometry *out);
const CoralModel *qlcg_coral_model(int index);
int qlcg_coral_index(int archetype);
float qlcg_coral_height(int index);
const RockGeometry *qlcg_coral_geometry(int index, int form, int palette);
void qlcg_coral_shutdown(void);

#endif
