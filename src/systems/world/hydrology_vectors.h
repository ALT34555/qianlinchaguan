#ifndef QLCG_HYDROLOGY_VECTORS_H
#define QLCG_HYDROLOGY_VECTORS_H

typedef struct { double wx,wz,x,z,arc,width,curvature,level,depth,fall,slope; } RiverVectorPoint;
typedef struct { int x,z,segment,next; } RiverBinEntry;
typedef struct {
    int bins[512],count,capacity;
    double bin_size;
    RiverBinEntry *entries;
} RiverVectorIndex;

int qlcg_river_index_build(RiverVectorIndex *index,const RiverVectorPoint *points,int count,double pad);
int qlcg_river_index_nearest(const RiverVectorIndex *index,const RiverVectorPoint *points,
                              double x,double z,double *distance,double *t);
int qlcg_river_index_height(const RiverVectorIndex *index,const RiverVectorPoint *points,const float *heights,
                           double x,double z,double smoothing,double reach,double *height,double *distance);
int qlcg_river_index_level(const RiverVectorIndex *index,const RiverVectorPoint *points,
                          double x,double z,double smoothing,double reach,double *level,double *distance);
void qlcg_river_index_free(RiverVectorIndex *index);

#endif
