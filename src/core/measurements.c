#include "measurements.h"
#include <stdio.h>
#include <math.h>
#include "i18n.h"

/* 获取度量衡显示名称 */
const char *qlcg_meas_system_name(MeasurementSystem sys) {
    switch (sys) {
        case MEAS_MING:     return tr("settings.measurementSystem.ming");
        case MEAS_METRIC:   return tr("settings.measurementSystem.metric");
        case MEAS_IMPERIAL: return tr("settings.measurementSystem.imperial");
        default:            return tr("settings.measurementSystem.ming");
    }
}

/* 获取身高单位后缀 */
const char *qlcg_meas_height_unit(MeasurementSystem sys) {
    switch (sys) {
        case MEAS_MING:     return tr("measurement.height.ming");
        case MEAS_METRIC:   return "cm";
        case MEAS_IMPERIAL: return "in";
        default:            return "cm";
    }
}

/* 格式化玩家身高（营造尺1尺=32cm） */
void qlcg_format_player_height(MeasurementSystem sys, int height_cm, char *buf, size_t cap) {
    if (sys == MEAS_MING) {
        snprintf(buf, cap, "%.2f%s (%d cm)", (float)height_cm / 32.0f, tr("measure.unit.chi"), height_cm);
    } else if (sys == MEAS_IMPERIAL) {
        snprintf(buf, cap, "%.1f in (%d cm)", (float)height_cm / 2.54f, height_cm);
    } else {
        snprintf(buf, cap, "%d cm", height_cm);
    }
}

/* 格式化距离（尺/丈/里、m/km、ft/ */
void qlcg_format_distance(MeasurementSystem sys, float meters, char *buf, size_t cap) {
    float a = fabsf(meters);
    if (sys == MEAS_MING) {
        float zhang = a / 3.2f;
        if (zhang >= 150.0f) {
            snprintf(buf, cap, "%.1f%s", zhang / 150.0f, tr("measure.unit.li"));
        } else if (zhang >= 1.0f) {
            snprintf(buf, cap, "%.1f%s", zhang, tr("measure.unit.zhang"));
        } else {
            snprintf(buf, cap, "%.1f%s", a / 0.32f, tr("measure.unit.chi"));
        }
    } else if (sys == MEAS_IMPERIAL) {
        float ft = a * 3.28084f;
        if (ft >= 5280.0f) {
            snprintf(buf, cap, "%.1f mi", ft / 5280.0f);
        } else {
            snprintf(buf, cap, "%.0f ft", ft);
        }
    } else {
        if (a >= 1000.0f) {
            snprintf(buf, cap, "%.1f km", a / 1000.0f);
        } else {
            snprintf(buf, cap, "%.0f m", a);
        }
    }
}

/* 格式化海拔高程 */
void qlcg_format_elevation(MeasurementSystem sys, float meters, char *buf, size_t cap) {
    if (sys == MEAS_MING) {
        snprintf(buf, cap, "%.1f%s", meters / 3.2f, tr("measure.unit.zhang"));
    } else if (sys == MEAS_IMPERIAL) {
        snprintf(buf, cap, "%.0f ft", meters * 3.28084f);
    } else {
        snprintf(buf, cap, "%.0f m", meters);
    }
}

/* 格式化面积（亩/顷、m²/km²、ac） */
void qlcg_format_area(MeasurementSystem sys, float sq_meters, char *buf, size_t cap) {
    if (sys == MEAS_MING) {
        float mu = sq_meters / 614.4f;
        if (mu >= 100.0f) {
            snprintf(buf, cap, "%.2f%s", mu / 100.0f, tr("measure.unit.qing"));
        } else {
            snprintf(buf, cap, "%.1f%s", mu, tr("measure.unit.mu"));
        }
    } else if (sys == MEAS_IMPERIAL) {
        snprintf(buf, cap, "%.1f ac", sq_meters * 0.000247105f);
    } else {
        if (sq_meters >= 1e6f) {
            snprintf(buf, cap, "%.2f km²", sq_meters / 1e6f);
        } else {
            snprintf(buf, cap, "%.0f m²", sq_meters);
        }
    }
}

/* 按模式返回允许的身高范围 */
void qlcg_profile_height_range(PlayMode mode, int *min_cm, int *max_cm) {
    if (mode == PLAY_MODE_SURVIVAL) {
        *min_cm = 140;
        *max_cm = 210;
    } else {
        *min_cm = 50;
        *max_cm = 500;
    }
}
