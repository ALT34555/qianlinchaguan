/* 舆图细节瓦片：分级 LOD、槽位图集与逐帧预算烘焙 */
#include "atlas_tiles.h"
#include "../core/frame_workers.h"
#include "../core/math_util.h"
#include "../systems/world/gen/gen_palette.h"
#include "../terrain3d/ground_detail.h"
#include "../systems/world/gen/gen_cover.h"
#include "../systems/world/surface_internal.h"
#include "../systems/world/hydrology_sources.h"
#include "../systems/world/surface_coast.h"
#include "../systems/world/surface_worker.h"
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
#define TILE_FADE_PER_MS  (1.0f / 120.0f)

typedef struct {
    int x,z;
    bool ready;
    SurfaceMapPoint point;
} AtlasNode;

typedef struct {
    int      ix, iy;          /* 世界瓦片索引（ix 已归一到 0..n-1） */
    int      level;           /* 0 表示空槽 */
    bool     complete;
    float    alpha;
} AtlasTileSlot;

typedef struct {
    int slot, sample, column, emit, corner;
    bool serial;
    double x0,z0,span,node_step;
    uint8_t pix[ATLAS_TILE_PX * ATLAS_TILE_PX * 4];
    float water_pix[ATLAS_TILE_PX * ATLAS_TILE_PX * 4];
    SurfaceMapPoint ring[3][TILE_RING_W];
    AtlasNode nodes[4096];
    int patch_x,patch_z;
    bool patch_ready;
    float patch_height[4][4];
    QlcgSurfaceWorker *sampler;
} AtlasBake;

struct AtlasTileCache {
    GLuint tex;               /* 带边缘的颜色图集 */
    GLuint meta;              /* 512×1 RGBA32F 槽位元数据 */
    GLuint water;
    int    level;
    bool   meta_dirty;

    int    vis_count;
    int    ready_count;
    bool   vis_cached;
    PlanetView vis_projection;
    double vis_wx,vis_wz;
    float vis_view[5];
    int    vis_ix[ATLAS_TILE_SLOTS];
    int    vis_iy[ATLAS_TILE_SLOTS];
    int    vis_slot[ATLAS_TILE_SLOTS];
    float  vis_d2[ATLAS_TILE_SLOTS];

    QlcgFrameWorkers *workers;
    AtlasBake bake[QLCG_FRAME_LANES];
    double   last_ms;

    AtlasTileSlot slots[ATLAS_TILE_SLOTS];
    float         meta_buf[ATLAS_TILE_SLOTS * 2 * 4];
    const MacroWorld *world;
    uint32_t delta_revision,surface_revision;
    float river_mix;
};

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
    for(int i=0;i<QLCG_FRAME_LANES;i++) tc->bake[i].slot=-1;
    tc->meta_dirty = true;
    tc->last_ms = qlcg_time_ms();
    uint8_t color[3];
    qlcg_land_color(0,0,color);

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
    qlcg_frame_workers_free(tc->workers);
    for(int i=0;i<QLCG_FRAME_LANES;i++) qlcg_surface_worker_free(tc->bake[i].sampler);
    free(tc);
}

void qlcg_atlas_tiles_invalidate(struct AtlasTileCache *tc) {
    if (!tc) return;
    for (int i = 0; i < ATLAS_TILE_SLOTS; i++) {
        tc->slots[i].level = 0;
        tc->slots[i].alpha = 0.0f;
        tc->slots[i].complete = false;
    }
    tc->vis_count=tc->ready_count=0;
    tc->vis_cached=false;
    for(int i=0;i<QLCG_FRAME_LANES;i++) {
        AtlasBake *b=&tc->bake[i];
        b->slot=-1;
        memset(b->nodes,0,sizeof(b->nodes));
        b->patch_ready=false;
        qlcg_surface_worker_free(b->sampler);b->sampler=NULL;
    }
    tc->meta_dirty = true;
}

/* 采样一行地表数据到滚动环 */
static SurfaceMapPoint tile_node(AtlasBake *bake,const MacroWorld *w,int x,int z) {
    unsigned key=qlcg_hash_u32((uint32_t)x*374761393u^(uint32_t)z*668265263u);
    AtlasNode *node=&bake->nodes[key%4096];
    if(!node->ready||node->x!=x||node->z!=z) {
        node->x=x; node->z=z; node->ready=true;
        qlcg_sample_surface_detail_map(w,x*bake->node_step,z*bake->node_step,&node->point);
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

static SurfaceMapPoint tile_surface(AtlasBake *bake,const MacroWorld *w,
                                    double wx,double wz,double step) {
    SurfaceMapPoint result={.depth=-1,.source_depth=-1};
    if(step>32) {
        qlcg_sample_surface_map(w,wx,wz,&result.h,&result.wl,&result.temp,&result.wet_gain);
        if(result.wl>result.h+.02f) result.depth=result.wl-result.h;
        return result;
    }
    int x=(int)floor(wx/bake->node_step),z=(int)floor(wz/bake->node_step);
    float u=(float)(wx/bake->node_step-x),v=(float)(wz/bake->node_step-z);
    SurfaceMapPoint b=tile_node(bake,w,x+1,z),c=tile_node(bake,w,x,z+1);
    SurfaceMapPoint a,d={0};
    float wa,wb,wc,wd=0;
    if(bake->node_step>2) {
        a=tile_node(bake,w,x,z); d=tile_node(bake,w,x+1,z+1);
        wa=(1-u)*(1-v); wb=u*(1-v); wc=(1-u)*v; wd=u*v;
    } else {
        a=u+v<=1?tile_node(bake,w,x,z):tile_node(bake,w,x+1,z+1);
        wb=u+v<=1?u:1-v; wc=u+v<=1?v:1-u; wa=1-wb-wc;
    }
    result.h=wa*a.h+wb*b.h+wc*c.h+wd*d.h;
    result.temp=wa*a.temp+wb*b.temp+wc*c.temp+wd*d.temp;
    result.depth=wa*a.depth+wb*b.depth+wc*c.depth+wd*d.depth;
    result.source_depth=wa*a.source_depth+wb*b.source_depth+wc*c.source_depth+wd*d.source_depth;
    result.coast_tint=wa*a.coast_tint+wb*b.coast_tint+wc*c.coast_tint+wd*d.coast_tint;
    result.wet_gain=wa*a.wet_gain+wb*b.wet_gain+wc*c.wet_gain+wd*d.wet_gain;
    if(bake->node_step>2) {
        float sx[4],sz[4];
        tile_smooth_weights(u,sx); tile_smooth_weights(v,sz);
        if(!bake->patch_ready || bake->patch_x!=x || bake->patch_z!=z) {
            for(int j=0;j<4;j++) for(int i=0;i<4;i++)
                bake->patch_height[j][i]=tile_node(bake,w,x+i-1,z+j-1).h;
            bake->patch_x=x; bake->patch_z=z; bake->patch_ready=true;
        }
        result.h=0;
        for(int j=0;j<4;j++) for(int i=0;i<4;i++)
            result.h+=sx[i]*sz[j]*bake->patch_height[j][i];
    }
    result.wl=result.h+fmaxf(result.depth,result.source_depth);
    return result;
}

static void tile_sample_batch(AtlasBake *bake, const MacroWorld *w, int row) {
    int rr = ((row % 3) + 3) % 3;
    double step = bake->span / ATLAS_TILE_SAMPLE_PX;
    double wz = bake->z0 + ((float)row - 0.5f) * step;
    int end=qlcg_clampi(bake->column+8,0,TILE_RING_W);
    for (int j = bake->column; j < end; j++) {
        double wx = bake->x0 + ((float)j - 1.5f) * step;
        bake->ring[rr][j]=tile_surface(bake,w,wx,wz,step);
    }
    bake->column=end==TILE_RING_W?0:end;
    if(bake->column==0) bake->sample++;
}

/* 由环上三行着色一行并写入瓦片缓冲 */
static void tile_emit_row(AtlasBake *bake, const MacroWorld *w, int row) {
    int rp = (((row - 1) % 3) + 3) % 3;
    int rc = ((row % 3) + 3) % 3;
    int rn = (((row + 1) % 3) + 3) % 3;
    double step = bake->span / ATLAS_TILE_SAMPLE_PX;
    uint8_t *out = &bake->pix[(size_t)row * ATLAS_TILE_PX * 4];
    double wz = bake->z0 + ((float)row - 0.5f) * step;

    for (int c = 0; c < ATLAS_TILE_PX; c++) {
        const SurfaceMapPoint *mid = &bake->ring[rc][c + 1];
        float h  = mid->h;
        float wl = mid->wl;
        float tp = mid->temp;

        float dhx = (bake->ring[rc][c + 2].h - bake->ring[rc][c].h) / (2.0f * step);
        float dhz = (bake->ring[rn][c + 1].h - bake->ring[rp][c + 1].h) / (2.0f * step);

        uint8_t col[3];
        double wx = bake->x0 + ((float)c - 0.5f) * step;
        float hc = h;
        bool water = mid->depth > .02f;
        bool near_water = water && step<=8;
        int wi=(row*ATLAS_TILE_PX+c)*4;
        bake->water_pix[wi]=mid->depth;
        bake->water_pix[wi+1]=qlcg_clampf((tp+45)/90,0,1);
        /* 米制高程直接存储 */
        bake->water_pix[wi+2]=h;
        bake->water_pix[wi+3]=step<=8?mid->source_depth:-1.0f;
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
            float moss=mid->wet_gain*.30f*(1-qlcg_snow_value(tp));
            const float wet_tint[3]={74,106,66};
            for(int ch=0;ch<3;ch++) col[ch]=(uint8_t)qlcg_lerpf(col[ch],wet_tint[ch],moss);
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

static void tile_upload(struct AtlasTileCache *tc,const AtlasBake *bake) {
    int slot=bake->slot;
    int sx = (slot % ATLAS_TILE_GRID) * ATLAS_TILE_PX;
    int sy = (slot / ATLAS_TILE_GRID) * ATLAS_TILE_PX;
    glBindTexture(GL_TEXTURE_2D, tc->tex);
    glTexSubImage2D(GL_TEXTURE_2D,0,sx,sy,ATLAS_TILE_PX,ATLAS_TILE_PX,GL_RGBA,GL_UNSIGNED_BYTE,bake->pix);
    glBindTexture(GL_TEXTURE_2D,tc->water);
    glTexSubImage2D(GL_TEXTURE_2D,0,sx,sy,ATLAS_TILE_PX,ATLAS_TILE_PX,GL_RGBA,GL_FLOAT,bake->water_pix);
}

/* 槽位取模同着色器，平移不换槽 */
static int tile_slot_of(int ix, int iy) {
    return (ix & (ATLAS_TILE_GRID - 1)) | ((iy & (ATLAS_TILE_GRID - 1)) << 4);
}

static void reset_bake(AtlasBake *bake) {
    bake->sample=-1;bake->column=0;bake->corner=0;bake->emit=0;
    bake->patch_ready=false;
}

static void tile_acquire_slot(struct AtlasTileCache *tc,AtlasBake *bake,const MacroWorld *w,
                              int ix,int iy,double span) {
    int pick = tile_slot_of(ix, iy);
    AtlasTileSlot *s = &tc->slots[pick];
    s->ix = ix;
    s->iy = iy;
    s->level = tc->level;
    s->complete = false;
    s->alpha = 0.0f;
    bake->slot=pick;bake->serial=qlcg_world_flat(w)&&w->planes;
    bake->x0=ix*span-w->circumference*.5;
    bake->z0=iy*span-qlcg_world_depth(w)*.5;
    bake->span=span;
    double step=span/ATLAS_TILE_SAMPLE_PX;
    bake->node_step=fmax(2,exp2(ceil(log2(step*(step>8?8:2)))));
    reset_bake(bake);
    tc->meta_dirty = true;
}

static void clear_slot(struct AtlasTileCache *tc,int slot) {
    tc->slots[slot]=(AtlasTileSlot){0};
    for(int i=0;i<QLCG_FRAME_LANES;i++) if(tc->bake[i].slot==slot) tc->bake[i].slot=-1;
    tc->meta_dirty=true;
}

static void invalidate_tile_changes(struct AtlasTileCache *tc,const MacroWorld *w) {
    if(tc->world!=w||tc->delta_revision!=w->delta_version) qlcg_surface_deltas_prepare((MacroWorld *)w);
    QlcgSurfaceBounds changes[QLCG_SURFACE_DIRTY_CAP];
    int count=qlcg_world_surface_changes(w,tc->delta_revision,tc->surface_revision,changes);
    if(tc->world!=w||count<0) qlcg_atlas_tiles_invalidate(tc);
    else if(count>0&&tc->level>0) {
        double span=w->circumference/(double)(1<<tc->level),step=span/ATLAS_TILE_SAMPLE_PX;
        double nodes=fmax(2,exp2(ceil(log2(step*(step>8?8:2)))));
        double margin=3*step+(step<=32?nodes*3:0);
        bool geometry_changed=false;
        for(int i=0;i<count;i++) geometry_changed|=!changes[i].appearance_only;
        if(geometry_changed) for(int i=0;i<QLCG_FRAME_LANES;i++) {
            memset(tc->bake[i].nodes,0,sizeof(tc->bake[i].nodes));
            tc->bake[i].patch_ready=false;
        }
        for(int i=0;geometry_changed&&i<ATLAS_TILE_SLOTS;i++) {
            AtlasTileSlot *slot=&tc->slots[i];
            if(slot->level!=tc->level) continue;
            double x=slot->ix*span-w->circumference*.5,z=slot->iy*span-qlcg_world_depth(w)*.5;
            for(int j=0;j<count;j++) if(qlcg_surface_bounds_overlap(w,&changes[j],
                x-margin,z-margin,x+span+margin,z+span+margin)) { clear_slot(tc,i);break; }
        }
    }
    tc->world=w;tc->delta_revision=w->delta_version;tc->surface_revision=w->surface_revision;
}

typedef struct {
    const MacroWorld *world;
    AtlasBake *bake[QLCG_FRAME_LANES];
    double deadline;
} AtlasBakeBatch;

static void bake_until(AtlasBake *bake,const MacroWorld *w,double deadline) {
    double step=bake->span/ATLAS_TILE_SAMPLE_PX;
    double margin=step*3+bake->node_step*2;
    while(step<=32&&bake->corner<4&&qlcg_time_ms()<deadline) {
        int k=bake->corner++;
        double wx=bake->x0+(k%2?bake->span+margin:-margin);
        double wz=bake->z0+(k/2?bake->span+margin:-margin);
        int cell=qlcg_macro_cell_at(w,wx,wz);
        if(qlcg_source_candidate(w,cell)&&!qlcg_source_network(w,cell,0)&&qlcg_source_network(w,cell,1)) {
            memset(bake->nodes,0,sizeof(bake->nodes));
            bake->patch_ready=false;
        }
    }
    if(step<=32&&bake->corner<4) return;
    while(bake->emit<ATLAS_TILE_PX&&qlcg_time_ms()<deadline) {
        if(bake->sample<=bake->emit+1&&bake->sample<=ATLAS_TILE_PX) tile_sample_batch(bake,w,bake->sample);
        else tile_emit_row(bake,w,bake->emit++);
    }
}

static void bake_parallel(void *context,int lane) {
    AtlasBakeBatch *batch=context;
    AtlasBake *bake=batch->bake[lane];
    const MacroWorld *w=qlcg_surface_worker_begin(&bake->sampler,batch->world);
    if(w) {
        bake_until(bake,w,batch->deadline);
        if(qlcg_surface_worker_end(&bake->sampler)) return;
    }
    reset_bake(bake);
    memset(bake->nodes,0,sizeof(bake->nodes));
    bake->serial=true;
}

static void tile_collect_visible(struct AtlasTileCache *tc,const AtlasRenderer *ar,const MacroWorld *w,
                                 float vp_x,float vp_y,float vp_w,float vp_h,double span) {
    float view[]={vp_x,vp_y,vp_w,vp_h,ar->zoom};
    if(tc->vis_cached&&tc->vis_projection==ar->planet_view&&tc->vis_wx==ar->cam_wx&&
       tc->vis_wz==ar->cam_wz&&!memcmp(tc->vis_view,view,sizeof(view))) return;
    tc->vis_cached=true;tc->vis_projection=ar->planet_view;
    tc->vis_wx=ar->cam_wx;tc->vis_wz=ar->cam_wz;
    memcpy(tc->vis_view,view,sizeof(view));
    /* 收集可见瓦片：屏幕 5×5 采样点求经度与纬度跨度 */
    int n=1<<tc->level;
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
    tc->vis_count=0;
    if(!any) return;

    int ix0 = (int)floor(u0 * n) - 1;
    int ix1 = (int)floor(u1 * n) + 1;
    int iy0 = qlcg_clampi((int)floor(v0 * n * (qlcg_world_flat(w)?1:.5)) - 1, 0, (qlcg_world_flat(w)?n:n/2) - 1);
    int iy1 = qlcg_clampi((int)floor(v1 * n * (qlcg_world_flat(w)?1:.5)) + 1, 0, (qlcg_world_flat(w)?n:n/2) - 1);

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
}

bool qlcg_atlas_tiles_update(AtlasRenderer *ar,
                             const MacroWorld *w,
                             float vp_x, float vp_y, float vp_w, float vp_h,
                             double budget_ms) {
    if (!ar || !w) return false;
    struct AtlasTileCache *tc = ar->tiles;
    bool enabled = (ar->planet_view != PLANET_VIEW_GLOBE) && !ar->sat_mode && !ar->tiles_off;
    int level = enabled ? tile_pick_level(ar, w, tc ? tc->level : 0, vp_w, vp_h) : 0;
    if (!tc) {
        if (!level) return false;
        ar->tiles = tc = tiles_create();
        if (!tc) return false;
    }
    invalidate_tile_changes(tc,w);
    double t0 = qlcg_time_ms();
    float dt = (float)(t0 - tc->last_ms);
    tc->last_ms = t0;
    dt = qlcg_clampf(dt, 0.0f, 120.0f);

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

    double span=w->circumference/(double)(1<<tc->level);
    tile_collect_visible(tc,ar,w,vp_x,vp_y,vp_w,vp_h,span);
    tc->ready_count=0;
    bool wanted[ATLAS_TILE_SLOTS]={0};
    for(int v=0;v<tc->vis_count;v++) {
        int slot=tile_slot_of(tc->vis_ix[v],tc->vis_iy[v]);
        tc->vis_slot[v]=-1;
        if(wanted[slot]) continue;
        wanted[slot]=true;
        AtlasTileSlot *tile=&tc->slots[slot];
        if(tile->level==tc->level&&tile->ix==tc->vis_ix[v]&&tile->iy==tc->vis_iy[v]) tc->vis_slot[v]=slot;
        else if(tile->level>0) clear_slot(tc,slot);
    }
    for(int i=0;i<QLCG_FRAME_LANES;i++) {
        int slot=tc->bake[i].slot;
        if(slot>=0&&!wanted[slot]) clear_slot(tc,slot);
    }
    double deadline=t0+budget_ms;
    int uploaded=0;
    while(qlcg_time_ms()<deadline&&uploaded<8) {
        AtlasBakeBatch batch={.world=w,.deadline=deadline};
        int lanes=0;
        memset(wanted,0,sizeof(wanted));
        for(int i=0;i<QLCG_FRAME_LANES;i++) {
            AtlasBake *bake=&tc->bake[i];
            for(int v=0;bake->slot<0&&v<tc->vis_count;v++) {
                int slot=tile_slot_of(tc->vis_ix[v],tc->vis_iy[v]);
                if(wanted[slot]) continue;
                wanted[slot]=true;
                if(tc->vis_slot[v]>=0||tc->slots[slot].level>0) continue;
                tile_acquire_slot(tc,bake,w,tc->vis_ix[v],tc->vis_iy[v],span);
                tc->vis_slot[v]=slot;
            }
            if(bake->slot<0) continue;
            if(bake->serial) bake_until(bake,w,fmin(deadline,qlcg_time_ms()+2));
            else batch.bake[lanes++]=bake;
        }
        if(lanes) {
            if(!tc->workers&&lanes>1) tc->workers=qlcg_frame_workers_create();
            qlcg_frame_workers_run(tc->workers,bake_parallel,&batch,lanes);
        }
        int finished=0;
        for(int i=0;i<QLCG_FRAME_LANES;i++) {
            AtlasBake *bake=&tc->bake[i];
            if(bake->slot<0||bake->emit<ATLAS_TILE_PX) continue;
            tile_upload(tc,bake);
            tc->slots[bake->slot].complete=true;
            bake->slot=-1;finished++;
        }
        uploaded+=finished;
        if(!finished) break;
    }
    changed|=uploaded>0;

    /* 淡入 */
    for (int i = 0; i < ATLAS_TILE_SLOTS; i++) {
        AtlasTileSlot *s = &tc->slots[i];
        if (s->level <= 0 || !s->complete || s->alpha >= 1.0f) continue;
        s->alpha = qlcg_minf(1.0f, s->alpha + dt * TILE_FADE_PER_MS);
        tc->meta_dirty = true;
        changed = true;
    }

    tc->river_mix=tc->vis_count&&span/ATLAS_TILE_SAMPLE_PX<=8?1.0f:0.0f;
    for(int i=0;i<tc->vis_count;i++) {
        int slot=tc->vis_slot[i];
        float alpha=slot<0?0:tc->slots[slot].alpha;
        tc->river_mix=fminf(tc->river_mix,alpha);
        if(alpha>.92f) tc->ready_count++;
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
