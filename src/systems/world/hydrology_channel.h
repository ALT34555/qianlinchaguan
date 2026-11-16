#ifndef QLCG_HYDROLOGY_CHANNEL_H
#define QLCG_HYDROLOGY_CHANNEL_H
#include "hydrology_types.h"
#include "hydrology_vectors.h"

typedef struct {
    const void *context;
    double (*width_at)(const void *context,int kind,int target,double arc,double width);
} HydrologyWidthQuery;

static inline double qlcg_hydrology_receiver_width(const HydrologyWidthQuery *query,int kind,int target,double arc,double width) {
    return query&&query->width_at?query->width_at(query->context,kind,target,arc,width):width;
}
int qlcg_hydrology_section(double distance,double radius,double depth,double natural,double level,
                            double *height,double *water);
int qlcg_hydrology_section_span(double distance,double radius,double depth,double natural,double level,
                                 double span,double *height,double *water);
int qlcg_river_edit_validate(const RiverVectorPoint *points,int count);
int qlcg_river_segments_cross(const RiverVectorPoint *a,const RiverVectorPoint *b,
                              const RiverVectorPoint *c,const RiverVectorPoint *d);
#endif
