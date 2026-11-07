/* 大陆壳场，对齐 continental-crust */
#ifndef QLCG_GEN_CRUST_H
#define QLCG_GEN_CRUST_H

#include "../planet_macro.h"

#define QLCG_CRUST_MAX 16

typedef struct {
    int   count;
    float cx[QLCG_CRUST_MAX], cy[QLCG_CRUST_MAX], cz[QLCG_CRUST_MAX];
    float cb[QLCG_CRUST_MAX];
    float chords[QLCG_CRUST_MAX * QLCG_CRUST_MAX];
    float half_width;      /* 弧度为单位的盆地半宽 */
    float max_sin;
    float reference;       /* 海平面参考分位 */
    float angle;           /* 漂移总角 */
    uint32_t seed_basin, seed_crust, seed_coast;
} CrustField;

/* 大陆核与参考分位；angle 取 drift.span */
void  qlcg_crust_build(CrustField *cf, const MacroWorld *w, const TectonicPlate *plates,
                       int plate_count, const WorldCreatorOptions *opt, float angle);

/* 球面点的大陆得分；basin 与 core 可空 */
float qlcg_crust_score(const CrustField *cf, float px, float py, float pz,
                       float *out_basin, int *out_core);

/* 反推板块漂移后的大陆度 0..1 */
float qlcg_crust_continental_xyz(const CrustField *cf, float px, float py, float pz,
                                 const TectonicPlate *plate, float fraction);

/* 大陆核数量（供统计） */
int   qlcg_crust_core_count(const CrustField *cf);

#endif
