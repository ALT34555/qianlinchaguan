#include "hydrology_oxbow.h"
/* 地表单点采样：地质、水文、植被 */
#include "planet_macro.h"
#include "surface_internal.h"
#include "hydrology_river.h"
#include "hydrology_sample.h"
#include "hydrology_archive.h"
#include "surface_detail.h"
#include "surface_coast.h"
#include "hydrology_lakes.h"
#include "surface_islands.h"
#include "gen/hydrology_dimensions.h"
#include "gen/gen_noise.h"
#include "../../core/math_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void qlcg_chunk_shape(const float h[9], float *relief, float *slope, float *relative) {
    float low=h[0],high=h[0],sum=0;
    const float step=CHUNK_SIZE*.5f;
    *slope=0;
    for (int z=0; z<3; z++) for (int x=0; x<3; x++) {
        int i=z*3+x;
        low=fminf(low,h[i]); high=fmaxf(high,h[i]); sum+=h[i];
        if (x<2) *slope=fmaxf(*slope,fabsf(h[i+1]-h[i])/step);
        if (z<2) *slope=fmaxf(*slope,fabsf(h[i+3]-h[i])/step);
    }
    *relief=high-low;
    *relative=h[4]-(sum-h[4])*.125f;
}

static int chunk_material(const MacroWorld *w,int cx,int cz,int base) {
    float h[9],relief,slope,relative;
    const float step=CHUNK_SIZE*.5f;
    float x=(float)cx*CHUNK_SIZE,z=(float)cz*CHUNK_SIZE;
    int cell=qlcg_world_cell_at_wxz(w,x+step,z+step);
    for (int dz=0; dz<3; dz++) for (int dx=0; dx<3; dx++)
        h[dz*3+dx]=(float)(base?qlcg_natural_base_height(w,x+dx*step,z+dz*step):
            qlcg_natural_detail_height(w,x+dx*step,z+dz*step));
    qlcg_chunk_shape(h,&relief,&slope,&relative);
    float aridity=w->rainfall[cell]/fmaxf(150,w->evaporation[cell]);
    int type=qlcg_classify_chunk(false,false,false,false,false,h[4],w->temperature[cell],
                                aridity,relief,slope,relative,0);
    return qlcg_chunk_material(type);
}
int qlcg_world_natural_chunk_material(const MacroWorld *w,int cx,int cz) { return chunk_material(w,cx,cz,0); }
int qlcg_world_base_chunk_material(const MacroWorld *w,int cx,int cz) { return chunk_material(w,cx,cz,1); }

float dist_to_seg2d(float px, float pz, float ax, float az, float bx, float bz, float *out_t) {
    float dx = bx - ax;
    float dz = bz - az;
    float l2 = dx * dx + dz * dz;
    float t  = (l2 > 1e-6f) ? qlcg_clampf(((px - ax) * dx + (pz - az) * dz) / l2, 0.0f, 1.0f) : 0.0f;
    if (out_t) *out_t = t;
    float qx = ax + t * dx - px;
    float qz = az + t * dz - pz;
    return sqrtf(qx * qx + qz * qz);
}

/* 双线性宏观量与四角格号 */
typedef struct {
    float height, temperature, moisture, desert, forest;
    float rainfall, evaporation, uplift;
    int   idx[4];
    float w[4];
    double tx, tz;          /* 格内位置，岸边水位收口用 */
} MacroSample;

static void macro_sample(const MacroWorld *w, float wx, float wz, MacroSample *s) {
    SurfaceMacroContext c=qlcg_macro_context(w,wx,wz);
    s->idx[0]=c.a; s->idx[1]=c.b; s->idx[2]=c.c; s->idx[3]=c.d;
    s->w[0]=(float)((1-c.tx)*(1-c.tz)); s->w[1]=(float)(c.tx*(1-c.tz));
    s->w[2]=(float)((1-c.tx)*c.tz); s->w[3]=(float)(c.tx*c.tz);
    s->tx=c.tx; s->tz=c.tz;
    s->height=(float)qlcg_read_macro(w,&c,w->height);
    s->temperature=(float)qlcg_read_macro(w,&c,w->temperature);
    s->moisture=(float)qlcg_read_macro(w,&c,w->moisture);
    s->desert=(float)qlcg_read_macro(w,&c,w->desert_factor);
    s->forest=(float)qlcg_read_macro(w,&c,w->forest_bonus);
    s->rainfall=(float)qlcg_read_macro(w,&c,w->rainfall);
    s->evaporation=(float)qlcg_read_macro(w,&c,w->evaporation);
    s->uplift=(float)qlcg_read_macro(w,&c,w->uplift);
}

float qlcg_sample_macro_height(const MacroWorld *w, float wx, float wz) {
    SurfaceMacroContext c=qlcg_macro_context(w,wx,wz);
    return (float)qlcg_read_macro(w,&c,w->height);
}

float qlcg_sample_natural_height(const MacroWorld *w, float wx, float wz) {
    return (float)qlcg_natural_detail_height(w,wx,wz);
}

/* 湖体采样入口 */
int qlcg_lake_shore_at(const MacroWorld *w, float wx, float wz, float *out_level, float *out_d) {
    LakeShoreField f=qlcg_lake_field(w,wx,wz);
    if (out_level) *out_level=f.level;
    if (out_d) *out_d=f.d*w->circumference/w->columns;
    return f.lake;
}

/* 舆图连续基底，复用岸线模板 */
void qlcg_sample_surface_map(const MacroWorld *w, double wx, double wz,
                              float *out_h, float *out_wl, float *out_temp, float *out_wet_gain) {
    SurfaceMacroContext c = qlcg_macro_context(w, wx, wz);
    float base = (float)qlcg_read_macro(w, &c, w->height);
    float h = base;
    float temp = (float)qlcg_read_macro(w, &c, w->temperature);
    double island = 0;
    LakeShoreField lake = {0};
    if (qlcg_island_coast(w, wx, wz, base, &island)) h = (float)island;
    else {
        lake = qlcg_lake_field(w, wx, wz);
        double natural=base;
        if(qlcg_river_archive_terrain(w)>=13)
            natural+=qlcg_map_regional_relief(w,wx,wz,base,qlcg_read_macro(w,&c,w->uplift));
        h = (float)qlcg_coast_surface(w,&c,wx,wz,base,natural).height;
        h = qlcg_lake_height(&lake, h);
    }
    if(h>0&&!lake.lake)h=(float)qlcg_headwater_map_height(w,wx,wz,h);
    float wl = h <= 0 ? 0 : -1e9f;
    if (h > 0 && lake.lake && lake.d < 0 && lake.level > h + .02f)
        wl = lake.level;
    if (h>0 && !lake.lake) {
        RiverSurfaceSample oxbow={0};
        oxbow.height=h; oxbow.level=wl;
        qlcg_oxbow_surface(w,wx,wz,&oxbow);
        h=oxbow.height; wl=oxbow.level;
    }
    if (h > 0) temp += (fmaxf(0, base) - h) * .0065f;
    float wet = 0, desert = 0, forest = 0;
    qlcg_surface_apply_deltas(w, wx, wz, &h, &wl, &wet, &desert, &forest);
    if (wl <= h + .02f) wl = -1e9f;
    if (out_h) *out_h = h;
    if (out_wl) *out_wl = wl;
    if (out_temp) *out_temp = temp;
    if (out_wet_gain) *out_wet_gain = 0;
}



/* 地表标量复用完整几何 */
void qlcg_sample_surface_lod(const MacroWorld *w, float wx, float wz, float lod_m,
                             float *out_h, float *out_wl, float *out_temp,
                             float *out_wet, float *out_desert, float *out_forest,
                             float *out_aridity, float *out_uplift) {
    (void)lod_m;
    qlcg_sample_surface_water(w,wx,wz,0,out_h,out_wl,out_temp,out_wet,
                               out_desert,out_forest,out_aridity,out_uplift,NULL,NULL);
}

/* 全细节采样：lod_m = 0 */
void qlcg_sample_surface_point(const MacroWorld *w, float wx, float wz,
                               float *out_h, float *out_wl, float *out_temp,
                               float *out_wet, float *out_desert, float *out_forest,
                               float *out_aridity, float *out_uplift) {
    qlcg_sample_surface_lod(w, wx, wz, 0.0f, out_h, out_wl, out_temp,
                            out_wet, out_desert, out_forest, out_aridity, out_uplift);
}

/* base flowSpeed：坡度流量河宽定流速 */
static float flow_speed(float grade, float discharge, float width) {
    return fminf(8.0f, 0.12f + sqrtf(fmaxf(0.0f, grade)) * 3.0f +
                 log1pf(discharge / fmaxf(1.0f, width)) * 0.14f);
}

/* 四态流量缩放对齐 base [1,.4,0,1.6] */
static float form_flow_scale(int form, int freeze) {
    static const float FORM[4] = { 1.0f, 0.4f, 0.0f, 1.6f };
    float fz = (freeze == 2) ? 0.0f : ((freeze == 1) ? 0.4f : 1.0f);
    int f = (form >= 0 && form < 4) ? form : 0;
    return FORM[f] * fz;
}

/* 固定空间噪声：只按地点取值，不含时间 */
static float ice_noise_at(const MacroWorld *w, float wx, float wz) {
    float n = qlcg_base_noise(wx / 9.0f, 0.0f, wz / 9.0f, w->opt.seed ^ 0x51CEu);
    return qlcg_clampf(0.5f + 0.5f * n, 0.0f, 1.0f);
}

/* 近岸权重：湖岸距离与河岸距离取大者 */
static float shore_weight_at(const MacroWorld *w, const LakeShoreField *lf,
                             const HydrologySample *nw, float depth, int kind) {
    float b = 0.0f;
    if (lf && lf->lake && lf->d < 0.0f && w->columns > 0) {
        float shore_m = -lf->d * w->circumference / (float)w->columns;
        b = 1.0f - qlcg_smoothf(2.0f, 18.0f, shore_m);
    }
    if (kind == WATER_KIND_RIVER && nw->bank_distance < 9.0f) {
        float rb = 1.0f - qlcg_smoothf(1.0f, 8.0f, nw->bank_distance);
        if (rb > b) b = rb;
    }
    /* 场缺岛岸距离时以浅水补充 */
    if (b <= 0.0f && depth > 0.0f && depth < 1.4f)
        b = qlcg_maxf(b, 1.0f - qlcg_smoothf(.2f, 1.2f, depth));
    return qlcg_clampf(b, 0.0f, 1.0f);
}

/* 统一气候时刻：未设置时按季中相位兜底 */
static void ice_time_of(const MacroWorld *w, int season_idx, IceTime *t) {
    memset(t, 0, sizeof(*t));
    if (w->ice_time_valid) { *t = w->ice_time; return; }
    int s = (season_idx >= 0 && season_idx < 4) ? season_idx : 0;
    t->phase = (float)s + .5f;
    t->climate_day = QLCG_ICE_PER_SEASON * (double)t->phase;
}

/* 由 IceSite 求自然冰，写入水面样本 */
static void ice_into_water(const MacroWorld *w, const IceSite *site, int season_idx,
                           SurfaceWater *out) {
    IceTime t;
    ice_time_of(w, season_idx, &t);
    IceSample o;
    qlcg_ice_evaluate(site, &t, &o);
    out->ice_thickness = o.thickness;
    out->ice_quality   = o.quality;
    out->ice_coverage  = o.coverage;
    out->ice_strength  = o.strength;
    out->ice_stage     = o.stage;
    out->ice_top       = (site->depth > .02f) ? site->top : -1e9f;
    out->ice_bottom    = (o.thickness > 1e-4f) ? o.bottom : -1e9f;
    out->freeze = (o.coverage < .5f) ? 0u
                : ((o.thickness >= .12f && o.quality >= .5f) ? 2u : 1u);
}

/* 兼容旧入口：只按湖/静水构造 IceSite */
void qlcg_water_ice_state(const MacroWorld *w, float wz, int season, float bed, float level,
                          float annual_temperature, SurfaceWater *out) {
    IceSite site;
    memset(&site, 0, sizeof(site));
    site.annual_temp = annual_temperature;
    site.latitude = w->circumference > 0
                  ? -wz / qlcg_world_depth(w) * 3.1415926f : 0.0f;
    site.bed = bed;
    site.top = level;
    site.depth = (level > bed + .02f) ? (level - bed) : 0.0f;
    site.noise = .5f;
    site.kind = (site.depth > .02f) ? ICE_KIND_LAKE : ICE_KIND_DRY;
    out->temperature = annual_temperature;
    ice_into_water(w, &site, season, out);
}

/* 填充点级水体属性：水位 → 原始流速 → 冰 → 兼容输出 */
static float shore_depth_at(float h,float wl,float shore_level) {
    if(wl>h+.02f) return wl-h;
    return shore_level>-1e8f?fminf(0,shore_level-h):-1.0f;
}

static void water_fill(const MacroWorld *w, const MacroSample *s, const HydrologySample *nw,
                       float wx, float wz, float bed, float wl, int season_idx,
                       const LakeShoreField *lf, SurfaceWater *out, IceSite *out_site) {
    memset(out, 0, sizeof(*out));
    out->level = -1e9f;
    out->ice_top = out->ice_bottom = -1e9f;

    float h = bed;
    out->bed = h;
    out->shore_depth = shore_depth_at(h,wl,nw->shore_level);
    int kind = nw->kind;

    int form = WATER_FORM_NORMAL;
    if (wl <= h + 0.02f) {
        wl = -1e9f;
        kind = WATER_KIND_DRY;
    } else if (kind == WATER_KIND_DRY) kind = WATER_KIND_LAKE;

    /* 原始流速：静水为零，瀑布取落差速度，先于结冰计算 */
    float raw = 0.0f;
    if (kind == WATER_KIND_RIVER || kind == WATER_KIND_FALL) {
        float width = fmaxf(1.0f, nw->width_m);
        raw = nw->channel_depth>0?(float)fmin(8,qlcg_river_mean_speed(nw->discharge,width,nw->channel_depth)):
            flow_speed(nw->grade,nw->discharge,width);
        if (kind == WATER_KIND_FALL) {
            float drop = fmaxf(0.0f, nw->grade * 32.0f);
            raw = fmaxf(raw, sqrtf(9.81f * drop));
        }
    }

    out->level       = wl;
    out->depth       = (wl > -1e8f) ? (wl - h) : 0.0f;
    out->kind        = (uint8_t)kind;
    out->form        = (uint8_t)form;
    out->raw_speed   = raw;
    if(kind==WATER_KIND_RIVER||kind==WATER_KIND_FALL) {
        out->direction_x = nw->ve;
        out->direction_z = -nw->vn;
    }
    out->raw_vx      = nw->ve * raw;
    out->raw_vz      = -nw->vn * raw;
    out->discharge   = nw->discharge;
    out->decorative  = nw->decorative!=0;
    out->width_m     = nw->width_m;
    out->river_sediment=(uint8_t)(255*qlcg_clampf(nw->sediment,0,1)+.5f);
    out->fresh=nw->fresh;
    out->source_effects=nw->source_effects;
    if(kind==WATER_KIND_DRY) {
        if(out->source_effects.salt==255) out->source_effects.salt=0;
        out->source_effects.spring=0;
    }
    out->temperature = s->temperature;

    IceSite site;
    memset(&site, 0, sizeof(site));
    site.annual_temp   = s->temperature;
    site.latitude      = w->circumference > 0
                       ? -wz / qlcg_world_depth(w) * 3.1415926f : 0.0f;
    site.bed           = h;
    site.top           = wl;
    site.depth         = out->depth;
    site.raw_speed     = raw;
    site.lateral       = nw->lateral;
    site.bank_distance = nw->bank_distance;
    site.grade         = nw->grade;
    site.kind          = (uint8_t)kind;
    site.noise         = ice_noise_at(w, wx, wz);
    site.shore_weight  = shore_weight_at(w, lf, nw, out->depth, kind);

    ice_into_water(w, &site, season_idx, out);
    if (out_site) *out_site = site;

    /* 兼容输出：冻结因子只缩放可见流速 */
    float speed = raw * form_flow_scale(form, out->freeze);
    out->speed = speed;
    out->vx    = nw->ve * speed;
    out->vz    = -nw->vn * speed;
}

/* 点级自然水：供 3D 水面、探针与玩家共用 */
void qlcg_sample_water_at(const MacroWorld *w, float wx, float wz, int season_idx,
                          float bed, float wl, SurfaceWater *out, IceSite *out_ice) {
    if (!out) return;
    MacroSample s;
    macro_sample(w, wx, wz, &s);
    HydrologySample nw;
    LakeShoreField lf = qlcg_lake_field(w, wx, wz);
    float natural = qlcg_sample_natural_height(w, wx, wz);
    if (!isfinite(natural)) natural = bed;
    qlcg_hydrology_sample(w,s.idx,wx,wz,natural,&lf,&nw,1);
    water_fill(w, &s, &nw, wx, wz, bed, wl, season_idx, &lf, out, out_ice);
}

void qlcg_sample_water(const MacroWorld *w, float wx, float wz, int season_idx,
                       SurfaceWater *out, IceSite *out_ice) {
    if(!out) return;
    qlcg_sample_surface_water(w,wx,wz,season_idx,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,out,out_ice);
}

/* 合并采样：标量与水体一次算齐 */
static void surface_geometry(const MacroWorld *w,float wx,float wz,int source_mode,
                             MacroSample *s,HydrologySample *nw,LakeShoreField *lf,
                             float *h,float *wl,float *wet,float *desert,float *forest,CoastSample *coast) {
    macro_sample(w,wx,wz,s);
    *lf=qlcg_lake_field(w,wx,wz);
    *h=qlcg_lake_height(lf,(float)qlcg_natural_detail_coast(w,wx,wz,coast));
    qlcg_hydrology_sample(w,s->idx,wx,wz,*h,lf,nw,source_mode);
    *h-=nw->cut;
    *wl=nw->level;
    *wet=fmaxf(s->moisture,nw->damp); *desert=s->desert; *forest=s->forest;
    qlcg_surface_apply_deltas(w,wx,wz,h,wl,wet,desert,forest);
    if (*wl <= *h+.02f) *wl=-1e9f;
}

void qlcg_sample_surface_detail_map(const MacroWorld *w,double wx,double wz,
                                    SurfaceMapPoint *out) {
    MacroSample s;
    HydrologySample nw;
    LakeShoreField lf;
    float wet,desert,forest;
    CoastSample coast;
    surface_geometry(w,(float)wx,(float)wz,0,&s,&nw,&lf,&out->h,&out->wl,&wet,&desert,&forest,&coast);
    out->coast_tint=coast.weight*(coast.sand-coast.mud);
    out->wet_gain=0;
    out->temp=s.temperature;
    float depth=shore_depth_at(out->h,out->wl,nw.shore_level);
    out->depth=nw.decorative?fminf(0,depth):depth;
    out->source_depth=nw.decorative?depth:fminf(0,depth);
}

void qlcg_sample_surface_water(const MacroWorld *w, float wx, float wz, int season_idx,
                               float *out_h, float *out_wl, float *out_temp, float *out_wet,
                               float *out_desert, float *out_forest, float *out_aridity,
                               float *out_uplift, SurfaceWater *out_water, IceSite *out_ice) {
    MacroSample s;
    HydrologySample nw;
    LakeShoreField lf;
    float h,wl,bwt,bds,bfr;
    CoastSample coast;
    surface_geometry(w,wx,wz,1,&s,&nw,&lf,&h,&wl,&bwt,&bds,&bfr,&coast);
    if (out_h)       *out_h       = h;
    if (out_wl)      *out_wl      = wl;
    if (out_temp)    *out_temp    = s.temperature;
    if (out_wet)     *out_wet     = bwt;
    if (out_desert)  *out_desert  = bds;
    if (out_forest)  *out_forest  = bfr;
    if (out_aridity) *out_aridity = s.rainfall / qlcg_maxf(150.0f, s.evaporation);
    if (out_uplift)  *out_uplift  = s.uplift;
    if (out_water) {
        water_fill(w,&s,&nw,wx,wz,h,wl,season_idx,&lf,out_water,out_ice);
        out_water->coast_material[0]=(uint8_t)(255*coast.weight*coast.mud);
        out_water->coast_material[1]=(uint8_t)(255*coast.weight*coast.sand);
        out_water->coast_material[2]=(uint8_t)(255*fmaxf(coast.weight*coast.rock,nw.waterfall_rock));
    }
}
