/* 河道宽度与深度按流量分档，档间对数插值 */
#include "gen_river.h"
#include "../../../core/math_util.h"
#include <math.h>

static double clampd(double v, double a, double b) { return fmax(a, fmin(b, v)); }

#define RIVER_SMALL_MAIN_DEPTH 1.6
#define RIVER_LARGE_MAIN_MAX   14.0

static double smooth01(double t) {
    t = clampd(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double qlcg_macro_river_threshold(double scale, double precipitation) {
    double s = fmax(0.01, scale);
    return 0.96 * s * fmin(1.0, s / 625.0)
           * pow(fmax(0.2, precipitation), 0.6);
}

double qlcg_macro_band_width(double discharge) {
    static const double START[4] = { 0.0, 10.0, 1000.0, 10000.0 };
    static const double END[4]   = { 10.0, 1000.0, 10000.0, 100000.0 };
    static const double LOW[4]   = { 16.0, 28.0, 72.0, 180.0 };
    static const double HIGH[4]  = { 28.0, 72.0, 180.0, 420.0 };
    double q = fmax(0.0, discharge);
    for (int k = 0; k < 4; k++) {
        if (q <= END[k]) {
            double t = clampd(log1p(fmax(0.0, q - START[k])) / log1p(END[k] - START[k]), 0.0, 1.0);
            return LOW[k] + (HIGH[k] - LOW[k]) * smooth01(t);
        }
    }
    return 420.0;
}

double qlcg_macro_depth(double discharge) {
    static const double START[3] = { 0.0, 10.0, 1000.0 };
    static const double END[3]   = { 10.0, 1000.0, 100000.0 };
    static const double LOW[3]   = { RIVER_SMALL_MAIN_DEPTH, 3.5, 6.5 };
    static const double HIGH[3]  = { 3.5, 6.5, RIVER_LARGE_MAIN_MAX };
    double q = fmax(0.0, discharge);
    for (int k = 0; k < 3; k++) {
        if (q <= END[k]) {
            double t = clampd(log1p(fmax(0.0, q - START[k])) / log1p(END[k] - START[k]), 0.0, 1.0);
            return LOW[k] + (HIGH[k] - LOW[k]) * smooth01(t);
        }
    }
    return RIVER_LARGE_MAIN_MAX;
}

double qlcg_river_discharge_width(double discharge) {
    static const double flow[]={10,100,300,650,2000,10000,30000,43100,100000};
    static const double width[]={28,96,288,480,640,1800,3000,3800,6000.0};
    if (discharge<=flow[0]) return qlcg_macro_band_width(discharge);
    for (int i=1;i<sizeof(flow)/sizeof(*flow);i++) {
        if (discharge>flow[i]) continue;
        double t=log(discharge/flow[i-1])/log(flow[i]/flow[i-1]);
        double open=width[i-1]*pow(width[i]/width[i-1],t);
        return fmax(qlcg_macro_band_width(discharge),open);
    }
    return 6000.0;
}
double qlcg_river_friction_slope(double discharge,double width,double depth,double friction) {
    double area=fmax(.01,width*depth*.62),radius=area/fmax(.1,width+2*depth);
    double speed=qlcg_river_mean_speed(discharge,width,depth);
    double roughness=.028*sqrt(clampd(friction,.25,4));
    double grade=roughness*speed/pow(fmax(.05,radius),2.0/3);
    return grade*grade;
}
double qlcg_river_mean_speed(double discharge,double width,double depth) {
    return fmax(0,discharge)/fmax(.01,width*depth*.62);
}
