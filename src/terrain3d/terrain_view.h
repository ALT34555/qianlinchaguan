#ifndef QLCG_TERRAIN_VIEW_H
#define QLCG_TERRAIN_VIEW_H

#include "ground_detail.h"
#include "astronomy_render.h"
#include "prop_sprites.h"

#include "../core/config.h"
#include "../systems/world/planet_macro.h"
#include "../ui/ui_core.h"
#include "../systems/world/surface_stream.h"
#include "../systems/world/model/plant_def.h"
#include "../systems/world/model/plant_forms.h"
#include "../systems/world/model/rock_def.h"

/* 准星射线检视结果 */
typedef enum {
    AIM_HIT_NONE = 0,
    AIM_HIT_GROUND,
    AIM_HIT_WATER,
    AIM_HIT_PLANT,
    AIM_HIT_ROCK,
    AIM_HIT_FLAG
} AimHitKind;

typedef struct {
    AimHitKind kind;
    float      lx, ly, lz;
    float      wx, wz;
    float      dist_m;
    char       title[64];
    char       detail1[96];
    char       detail2[96];
} AimProbeResult;

/* 单个物种的植物批次：一份网格 + 一组实例 */
#define QLCG_PLANT_FORM_SLOTS (VEG_FORM_RICH + PLANT_VARIANT_COUNT)
#define QLCG_MAX_PLANT_BATCH (255 * PD_SEASON_COUNT * QLCG_PLANT_FORM_SLOTS)

/* 批次键按真实形态区分，简模与普通模型不合并 */
static inline int qlcg_plant_batch_key(int species, int season, int form) {
    return (species * PD_SEASON_COUNT + season) * QLCG_PLANT_FORM_SLOTS + form;
}

typedef struct {
    int    species;
    int    form;
    int    season;
    int    category;   /* 六类，决定灌木/草类风摆 */
    GLuint vao, vbo, ebo, inst_vbo;
    int    idx_count;
    int    inst_count;
    int    inst_cap;
} PlantBatch;

/* 植物实例：位置/朝向/缩放/风速等级 */
typedef struct {
    float x, y, z;
    float rot;
    float scale;
    float level;
} PlantInstGPU;

/* 远景贴图实例：脚底位置/世界尺寸/图集 UV/朝向/首次出现时间 */
typedef struct {
    float x, y, z;
    float w, h;
    float u0, v0, u1, v1;
    float rot;
    float shade;
    float birth;
} FarPropInst;

/* 远景实例首次出现时间表（key → 毫秒），gen 用于清理 */
typedef struct {
    unsigned long long key;
    double t0;
    unsigned int gen;
} PropFadeEnt;

bool qlcg_plant_batch_setup(PlantBatch *b, int species, int form, int season);
#define QLCG_MAX_ROCK_BATCH ROCK_CATALOG_CAPACITY
bool qlcg_rock_batch_setup(PlantBatch *b, int index, int form, int season, int chunk);
struct TerrainRenderer;
void qlcg_rock_batches_clear(struct TerrainRenderer *tr);
void qlcg_plant_batch_instances(PlantBatch *b, const PlantInstGPU *inst, int count);
typedef struct TerrainRenderer TerrainRenderer;
void qlcg_plant_batches_clear(TerrainRenderer *tr);

typedef struct {
    GLuint water_vao, water_vbo, water_ebo;
    GLuint cliff_vao, cliff_vbo;
    int cliff_vertex_count;
    float fall_emitters[4][4];
    int    water_idx_count;
    GLuint ice_ebo;
    int    ice_idx_count;
    uint64_t serial;
    uint32_t revision;
    uint32_t ice_rev;        /* 已上传的冰版本 */
    int    terrain_slot;
    bool   cliff_detail;
    bool   terrain_ready;    /* 地形顶点已写入共享缓冲 */
} TerrainChunkGPU;

/* 浮冰与裂纹的固定实例池 */
#define QLCG_ICE_FLOW_TEMPLATES  4
#define QLCG_ICE_FLOW_NATURAL   256
#define QLCG_ICE_FLOW_FRAGMENT  128
#define QLCG_ICE_CRACK_MAX       32

typedef struct {
    float x, y, z;
    float rot, scale, thickness, alpha;
    float yaw_spin, phase;
    uint8_t kind;    /* 0 天然 1 破冰碎片 */
    uint8_t tpl;
    bool  used;
} IceFlowInst;

typedef struct {
    float x, y, z, rot, size, damage, decay;
} IceCrackInst;

typedef struct {
    GLuint ice_prog;      /* 浮冰与碎片 */
    GLuint ice_top_prog;  /* 冰顶与冰底 */
    GLuint ice_tpl_vao[QLCG_ICE_FLOW_TEMPLATES];
    GLuint ice_tpl_vbo[QLCG_ICE_FLOW_TEMPLATES];
    GLuint ice_tpl_ebo[QLCG_ICE_FLOW_TEMPLATES];
    int    ice_tpl_idx[QLCG_ICE_FLOW_TEMPLATES];
    int    ice_tpl_vert[QLCG_ICE_FLOW_TEMPLATES];
    GLuint ice_inst_vbo;
    IceFlowInst ice_pool[QLCG_ICE_FLOW_NATURAL + QLCG_ICE_FLOW_FRAGMENT];
    int    ice_flow_count[QLCG_ICE_FLOW_TEMPLATES];
    GLuint crack_prog;
    GLuint crack_vao, crack_vbo;
    IceCrackInst cracks[QLCG_ICE_CRACK_MAX];
    int    crack_count;
    float  crack_buf[QLCG_ICE_CRACK_MAX * 8 * 8];
    int    crack_vert_count;
    int    ice_seed_cursor;
    float  ice_seed_timer;
} IceVisual;

/* 一帧玩家输入快照：键盘与触屏虚拟键已汇总 */
typedef struct {
    bool forward, back, left, right;
    bool jump, sprint, crouch;
    bool inspect_plants;
    bool jump_pressed;   /* 本帧刚按下（边沿），双击浮空用 */
} PlayerInput;

/* 运动学中间量：供自检与 F12 面板读，不参与计算 */
typedef struct {
    float speed;     /* 目标水平速度（已含身高与植被修正） */
    float accel;     /* 加速度系数 */
    float brush;     /* 植被减速系数 */
    float step_h;    /* 自动上台阶高度 */
    float eye_off;   /* 眼高偏移 */
} PlayerMotionDebug;

struct TerrainRenderer {
    /* 玩家相机与物理状态 */
    float x, y, z;
    float vx, vy, vz;
    float yaw, pitch;
    bool  flying;
    bool  in_water;
    bool  on_ground;
    bool  blocked_forward;
    int   plant_contacts;
    BlockFeedback block_feedback;
    float crouch_offset;      /* 蹲下时眼高下移量 */
    float step_offset;        /* 自动上台阶的镜头平滑量 */
    PlayerMotionDebug motion; /* 上帧运动学中间量，只读 */
    double last_jump_tap_ms;  /* 上一次跳跃键按下的时刻，双击浮空用 */
    bool  mouse_captured;
    char  status_text[96];

    /* 地形网格：共享大 VBO，逐块 baseVertex */
    GLuint terrain_prog;
    GLuint terrain_vao, terrain_ebo;
    GLuint terrain_vbo;
    int    terrain_capacity;
    int    terrain_next_slot, terrain_free_count;
    uint16_t terrain_free_slots[QLCG_STREAM_CAP];
    int    terrain_idx_count;
    int    terrain_lod_off;      /* 半 LOD 索引段在 EBO 中的起点 */
    int    terrain_lod_count;
    float  terrain_origin_x, terrain_origin_z;   /* 上传时用的 region 中心 */
    GLsizei *md_count;      /* 每帧可见块 */
    GLvoid **md_index;
    GLint   *md_base;

    /* 水体与冰壳 VAO/VBO */
    GLuint water_prog;
    int    water_idx_count;

    /* 硬件实例化：植被、山石与领地旗帜 */
    GLuint prop_prog;
    GLuint tree_vao, tree_vbo, tree_ebo, tree_inst_vbo;
    int    tree_mesh_indices;
    int    tree_inst_count;

    /* 植物：按物种分批的真实网格 */
    GLuint     plant_prog;
    PlantBatch plants[QLCG_MAX_PLANT_BATCH];
    int        plant_batch_count;
    PlantBatch rocks[QLCG_MAX_ROCK_BATCH];
    int        rock_batch_count;
    int        rock_inst_total;
    int        plant_inst_total;
    int        plant_season;
    PropSprites sprites;      /* 远景四季贴图图集 */

    /* 远景贴图两批：交叉面片与 billboard */
    GLuint far_prog;
    GLuint far_cross_vao, far_cross_vbo, far_cross_ebo, far_cross_inst;
    int    far_cross_idx, far_cross_count, far_cross_cap;
    GLuint far_bill_vao, far_bill_vbo, far_bill_ebo, far_bill_inst;
    int    far_bill_idx, far_bill_count, far_bill_cap;
    bool   msaa_active;       /* 离屏缓冲是否带 MSAA，决定贴图边缘方式 */
    PropFadeEnt *far_fade;    /* 实例淡入时间表 */
    int    far_fade_cap;
    unsigned int far_fade_gen;
    int    far_fade_dead;
    double fade_origin_ms;    /* 淡入计时起点（会话相对） */

    GLuint rock_vao, rock_vbo, rock_ebo, rock_inst_vbo;
    int    rock_mesh_indices;
    int    rock_inst_count;

    GLuint flag_vao, flag_vbo, flag_ebo, flag_inst_vbo;
    GLuint flag_prog;
    int    flag_mesh_indices;
    int    flag_mesh_verts;
    int    flag_inst_count;

    /* 天空穹顶与全天候雨雪沙尘粒子 */
    AstraRenderer astronomy;
    GLuint sky_prog;
    GLuint sky_vao, sky_vbo;
    GLuint weather_prog;
    GLuint spring_prog;
    GLuint weather_vao, weather_vbo;

    /* 地面材质纹理：五个气候带各一张 256m 周期图 */
    GLuint ground_tex[QLCG_GROUND_BAND_N];
    double anchor_x, anchor_z;      /* 宏观格锚点，顶点坐标基准 */
    int    anchor_cell_x, anchor_cell_z;

    /* 当前天气与准星检视 */
    WeatherSystem         weather_sys;
    const WeatherProfile *cur_weather;
    AimProbeResult        aim;

    /* 3D 自由自然工程笔刷与样条河流改道 */
    EngToolType           eng_tool;
    float                 brush_radius;
    bool                  has_hover;
    float                 hover_lx, hover_ly, hover_lz;
    float                 river_pts_x[16];
    float                 river_pts_z[16];
    int                   river_pt_count;
    float                 river_width_m;
    float                 river_depth_m;
    float                 river_meander;

    /* 冰顶/冰底 pass、浮冰池与裂纹 */
    IceVisual ice_vis;
    float     ice_lift;

    float                 cam_shake;
    float                 wind_strength;
    float                 rain_override;
    float                 precip_intensity;  /* 降水强度 0..1 */
    double                weather_unix_ms;   /* 天气采样时刻 */
    uint32_t              props_weather_stamp; /* 分钟+强制天气，驱动风等级刷新 */
    double                upload_ms;
    TerrainChunkGPU       *chunks;
    RegionStream          *bound_stream;
    PropInstance         *probe_plants;
    int                   probe_plant_count;
    uint64_t              props_revision;
    double                props_ms;
    int                   props_distance;
    int                   props_form;
};

void qlcg_terrain_gpu_release(TerrainRenderer *tr);
bool qlcg_terrain_gpu_restore(TerrainRenderer *tr);
bool qlcg_terrain_init(TerrainRenderer *tr);
void qlcg_terrain_release_scene(TerrainRenderer *tr);
void qlcg_terrain_shutdown(TerrainRenderer *tr);
/* 冰视觉：浮冰实例池与裂纹反馈 */
bool qlcg_ice_visual_init(TerrainRenderer *tr);
void qlcg_ice_visual_shutdown(TerrainRenderer *tr);
void qlcg_ice_visual_update(TerrainRenderer *tr, RegionData *reg, const MacroWorld *w, float dt);
void qlcg_ice_visual_reset(TerrainRenderer *tr);
void qlcg_ice_visual_spawn_fragments(TerrainRenderer *tr, float wx, float wy, float wz, int count);
void qlcg_ice_visual_spawn_rim(TerrainRenderer *tr, float wx, float wy, float wz, float half, int count);
void qlcg_ice_visual_cracks_clear(TerrainRenderer *tr);
void qlcg_ice_visual_crack_add(TerrainRenderer *tr, float wx, float wy, float wz,
                               float size, float damage);
void qlcg_ice_visual_draw_flow(TerrainRenderer *tr, const float *mvp, const float *cam,
                               const float *fog_col, float fog_near, float fog_far,
                               float light_scale,
                               const float *sun, const float *tint, float time);
void qlcg_ice_visual_draw_cracks(TerrainRenderer *tr, const float *mvp, const float *cam,
                                 const float *fog_col, float fog_near, float fog_far,
                                 float light_scale);
int  qlcg_ice_visual_flow_count(const TerrainRenderer *tr);
void qlcg_terrain_upload_region(TerrainRenderer *tr, const RegionData *reg, const GameSettings *s);
void qlcg_terrain_clear_chunks(TerrainRenderer *tr);
void qlcg_terrain_stream_update(TerrainRenderer *tr, RegionData *reg, const GameSettings *s);
/* 植被季节：0 春 1 夏 2 秋 3 冬 */
void qlcg_terrain_set_season(TerrainRenderer *tr, int season_idx);
void qlcg_terrain_place_player_at_spawn(TerrainRenderer *tr, const RegionData *reg, const GameSettings *s);
void qlcg_terrain_update_player(TerrainRenderer *tr, RegionData *reg, const MacroWorld *w,
                                const GameSettings *s, const CalendarClock *clk,
                                const PlayerInput *in, float dt);
bool qlcg_terrain_raycast(const TerrainRenderer *tr, const RegionData *reg,
                          float ray_ox, float ray_oy, float ray_oz,
                          float ray_dx, float ray_dy, float ray_dz,
                          float *out_lx, float *out_ly, float *out_lz, float *out_dist);
void qlcg_terrain_update_aim_probe(TerrainRenderer *tr, const RegionData *reg,
                                   const MacroWorld *w, const GameSettings *s, bool inspect_plants);
void qlcg_terrain_render(TerrainRenderer *tr, const RegionData *reg, const MacroWorld *w,
                         const CalendarClock *clk, int utc_offset_min,
                         const GameSettings *s, int win_w, int win_h);

/* 地面材质纹理：启动时生成五个气候带的可平铺贴图 */
void qlcg_ground_tex_init(TerrainRenderer *tr);

#endif /* QLCG_TERRAIN_VIEW_H */
