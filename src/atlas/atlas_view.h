#ifndef QLCG_ATLAS_VIEW_H
#define QLCG_ATLAS_VIEW_H

#include "../core/config.h"
#include "../systems/world/planet_macro.h"
#include "../ui/ui_core.h"
#include "../systems/world/astronomy_eclipses.h"

struct AtlasTileCache;

typedef struct {
    double wx, wz;
    float yaw;
} AtlasPlayerMarker;

typedef struct {
    GLuint             prog;
    GLuint             vao, vbo;
    GLuint             data_tex0;
    GLuint             data_tex1;
    GLuint             sat_tex;
    GLuint             rain_tex;      /* 雨云图层降水纹理 */
    int                rain_tex_w, rain_tex_h;
    uint32_t           rain_key_day;
    uint32_t           rain_key_ver;
    uint32_t           rain_key_stamp;
    double             rain_last_ms;
    int                tex_w, tex_h;
    bool               world_upload_pending;
    uint32_t           synced_delta_ver;
    uint32_t           synced_terr_rev;
    /* 分级细节瓦片缓存（见 atlas_tiles.h） */
    struct AtlasTileCache *tiles;
    bool               tiles_off;   /* 调试：关闭细节瓦片做 A/B */

    PlanetView         planet_view;
    bool               sat_mode;
    bool               astronomy_overlay, eclipse_ready;
    uint32_t           eclipse_seed;
    CalendarMode       eclipse_mode;
    float              eclipse_tilt;
    bool               eclipse_prograde;
    double             eclipse_checked;
    AstraEclipse       eclipse;
    GLuint             eclipse_tex;
    AtlasDisplayState  display;
    AtlasQuickFilter   quick_filter;
    bool               layer_panel_open;
    bool               layer_dragging;
    int                layer_drag_from, layer_drag_to;
    float              layer_scroll;

    /* 平面投影漫游与缩放至L24 */
    double             cam_wx, cam_wz;
    float              zoom;
    float              target_zoom;
    float              overview_zoom;      /* 全览档，按视口比例实时算出 */
    float              last_vp_x, last_vp_y, last_vp_w, last_vp_h;

    /* 3D 星球正射视图旋转角 */
    float              globe_lon;
    float              globe_lat;

    bool               dragging;
    bool               has_selection;
    int                sel_cx, sel_cz;
    ChunkInspectInfo   sel_info;
    int                hover_cx, hover_cz;
    ChunkInspectInfo   hover_info;

    /* 舆图工程与事件工具状态 */
    EngToolType        eng_tool;
    float              brush_radius;
    bool               river_Release_pending;
    float              river_x0, river_z0;

    double             last_render_ms;
    size_t             synced_plane_count;
    float              flat_rect[4];
    const MacroWorld   *detail_world;
} AtlasRenderer;

bool qlcg_atlas_gpu_restore(AtlasRenderer *ar);
bool        qlcg_atlas_init(AtlasRenderer *ar);
void        qlcg_atlas_shutdown(AtlasRenderer *ar);
void        qlcg_atlas_release_world(AtlasRenderer *ar);
void        qlcg_atlas_upload_world(AtlasRenderer *ar, const MacroWorld *w);
void        qlcg_atlas_reset_view(AtlasRenderer *ar, const MacroWorld *w, bool focus_spawn);
void        qlcg_atlas_zoom_step(AtlasRenderer *ar, const MacroWorld *w, float delta_zoom,
                                 float pivot_sx, float pivot_sy,
                                 float vp_x, float vp_y, float vp_w, float vp_h);
bool        qlcg_atlas_screen_to_world(const AtlasRenderer *ar, const MacroWorld *w,
                                       float sx, float sy,
                                       float vp_x, float vp_y, float vp_w, float vp_h,
                                       float *out_wx, float *out_wz);
bool        qlcg_atlas_screen_to_world_d(const AtlasRenderer *ar,const MacroWorld *w,
                                           float sx,float sy,float x,float y,float width,float height,
                                           double *wx,double *wz);
void        qlcg_atlas_world_to_screen(const AtlasRenderer *ar, const MacroWorld *w,
                                       double wx, double wz,
                                       float vp_x, float vp_y, float vp_w, float vp_h,
                                       float *out_sx, float *out_sy);
void        qlcg_atlas_render_viewport(AtlasRenderer *ar, const MacroWorld *w,
                                       const CalendarClock *clk, UiContext *ui,
                                       float vp_x, float vp_y, float vp_w, float vp_h,
                                       bool is_creator_preview, int player_cx, int player_cz);
void        qlcg_atlas_render_player_viewport(AtlasRenderer *ar, const MacroWorld *w,
                                              const CalendarClock *clk, UiContext *ui,
                                              float x, float y, float width, float height,
                                              const AtlasPlayerMarker *player);
const char *qlcg_atlas_layer_name(AtlasLayerId id);
const char *qlcg_atlas_filter_name(AtlasQuickFilter f);

/* 相机：视口宽度对应的世界像素比 */
float       calc_zoom_scale(const AtlasRenderer *ar, const MacroWorld *w, float vp_w);

/* 等高线档位与下限，着色器同值 */
#define QLCG_ATLAS_CONTOUR_MIN 0.02f
float       qlcg_atlas_contour_unit(int level, int columns);
float       qlcg_atlas_contour_step(int level, int columns, int density);

/* 总览缩放缺省值（16:9下世界宽占88%） */
#define QLCG_ATLAS_OVERVIEW_ZOOM 1.641f

/* 按当前视口反解的总览档；未渲染过时回退缺省值 */
float       qlcg_atlas_overview_zoom(const AtlasRenderer *ar);

/* 视图中心移到世界坐标；keep_zoom 保留缩放 */
void        qlcg_atlas_center_on(AtlasRenderer *ar, const MacroWorld *w,
                                 double wx, double wz, bool keep_zoom);

/* 球体正射半径，渲染与拾取共用同一值 */
float       qlcg_atlas_globe_radius(const AtlasRenderer *ar, float vp_w, float vp_h);

/* 河流 LOD 层级：绕行一圈像素 / 128 的二进制阶 */
void        qlcg_atlas_draw_rivers(const AtlasRenderer *ar, const MacroWorld *w, UiContext *ui,
                                  float vp_x, float vp_y, float vp_w, float vp_h);

int         qlcg_atlas_river_level(const AtlasRenderer *ar, const MacroWorld *w,
                                   float vp_w, float vp_h);
void        qlcg_atlas_draw_sources(const AtlasRenderer *ar,const MacroWorld *w,UiContext *ui,
                                     float opacity,float x,float y,float width,float height);

/* 世界坐标投影到屏幕，返回是否可见（球面含背面剔除） */
bool        qlcg_atlas_world_to_screen_vis(const AtlasRenderer *ar, const MacroWorld *w,
                                           double wx, double wz,
                                           float vp_x, float vp_y, float vp_w, float vp_h,
                                           float *out_sx, float *out_sy);

float       qlcg_atlas_local_zoom(const MacroWorld *w, float vp_w, float vp_h);
void        qlcg_atlas_set_projection(AtlasRenderer *ar, const MacroWorld *w, PlanetView view);
void        qlcg_atlas_drag(AtlasRenderer *ar, const MacroWorld *w, float x, float y, float dx, float dy);

#endif /* QLCG_ATLAS_VIEW_H */
