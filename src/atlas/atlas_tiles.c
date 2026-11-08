/* 舆图细节瓦片：分级 LOD、槽位图集与逐帧预算烘焙 */
#include "atlas_tiles.h"
#include "../core/math_util.h"
#include "../systems/world/gen/gen_palette.h"
#include "../terrain3d/ground_detail.h"
#include "../systems/world/gen/gen_cover.h"
#include "../systems/world/surface_internal.h"
#include "../systems/world/source_rivers.h"
#include "../systems/world/surface_coast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif

#define TILE_RING_W       (ATLAS_TILE_PX + 2)
#define TILE_UPLOAD_ROWS  16
/* 约 180ms 淡入 */
#define TILE_FADE_PER_MS  0.0055f

typedef struct {
    int x,z;
    bool ready;
    SurfaceMapPoint point;
} AtlasNode;

typedef struct {
    int      ix, iy;          /* 世界瓦片索引（ix 已归一到 0..n-1） */
    int      level;           /* 0 表示空槽 */
    bool     complete;
    int      rows_uploaded;
    float    alpha;
} AtlasTileSlot;

struct AtlasTileCache {
    GLuint tex;               /* 带边缘的颜色图集 */
    GLuint meta;              /* 512×1 RGBA32F 槽位元数据 */
    GLuint water;
    int    level;
    bool   meta_dirty;

    int    vis_count;
    int    ready_count;
    int    vis_ix[ATLAS_TILE_SLOTS];
    int    vis_iy[ATLAS_TILE_SLOTS];
    int    vis_slot[ATLAS_TILE_SLOTS];
    float  vis_d2[ATLAS_TILE_SLOTS];

    int    bake_slot;
    int    bake_sample;       /* 下一待采样行 -1..ATLAS_TILE_PX */
    int    bake_column,bake_corner;
    int    bake_emit;         /* 下一待着色行 0..ATLAS_TILE_PX */
    double bake_x0, bake_z0, bake_span, node_step;

    uint64_t tick;
    double   last_ms;

    AtlasTileSlot slots[ATLAS_TILE_SLOTS];
    float         meta_buf[ATLAS_TILE_SLOTS * 2 * 4];
    uint8_t       pix[ATLAS_TILE_PX * ATLAS_TILE_PX * 4];
    float         water_pix[ATLAS_TILE_PX * ATLAS_TILE_PX * 4];
    SurfaceMapPoint ring[3][TILE_RING_W];
    AtlasNode nodes[4096];
    int patch_x,patch_z;
    bool patch_ready;
    float patch_height[4][4];
    uint32_t revision;
    float river_mix;
};

/* 世界 z 转 0..1 纬度参数，同着色器 */
static float tile_v_from_wz(const AtlasRenderer *ar, const MacroWorld *w, float wz) {
    float half_z = (qlcg_world_depth(w)*.5);
    return (wz + half_z) / qlcg_world_depth(w);
}

/* 瓦片目标像素：max(128, 视口/9.5) */
static float tile_target_px(float vp_w, float vp_h) {
    float t = ATLAS_TILE_SCREEN_PX;
    if (vp_w / 9.5f > t) t = vp_w / 9.5f;
    if (vp_h / 9.5f > t) t = vp_h / 9.5f;
    return t;
}

/* 当前缩放对应的瓦片层级，带迟滞避免逐格重烘 */
static int tile_pick_level(const AtlasRenderer *ar, const MacroWorld *w, int cur,
                           float vp_w, float vp_h) {
    float world_px = calc_zoom_scale(ar, w, vp_w) * w->circumference;
    float target_px = tile_target_px(vp_w, vp_h);
    int target = (int)ceilf(log2f(qlcg_maxf(world_px / target_px, 1.0f)));
    /* 层级不足说明宏观纹理已够细，不启用细节瓦片 */
    if (target < ATLAS_TILE_MIN_LEVEL) return 0;
    target = qlcg_clampi(target, ATLAS_TILE_MIN_LEVEL, ATLAS_TILE_MAX_LEVEL);
    if (cur <= 0) return target;
    float r = (world_px / (float)(1 << cur)) / target_px;
    if (r > ATLAS_TILE_HYST_HI) return qlcg_clampi(cur + 1, ATLAS_TILE_MIN_LEVEL, ATLAS_TILE_MAX_LEVEL);
    if (r < ATLAS_TILE_HYST_LO) return (cur - 1 < ATLAS_TILE_MIN_LEVEL) ? 0 : (cur - 1);
    return cur;
}

static void tile_publish_meta(struct AtlasTileCache *tc) {
    for (int i = 0; i < ATLAS_TILE_SLOTS; i++) {
        const AtlasTileSlot *s = &tc->slots[i];
        float *m0 = &tc->meta_buf[(size_t)i * 8];
        float *m1 = m0 + 4;
        if (s->level <= 0) {
            m0[0] = -1.0f; m0[1] = -1.0f; m0[2] = -1.0f; m0[3] = 0.0f;
            m1[0] = 0.0f;  m1[1] = 0.0f;
        } else {
            m0[0] = (float)s->ix;
            m0[1] = (float)s->iy;
            m0[2] = (float)s->level;
            m0[3] = 0.0f;
            m1[0] = 0.0f;
            m1[1] = s->alpha;
        }
        m1[2] = 0.0f;
        m1[3] = 0.0f;
    }
    glBindTexture(GL_TEXTURE_2D, tc->meta);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ATLAS_TILE_SLOTS * 2, 1,
                    GL_RGBA, GL_FLOAT, tc->meta_buf);
    tc->meta_dirty = false;
}

static struct AtlasTileCache *tiles_create(void) {
    struct AtlasTileCache *tc = (struct AtlasTileCache *)calloc(1, sizeof(*tc));
    if (!tc) return NULL;
    tc->level = 0;
    tc->bake_slot = -1;
    tc->meta_dirty = true;
    tc->last_ms = qlcg_time_ms();

    glGenTextures(1, &tc->tex);
    glBindTexture(GL_TEXTURE_2D, tc->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ATLAS_TILE_ATLAS_PX, ATLAS_TILE_ATLAS_PX,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glGenTextures(1,&tc->water);
    glBindTexture(GL_TEXTURE_2D,tc->water);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,qlcg_gl_caps.float_linear?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,qlcg_gl_caps.float_linear?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,ATLAS_TILE_ATLAS_PX,ATLAS_TILE_ATLAS_PX,0,GL_RGBA,GL_FLOAT,NULL);

    glGenTextures(1, &tc->meta);
    glBindTexture(GL_TEXTURE_2D, tc->meta);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, ATLAS_TILE_SLOTS * 2, 1, 0,
                 GL_RGBA, GL_FLOAT, NULL);
    tile_publish_meta(tc);
    return tc;
}

void qlcg_atlas_tiles_destroy(struct AtlasTileCache *tc) {
    if (!tc) return;
    if (tc->tex)  qlcg_gl_delete_textures(1, &tc->tex);
    if (tc->meta) qlcg_gl_delete_textures(1, &tc->meta);
    if (tc->water) qlcg_gl_delete_textures(1,&tc->water);
    free(tc);
}

void qlcg_atlas_tiles_invalidate(struct AtlasTileCache *tc) {
    if (!tc) return;
    for (int i = 0; i < ATLAS_TILE_SLOTS; i++) {
        tc->slots[i].level = 0;
        tc->slots[i].alpha = 0.0f;
        tc->slots[i].complete = false;
        tc->slots[i].rows_uploaded = 0;
    }
    tc->bake_slot = -1;
    tc->ready_count = 0;
    memset(tc->nodes,0,sizeof(tc->nodes));
    tc->patch_ready=false;
    tc->meta_dirty = true;
}

/* 采样一行地表数据到滚动环 */
static SurfaceMapPoint tile_node(struct AtlasTileCache *tc,const MacroWorld *w,int x,int z) {
    unsigned key=qlcg_hash_u32((uint32_t)x*374761393u^(uint32_t)z*668265263u);
    AtlasNode *node=&tc->nodes[key%4096];
    if(!node->ready||node->x!=x||node->z!=z) {
        node->x=x; node->z=z; node->ready=true;
        qlcg_sample_surface_detail_map(w,x*tc->node_step,z*tc->node_step,&node->point);
    }
    return node->point;
}

/* 固定四点权重，复用节点缓存 */
static void tile_smooth_weights(float t,float weights[4]) {
    float t2=t*t,t3=t2*t,s=1-t;
    weights[0]=s*s*s/6;
    weights[1]=(3*t3-6*t2+4)/6;
    weights[2]=(-3*t3+3*t2+3*t+1)/6;
    weights[3]=t3/6;
}

static SurfaceMapPoint tile_surface(struct AtlasTileCache *tc,const MacroWorld *w,
                                    double wx,double wz,double step) {
    SurfaceMapPoint result={.depth=-1,.source_depth=-1};
    if(step>32) {
        qlcg_sample_surface_map(w,wx,wz,&result.h,&result.wl,&result.temp);
        if(result.wl>result.h+.02f) result.depth=result.wl-result.h;
        return result;
    }
    int x=(int)floor(wx/tc->node_step),z=(int)floor(wz/tc->node_step);
    float u=(float)(wx/tc->node_step-x),v=(float)(wz/tc->node_step-z);
    SurfaceMapPoint b=tile_node(tc,w,x+1,z),c=tile_node(tc,w,x,z+1);
    SurfaceMapPoint a,d={0};
    float wa,wb,wc,wd=0;
    if(tc->node_step>2) {
        a=tile_node(tc,w,x,z); d=tile_node(tc,w,x+1,z+1);
        wa=(1-u)*(1-v); wb=u*(1-v); wc=(1-u)*v; wd=u*v;
    } else {
        a=u+v<=1?tile_node(tc,w,x,z):tile_node(tc,w,x+1,z+1);
        wb=u+v<=1?u:1-v; wc=u+v<=1?v:1-u; wa=1-wb-wc;
    }
    result.h=wa*a.h+wb*b.h+wc*c.h+wd*d.h;
    result.temp=wa*a.temp+wb*b.temp+wc*c.temp+wd*d.temp;
    result.depth=wa*a.depth+wb*b.depth+wc*c.depth+wd*d.depth;
    result.source_depth=wa*a.source_depth+wb*b.source_depth+wc*c.source_depth+wd*d.source_depth;
    result.coast_tint=wa*a.coast_tint+wb*b.coast_tint+wc*c.coast_tint+wd*d.coast_tint;
    if(tc->node_step>2) {
        float sx[4],sz[4];
        tile_smooth_weights(u,sx); tile_smooth_weights(v,sz);
        if(!tc->patch_ready || tc->patch_x!=x || tc->patch_z!=z) {
            for(int j=0;j<4;j++) for(int i=0;i<4;i++)
                tc->patch_height[j][i]=tile_node(tc,w,x+i-1,z+j-1).h;
            tc->patch_x=x; tc->patch_z=z; tc->patch_ready=true;
        }
        result.h=0;
        for(int j=0;j<4;j++) for(int i=0;i<4;i++)
            result.h+=sx[i]*sz[j]*tc->patch_height[j][i];
    }
    result.wl=result.h+fmaxf(result.depth,result.source_depth);
    return result;
}

static void tile_sample_batch(struct AtlasTileCache *tc, const MacroWorld *w, int row) {
    int rr = ((row % 3) + 3) % 3;
    double step = tc->bake_span / ATLAS_TILE_SAMPLE_PX;
    double wz = tc->bake_z0 + ((float)row - 0.5f) * step;
    int end=qlcg_clampi(tc->bake_column+8,0,TILE_RING_W);
    for (int j = tc->bake_column; j < end; j++) {
        double wx = tc->bake_x0 + ((float)j - 1.5f) * step;
        tc->ring[rr][j]=tile_surface(tc,w,wx,wz,step);
    }
    tc->bake_column=end==TILE_RING_W?0:end;
    if(tc->bake_column==0) tc->bake_sample++;
}

/* 由环上三行着色一行并写入瓦片缓冲 */
static void tile_emit_row(struct AtlasTileCache *tc, const MacroWorld *w, int row) {
    int rp = (((row - 1) % 3) + 3) % 3;
    int rc = ((row % 3) + 3) % 3;
    int rn = (((row + 1) % 3) + 3) % 3;
    double step = tc->bake_span / ATLAS_TILE_SAMPLE_PX;
    uint8_t *out = &tc->pix[(size_t)row * ATLAS_TILE_PX * 4];
    double wz = tc->bake_z0 + ((float)row - 0.5f) * step;

    for (int c = 0; c < ATLAS_TILE_PX; c++) {
        const SurfaceMapPoint *mid = &tc->ring[rc][c + 1];
        float h  = mid->h;
        float wl = mid->wl;
        float tp = mid->temp;

        float dhx = (tc->ring[rc][c + 2].h - tc->ring[rc][c].h) / (2.0f * step);
        float dhz = (tc->ring[rn][c + 1].h - tc->ring[rp][c + 1].h) / (2.0f * step);

        uint8_t col[3];
        double wx = tc->bake_x0 + ((float)c - 0.5f) * step;
        float hc = h;
        bool water = mid->depth > .02f;
        bool near_water = water && step<=8;
        int wi=(row*ATLAS_TILE_PX+c)*4;
        tc->water_pix[wi]=mid->depth;
        tc->water_pix[wi+1]=qlcg_clampf((tp+45)/90,0,1);
        /* 米制高程直接存储 */
        tc->water_pix[wi+2]=h;
        tc->water_pix[wi+3]=step<=8?mid->source_depth:-1.0f;
        float k = 1.0f;
        if (water && !near_water) {
            /* 湖海水量程按水位连续过渡，避免岸边在水位线上跳色 */
            float wl_pos = (wl < -1.0e8f) ? 0.0f : wl;
            float inland = qlcg_clampf(wl_pos * 0.5f, 0.0f, 1.0f);
            float full = 1200.0f + (60.0f - 1200.0f) * inland;
            float lvl = qlcg_maxf(wl_pos, hc);
            qlcg_water_color(qlcg_depth_value(lvl, hc, full), col);
            k = 0.97f;   /* 水面不显地形明暗 */
        } else {
            qlcg_land_color(qlcg_elevation_ramp(hc), qlcg_snow_value(tp), col);
            /* 西北向光照：细节来自逐纹素坡度，非宏观格坡度 */
            float inv = 1.0f / sqrtf(dhx * dhx + dhz * dhz + 1.0f);
            float lam = (-dhx * 0.62f - dhz * 0.62f) * inv;
            float sh = qlcg_clampf(0.86f + lam * 0.95f, 0.42f, 1.28f);
            k = 0.60f + 0.54f * sh;
        }

        /* 地面材质：与3D同套，按世界坐标采样，过大淡出 */
        float dfade = qlcg_clampf(1.0f - (step - 1.0f) / 3.0f, 0.0f, 1.0f);
        if ((!water || near_water) && w && dfade > 0.01f) {
            int band = qlcg_clampi((int)qlcg_cover_band_of_temp(tp), 0, QLCG_GROUND_BAND_N - 1);
            float gc[3];
            qlcg_ground_tex_at(wx, wz, w->opt.seed, band, gc);
            for (int ch = 0; ch < 3; ch++)
                col[ch] += (gc[ch] - col[ch]) * 0.55f * dfade;
        }
        out[c * 4 + 0] = (uint8_t)qlcg_clampi((int)((float)col[0] * k), 0, 255);
        out[c * 4 + 1] = (uint8_t)qlcg_clampi((int)((float)col[1] * k), 0, 255);
        out[c * 4 + 2] = (uint8_t)qlcg_clampi((int)((float)col[2] * k), 0, 255);
        if(step>8 && water && h<0 && wl==0 && h>-85) {
            SurfaceMacroContext context=qlcg_macro_context(w,wx,wz);
            double base=qlcg_read_macro(w,&context,w->height);
            CoastSample coast=qlcg_coast_surface(w,&context,wx,wz,base,h);
            float shallow=coast.weight*(1-qlcg_smoothf(0,12+20*coast.sand,-h));
            const float tint[3]={125+35*coast.mud,176-28*coast.mud,171-54*coast.mud};
            for(int k=0;k<3;k++) out[c*4+k]=(uint8_t)qlcg_lerpf(out[c*4+k],tint[k],shallow);
        }
        out[c * 4 + 3] = (uint8_t)(127.5f*(1+mid->coast_tint));
    }
}

/* 把已着色行上传到槽位矩形 */
static void tile_upload_rows(struct AtlasTileCache *tc, int slot, int up_to) {
    AtlasTileSlot *s = &tc->slots[slot];
    if (up_to <= s->rows_uploaded) return;
    int sx = (slot % ATLAS_TILE_GRID) * ATLAS_TILE_PX;
    int sy = (slot / ATLAS_TILE_GRID) * ATLAS_TILE_PX;
    const uint8_t *src = &tc->pix[(size_t)s->rows_uploaded * ATLAS_TILE_PX * 4];
    glBindTexture(GL_TEXTURE_2D, tc->tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, sx, sy + s->rows_uploaded, ATLAS_TILE_PX,
                    up_to - s->rows_uploaded, GL_RGBA, GL_UNSIGNED_BYTE, src);
    glBindTexture(GL_TEXTURE_2D,tc->water);
    glTexSubImage2D(GL_TEXTURE_2D,0,sx,sy+s->rows_uploaded,ATLAS_TILE_PX,
        up_to-s->rows_uploaded,GL_RGBA,GL_FLOAT,
        &tc->water_pix[s->rows_uploaded*ATLAS_TILE_PX*4]);
    s->rows_uploaded = up_to;
}

/* 槽位取模同着色器，平移不换槽 */
static int tile_slot_of(int ix, int iy) {
    return (ix & (ATLAS_TILE_GRID - 1)) | ((iy & (ATLAS_TILE_GRID - 1)) << 4);
}

/* 取槽：槽位固定由瓦片索引决定，换级或换位即按需重烘 */
static int tile_acquire_slot(struct AtlasTileCache *tc, const MacroWorld *w,
                             int level, int ix, int iy,
                             double x0, double z0, double span) {
    int pick = tile_slot_of(ix, iy);
    AtlasTileSlot *s = &tc->slots[pick];
    s->ix = ix;
    s->iy = iy;
    s->level = level;
    s->complete = false;
    s->rows_uploaded = 0;
    s->alpha = 0.0f;
    tc->bake_slot = pick;
    tc->bake_x0 = x0;
    tc->bake_z0 = z0;
    tc->bake_span = span;
    double step=span/ATLAS_TILE_SAMPLE_PX;
    tc->node_step=fmax(2,exp2(ceil(log2(step*(step>8?8:2)))));
    tc->bake_sample = -1;
    tc->bake_column = 0;
    tc->bake_corner = 0;
    tc->bake_emit = 0;
    tc->meta_dirty = true;
    return pick;
}

bool qlcg_atlas_tiles_update(AtlasRenderer *ar,
                             const MacroWorld *w,
                             float vp_x, float vp_y, float vp_w, float vp_h,
                             double budget_ms) {
    if (!ar || !w) return false;
    struct AtlasTileCache *tc = ar->tiles;
    if(tc && tc->revision!=w->surface_revision) {
        qlcg_atlas_tiles_invalidate(tc);
        tc->revision=w->surface_revision;
    }
    bool enabled = (ar->planet_view != PLANET_VIEW_GLOBE) && !ar->sat_mode && !ar->tiles_off;
    int level = enabled ? tile_pick_level(ar, w, tc ? tc->level : 0, vp_w, vp_h) : 0;
    if (!tc) {
        if (!level) return false;
        ar->tiles = tc = tiles_create();
        if (!tc) return false;
    }
    double t0 = qlcg_time_ms();
    float dt = (float)(t0 - tc->last_ms);
    tc->last_ms = t0;
    dt = qlcg_clampf(dt, 0.0f, 120.0f);
    tc->tick++;

    bool changed = false;
    if (level != tc->level) {
        tc->level = level;
        qlcg_atlas_tiles_invalidate(tc);
        changed = true;
    }
    if (tc->level <= 0) {
        if (tc->meta_dirty) { tile_publish_meta(tc); changed = true; }
        return changed;
    }

    /* 收集可见瓦片：屏幕 5×5 采样点求经度与纬度跨度 */
    int n = 1 << tc->level;
    double span = w->circumference / (double)n;
    double uc = ar->cam_wx / w->circumference + 0.5f;
    if(!qlcg_world_flat(w)) uc -= floor(uc);
    double u0 = 1e30, u1 = -1e30, v0 = 1e30, v1 = -1e30;
    bool any = false;
    for (int gy = 0; gy <= 4; gy++) {
        for (int gx = 0; gx <= 4; gx++) {
            float sx = vp_x + vp_w * ((float)gx / 4.0f);
            float sy = vp_y + vp_h * ((float)gy / 4.0f);
            double wx = 0, wz = 0;
            if (!qlcg_atlas_screen_to_world_d(ar, w, sx, sy, vp_x, vp_y, vp_w, vp_h, &wx, &wz)) continue;
            double u = wx / w->circumference + 0.5f;
            if(!qlcg_world_flat(w)) u -= floor(u);
            double du = u - uc;
            if(!qlcg_world_flat(w)) du -= floor(du + .5);
            double uu = uc + du;
            double v = (wz+qlcg_world_depth(w)*.5)/qlcg_world_depth(w);
            if (uu < u0) u0 = uu;
            if (uu > u1) u1 = uu;
            if (v < v0) v0 = v;
            if (v > v1) v1 = v;
            any = true;
        }
    }
    if (!any) {
        if (tc->meta_dirty) { tile_publish_meta(tc); changed = true; }
        return changed;
    }

    int ix0 = (int)floor(u0 * n) - 1;
    int ix1 = (int)floor(u1 * n) + 1;
    int iy0 = qlcg_clampi((int)floor(v0 * n * (qlcg_world_flat(w)?1:.5)) - 1, 0, (qlcg_world_flat(w)?n:n/2) - 1);
    int iy1 = qlcg_clampi((int)floor(v1 * n * (qlcg_world_flat(w)?1:.5)) + 1, 0, (qlcg_world_flat(w)?n:n/2) - 1);

    tc->vis_count = 0;
    tc->ready_count = 0;

    /* 按到视图中心距离插入排序，优先烘焙近处瓦片 */
    for (int iy = iy0; iy <= iy1 && tc->vis_count < ATLAS_TILE_SLOTS; iy++) {
        for (int ixr = ix0; ixr <= ix1 && tc->vis_count < ATLAS_TILE_SLOTS; ixr++) {
            if(qlcg_world_flat(w) && (ixr<0 || ixr>=n)) continue;
            int ix = ((ixr % n) + n) % n;
            float cx = ((float)ixr + 0.5f) * span - w->circumference * 0.5f;
            float cz = ((float)iy + 0.5f) * span - (qlcg_world_depth(w)*.5);
            float dx = cx - ar->cam_wx;
            if(!qlcg_world_flat(w)) dx -= w->circumference * floorf(dx / w->circumference + 0.5f);
            float dz = cz - ar->cam_wz;
            float d2 = dx * dx + dz * dz;
            int pos = tc->vis_count;
            while (pos > 0 && tc->vis_d2[pos - 1] > d2) {
                tc->vis_ix[pos] = tc->vis_ix[pos - 1];
                tc->vis_iy[pos] = tc->vis_iy[pos - 1];
                tc->vis_d2[pos] = tc->vis_d2[pos - 1];
                pos--;
            }
            tc->vis_ix[pos] = ix;
            tc->vis_iy[pos] = iy;
            tc->vis_d2[pos] = d2;
            tc->vis_count++;
        }
    }

    for (int v = 0; v < tc->vis_count; v++) {
        int sl = tile_slot_of(tc->vis_ix[v], tc->vis_iy[v]);
        AtlasTileSlot *s = &tc->slots[sl];
        if (s->level == tc->level && s->ix == tc->vis_ix[v] && s->iy == tc->vis_iy[v]) {
                    if (s->complete && s->alpha > 0.92f) tc->ready_count++;
            tc->vis_slot[v] = sl;
        } else {
            tc->vis_slot[v] = -1;
        }
    }

    /* 推进当前烘焙 */
    if (tc->bake_slot >= 0) {
        AtlasTileSlot *bs = &tc->slots[tc->bake_slot];
        if (bs->level != tc->level) {
            tc->bake_slot = -1;
        } else {
            double step=tc->bake_span/ATLAS_TILE_SAMPLE_PX;
            double margin=step*3+tc->node_step*2;
            while(step<=32 && tc->bake_corner<4) {
                int k=tc->bake_corner++;
                double wx=tc->bake_x0+(k%2?tc->bake_span+margin:-margin);
                double wz=tc->bake_z0+(k/2?tc->bake_span+margin:-margin);
                int cell=qlcg_macro_cell_at(w,wx,wz);
                if(qlcg_source_candidate(w,cell)&&!qlcg_source_network(w,cell,0)) {
                    if(qlcg_source_network(w,cell,1)) {
                        memset(tc->nodes,0,sizeof(tc->nodes));
                        tc->patch_ready=false;
                    }
                    break;
                }
            }
            bool sources_ready=step>32 || tc->bake_corner==4;
            while (sources_ready && qlcg_time_ms() - t0 < budget_ms) {
                if (tc->bake_sample <= tc->bake_emit + 1 && tc->bake_sample <= ATLAS_TILE_PX) {
                    tile_sample_batch(tc,w,tc->bake_sample);
                    continue;
                }
                if (tc->bake_emit >= ATLAS_TILE_PX) break;
                tile_emit_row(tc, w, tc->bake_emit);
                tc->bake_emit++;
                if (tc->bake_emit == ATLAS_TILE_PX ||
                    (tc->bake_emit % TILE_UPLOAD_ROWS) == 0) {
                    tile_upload_rows(tc, tc->bake_slot, tc->bake_emit);
                    changed = true;
                }
                if (tc->bake_emit >= ATLAS_TILE_PX) {
                    bs->complete = true;
                    tc->bake_slot = -1;
                    break;
                }
            }
        }
    }

    /* 起一块新瓦片：跳过已就绪的可见瓦片 */
    tc->river_mix=tc->vis_count && span/ATLAS_TILE_SAMPLE_PX<=8?1.0f:0.0f;
    for(int i=0;tc->river_mix>0 && i<tc->vis_count;i++) {
        int slot=tc->vis_slot[i];
        tc->river_mix=slot<0?0:fminf(tc->river_mix,tc->slots[slot].alpha);
    }
    if (tc->bake_slot < 0 && qlcg_time_ms() - t0 < budget_ms) {
        for (int v = 0; v < tc->vis_count; v++) {
            int sl = tile_slot_of(tc->vis_ix[v], tc->vis_iy[v]);
            AtlasTileSlot *s = &tc->slots[sl];
            if (s->level == tc->level && s->ix == tc->vis_ix[v] &&
                s->iy == tc->vis_iy[v]) {
                tc->vis_slot[v] = sl;
                continue;
            }
            double x0 = tc->vis_ix[v] * span - w->circumference * 0.5;
            double z0 = tc->vis_iy[v] * span - (qlcg_world_depth(w)*.5);
            if (tile_acquire_slot(tc, w, tc->level, tc->vis_ix[v], tc->vis_iy[v],
                                  x0, z0, span) >= 0) {
                tc->vis_slot[v] = tc->bake_slot;
                break;
            }
        }
        if (tc->bake_slot >= 0) changed = true;
    }

    /* 淡入 */
    for (int i = 0; i < ATLAS_TILE_SLOTS; i++) {
        AtlasTileSlot *s = &tc->slots[i];
        if (s->level <= 0 || !s->complete || s->alpha >= 1.0f) continue;
        s->alpha = qlcg_minf(1.0f, s->alpha + dt * TILE_FADE_PER_MS);
        tc->meta_dirty = true;
        changed = true;
    }

    if (tc->meta_dirty) {
        tile_publish_meta(tc);
        changed = true;
    }
    return changed;
}

void qlcg_atlas_tiles_view(const struct AtlasTileCache *tc, AtlasTileView *out) {
    if (!out) return;
    out->tex = tc ? tc->tex : 0;
    out->meta = tc ? tc->meta : 0;
    out->level = tc ? tc->level : 0;
    out->water = tc ? tc->water : 0;
    out->river_mix = tc && tc->level>0 ? tc->river_mix : 0;
}

void qlcg_atlas_tiles_stats(const struct AtlasTileCache *tc,
                            int *out_ready, int *out_total, int *out_level) {
    if (out_ready) *out_ready = tc ? tc->ready_count : 0;
    if (out_total) *out_total = tc ? tc->vis_count : 0;
    if (out_level) *out_level = tc ? tc->level : 0;
}
