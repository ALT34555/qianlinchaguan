#ifndef QLCG_CIV_CONTENT_H
#define QLCG_CIV_CONTENT_H

#include "../world/planet_macro.h"
#include <stddef.h>
#include <stdint.h>

enum { QLCG_CIV_TYPE_ID=64, QLCG_CIV_TYPE_NAME=96, QLCG_CIV_ROLE=16,
       QLCG_FARMLAND_TYPES=64, QLCG_CROP_TYPES=128, QLCG_CLIMATE_TYPES=128,
       QLCG_CROP_CLIMATES=8, QLCG_FARMLAND_SIDE=30 };

typedef struct { float min,max; } QlcgCivRange;

typedef struct {
    char id[QLCG_CIV_TYPE_ID];
    char name[QLCG_CIV_TYPE_NAME];
    uint32_t soil_rgb;
    float width,depth,ridge_period,ridge_strength;
    int priority,chunk_type;
} QlcgFarmlandType;

typedef struct {
    char id[QLCG_CIV_TYPE_ID];
    char name[QLCG_CIV_TYPE_NAME];
    char plant_id[QLCG_CIV_TYPE_ID];
    char farmland_id[QLCG_CIV_TYPE_ID];
    char role[QLCG_CIV_ROLE];
    int annual_food,annual_wood,annual_fiber;
    int maturity_days,continuous_days,priority;
    bool selectable;
    QlcgCivRange mean_temp,amplitude,temperature,moisture;
    uint32_t base_zone_mask;
    char climate_ids[QLCG_CROP_CLIMATES][QLCG_CIV_TYPE_ID];
    int climate_count;
} QlcgCropType;

typedef struct {
    char id[QLCG_CIV_TYPE_ID];
    char name[QLCG_CIV_TYPE_NAME];
    int base_zone,priority;
    uint32_t color_rgb;
    QlcgCivRange mean_temp,amplitude,moisture,rainfall,monsoon;
    uint32_t match_zone_mask;
} QlcgClimateType;

bool qlcg_civ_content_load(const char *builtin_dir,const char *mod_dir,char *error,size_t capacity);
bool qlcg_civ_content_ready(void);
void qlcg_civ_content_free(void);
int qlcg_farmland_type_count(void);
int qlcg_crop_type_count(void);
int qlcg_climate_type_count(void);
const QlcgFarmlandType *qlcg_farmland_type_at(int index);
const QlcgCropType *qlcg_crop_type_at(int index);
const QlcgClimateType *qlcg_climate_type_at(int index);
const QlcgFarmlandType *qlcg_farmland_type_find(const char *id);
const QlcgCropType *qlcg_crop_type_find(const char *id);
const QlcgClimateType *qlcg_climate_type_find(const char *id);
const QlcgCropType *qlcg_crop_type_select(const MacroWorld *world,int cell,const char *role);
int qlcg_climate_type_match(float mean,float amplitude,float moisture,float rainfall,float monsoon,int base_zone);

#endif
