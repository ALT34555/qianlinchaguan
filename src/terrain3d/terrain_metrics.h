#ifndef QLCG_TERRAIN_METRICS_H
#define QLCG_TERRAIN_METRICS_H
#include <stdint.h>
typedef enum { QL_GPU_SKY,QL_GPU_TERRAIN,QL_GPU_PROPS,QL_GPU_WATER,QL_GPU_SHADOW,QL_GPU_REFLECTION,QL_GPU_PAINT,QL_GPU_UI,QL_GPU_COUNT } QlGpuStage;
typedef struct {
    uint64_t draws,triangles,upload_bytes,culled_chunks,culled_instances;
    double simulation_ms,submit_ms,present_ms,wait_ms,buffer_ms,queue_ms;
    int width,height,samples;
} TerrainMetrics;
extern TerrainMetrics qlcg_metrics,qlcg_metrics_last;
#ifdef QLCG_DEV_TOOLS
void qlcg_profile_frame(void);
void qlcg_profile_begin(QlGpuStage stage);
void qlcg_profile_end(QlGpuStage stage);
double qlcg_profile_gpu(QlGpuStage stage);
void qlcg_profile_reset(void);
#else
#define qlcg_profile_frame() ((void)0)
#define qlcg_profile_begin(stage) ((void)0)
#define qlcg_profile_end(stage) ((void)0)
#define qlcg_profile_gpu(stage) (-1.0)
#define qlcg_profile_reset() ((void)0)
#endif
#endif
