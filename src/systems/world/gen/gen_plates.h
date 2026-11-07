/* 板块布局与漂移（base plate-layout） */
#ifndef QLCG_GEN_PLATES_H
#define QLCG_GEN_PLATES_H

#include "../planet_macro.h"

/* major=round(cols/64)，最小 4 */
/* minor=round(major/4)，最小 1 */
void qlcg_planet_size(int columns, int *major, int *minor);

/* 漂移剖面，activity 0..9 */
typedef struct {
    float span;
    float speed;
    float dispersion;
    float belt_width;
    float uplift_scale;
    float rift_depth;
    float steps;
} DriftProfile;

DriftProfile qlcg_drift_profile(int activity);

/* 主板块在前，交界小板块随后，返回总数 */
int  qlcg_create_plates(TectonicPlate *plates, int cap, const WorldCreatorOptions *opt, int major, int minor);

/* 按 warp 后的点位归属板块 */
void qlcg_assign_plate_ids(const float *points, const TectonicPlate *plates, int total,
                           int major, uint16_t *ids, int count);

#endif
