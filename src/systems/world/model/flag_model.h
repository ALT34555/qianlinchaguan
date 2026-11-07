/* 领地旗帜网格：对齐 base DefaultFlag */
#ifndef QLCG_FLAG_MODEL_H
#define QLCG_FLAG_MODEL_H

#include <stdint.h>

/* mat:0木石1旗面2包边 wind:0硬1旗底 */
typedef struct {
    float px, py, pz;
    float nx, ny, nz;
    float cr, cg, cb;
    float mat;
    float wind;
} FlagMeshVert;

typedef struct {
    FlagMeshVert *verts;
    int           vcount;
    int           vcap;
    uint16_t     *indices;
    int           icount;
    int           icap;
    float         height;
} FlagMesh;

void qlcg_flag_mesh_build(FlagMesh *m);
void qlcg_flag_mesh_free(FlagMesh *m);

#endif
