#ifndef QLCG_SURFACE_FRAME_H
#define QLCG_SURFACE_FRAME_H
#include "planet_macro.h"

typedef struct { double x,z; } SurfacePoint;
typedef struct { double lon,lat,anchor[3],east[3],south[3],radius,C; bool flat; } SurfaceFrame;
SurfaceFrame qlcg_surface_frame(const MacroWorld *w,int cell);
SurfacePoint qlcg_frame_local(const SurfaceFrame *f,double wx,double wz);
SurfacePoint qlcg_frame_coordinates(const SurfaceFrame *f,double x,double z);

#endif
