#ifndef QLCG_MEASUREMENTS_H
#define QLCG_MEASUREMENTS_H

#include "config.h"

/* 度量衡与身高换算接口（对齐 base） */
const char *qlcg_meas_system_name(MeasurementSystem sys);
const char *qlcg_meas_height_unit(MeasurementSystem sys);
void        qlcg_format_player_height(MeasurementSystem sys, int height_cm, char *buf, size_t cap);
void        qlcg_format_distance(MeasurementSystem sys, float meters, char *buf, size_t cap);
void        qlcg_format_elevation(MeasurementSystem sys, float meters, char *buf, size_t cap);
void        qlcg_format_area(MeasurementSystem sys, float sq_meters, char *buf, size_t cap);
void        qlcg_profile_height_range(PlayMode mode, int *min_cm, int *max_cm);

#endif /* QLCG_MEASUREMENTS_H */
