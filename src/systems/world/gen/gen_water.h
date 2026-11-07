/* 水量口径（对齐 water-balance.mjs） */
#ifndef QLCG_GEN_WATER_H
#define QLCG_GEN_WATER_H

#define QLCG_YEAR_SECONDS 31557600.0

/* 径流深度 mm/年 */
double qlcg_runoff_mm(double rainfall, double evaporation);

/* 径流流量 m³/s */
double qlcg_runoff_m3s(double rainfall, double evaporation, double area_km2);

/* 潜在蒸发流量 m³/s */
double qlcg_evaporation_m3s(double evaporation, double area_km2);

#endif
