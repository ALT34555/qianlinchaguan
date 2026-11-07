/* 河道尺寸分档（river-dimensions） */
#ifndef QLCG_GEN_RIVER_H
#define QLCG_GEN_RIVER_H

/* 小尺度成河门槛，随集水尺度与降水缩放 */
double qlcg_macro_river_threshold(double scale, double precipitation);

/* 河道全宽（米） */
double qlcg_macro_band_width(double discharge);

/* 主槽深度（米） */
double qlcg_macro_depth(double discharge);
double qlcg_river_discharge_width(double discharge);
double qlcg_river_friction_slope(double discharge,double width,double depth,double friction);
double qlcg_river_mean_speed(double discharge,double width,double depth);

#endif
