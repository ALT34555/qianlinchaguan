#ifndef QLCG_TERRAIN_VISIBILITY_H
#define QLCG_TERRAIN_VISIBILITY_H
#include "terrain_view.h"
typedef struct { float plane[6][4]; } TerrainFrustum;
void qlcg_frustum_set(TerrainFrustum *f,const float m[16]);
bool qlcg_frustum_bounds(const TerrainFrustum *f,const TerrainBounds *b);
bool qlcg_frustum_sphere(const TerrainFrustum *f,float x,float y,float z,float r);
int qlcg_far_visible(TerrainRenderer *tr,int kind,const float m[16],const TerrainFrustum *frustum,int view);
int qlcg_batch_visible(PlantBatch *b,const float m[16],const TerrainFrustum *frustum,int view);
void qlcg_instance_view_free(TerrainInstanceView *view);
void qlcg_batch_vao(const PlantBatch *b,GLuint vao,GLuint instance);
void qlcg_far_vao(GLuint vao,GLuint mesh,GLuint indices,GLuint instance);
void qlcg_batch_cpu_free(PlantBatch *b);
void qlcg_chunk_bounds(TerrainChunkGPU *g,const RegionData *c,double ox,double oz);
#endif
