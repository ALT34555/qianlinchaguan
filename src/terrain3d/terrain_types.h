/* 山川网格顶点格式 */
#ifndef QLCG_TERRAIN_TYPES_H
#define QLCG_TERRAIN_TYPES_H

typedef struct {
    float x, y, z;      /* 相对宏观格锚点 */
    float nx, ny, nz;
    float r, g, b;
} TerrainVert;

typedef struct {
    float x, y, z;
    float h_ground;
    float flow_x, flow_z;   /* 未按冻态缩放的流向 m/s */
    float fall;             /* 1 瀑布 */
    float ice_h;            /* 冰厚 m */
    float ice_q;            /* 冰质 0..1 */
    float ice_cover;        /* 覆盖 0..1 */
    float brine, spring;
    float coast_mud,coast_sand,coast_rock;
} WaterVert;

typedef struct {
    float px, py, pz;
    float nx, ny, nz;
} PropMeshVert;

typedef struct {
    float x, y, z;
    float sx, sy, sz;
    float rot;
    float r, g, b;
} PropInstGPU;

/* 旗帜实例：主色旗面 + 装饰色包边 + 风速等级 */
typedef struct {
    float x, y, z;
    float scale;
    float rot;
    float fr, fg, fb;
    float tr, tg, tb;
    float level;
} FlagInstGPU;


/* 植物网格顶点：带顶点色与风摆槽位 */
typedef struct {
    float px, py, pz;
    float nx, ny, nz;
    float cr, cg, cb;   /* 0..1 顶点色 */
    float part;         /* 1 叶 2 枝干 3 其他 4 树冠覆雪 */
    float h;            /* 归一化高度 0..1 */
} PlantVertex;

#endif
