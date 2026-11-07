/* 降水与蒸发的唯一换算口径 */
#include "gen_water.h"
#include <math.h>

double qlcg_runoff_mm(double rainfall, double evaporation) {
    double p = fmax(0.0, rainfall);
    double e = fmax(0.0, evaporation);
    if (p <= 0.0) return 0.0;
    double share = p / (p + e + 1e-9);
    return p * share * sqrt(share);
}

double qlcg_runoff_m3s(double rainfall, double evaporation, double area_km2) {
    return qlcg_runoff_mm(rainfall, evaporation) * area_km2 * 1000.0 / (double)QLCG_YEAR_SECONDS;
}

double qlcg_evaporation_m3s(double evaporation, double area_km2) {
    return fmax(0.0, evaporation) * area_km2 * 1000.0 / (double)QLCG_YEAR_SECONDS;
}
