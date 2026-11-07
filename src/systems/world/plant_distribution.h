/* 自然植物分布：单站点快照与选种入口 */
#ifndef QLCG_PLANT_DISTRIBUTION_H
#define QLCG_PLANT_DISTRIBUTION_H

#include <stdint.h>

typedef struct {
    float temperature;
    float wetness;
    float desert;
    float forest;
    float aridity;
    float slope;

    float water_depth;
    float flow;
    float saline;

    int water_kind;
    int material;   /* 1岩 2土 3盐碱 4沙 5砾 */
} PlantSite;

typedef struct {
    float density, rocks, grove;
    float warp_x, warp_z;
    uint32_t seed;
} PlantPatch;

void qlcg_plant_patch(double x, double z, uint32_t seed, PlantPatch *patch);
int qlcg_plant_pick_patch(const PlantSite *site, uint32_t roll,
                          double x, double z, const PlantPatch *patch);

/* 建立/刷新物种档案，返回物种数 */
int qlcg_plant_habitat_prepare(void);
/* 0..1；人工类、未知、水陆不符返回 0 */
float qlcg_plant_suitability(int species, const PlantSite *site);
/* 按适宜度选基种；失败返回 -1 */
int qlcg_plant_pick(const PlantSite *site, uint32_t roll);

#endif
