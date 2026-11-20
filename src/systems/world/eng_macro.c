/* 宏观工程与稀疏记录 */
#include "planet_macro.h"
#include "world_internal.h"
#include "world_surface_dirty.h"
#include "surface_internal.h"
#include "../../core/math_util.h"
#include <string.h>

bool qlcg_eng_has_delta_capacity(const MacroWorld *w, int count) {
    return w && w->delta_count>=0 && w->delta_count<=MAX_MACRO_DELTAS &&
           count>=0 && count<=MAX_MACRO_DELTAS-w->delta_count;
}

void qlcg_eng_append_delta(MacroWorld *w, SparseDeltaRecord delta, uint8_t origin) {
    int i=w->delta_count++;
    w->deltas[i]=delta;
    w->delta_origin[i]=origin;
}

bool qlcg_eng_remove_local_deltas(MacroWorld *w, int first, int end) {
    if (!w || first<0 || first>=end || end>w->delta_count) return false;
    for (int i=first;i<end;i++) if (w->delta_origin[i]!=QLCG_DELTA_LOCAL) return false;
    size_t tail=(size_t)(w->delta_count-end);
    memmove(w->deltas+first,w->deltas+end,tail*sizeof(*w->deltas));
    memmove(w->delta_origin+first,w->delta_origin+end,tail);
    w->delta_count-=end-first;
    w->delta_version++;
    return true;
}

bool push_sparse_delta(MacroWorld *w, EngToolType tool, float wx, float wz,
                       float ex, float ez, float radius, float dh, float width_m) {
    if (!qlcg_eng_has_delta_capacity(w,1)) return false;
    uint32_t before=w->delta_version++;
    qlcg_eng_append_delta(w,(SparseDeltaRecord){tool,wx,wz,ex,ez,radius,dh,width_m},QLCG_DELTA_LOCAL);
    double r=tool==ENG_TOOL_RIVER_DIVERT?width_m*1.4:radius;
    if (tool!=ENG_TOOL_RIVER_DIVERT) { ex=wx; ez=wz; }
    qlcg_world_surface_dirty(w,before,w->surface_revision,
                             fmin(wx,ex)-r,fmin(wz,ez)-r,fmax(wx,ex)+r,fmax(wz,ez)+r,false);
    return true;
}

uint8_t qlcg_eng_legacy_delta_origin(const SparseDeltaRecord *d) {
    bool macro=false;
    switch (d->type) {
        case ENG_TOOL_DEMOLISH: macro=d->delta_h==-95; break;
        case ENG_TOOL_RAISE_MOUNT: macro=d->delta_h==145; break;
        case ENG_TOOL_DIG_LAKE: macro=d->delta_h==-110; break;
        case ENG_TOOL_RIVER_DIVERT:
            return d->width_m>0 && d->radius==d->width_m*2 && d->delta_h==-24;
        case ENG_TOOL_AFFOREST: case ENG_TOOL_RECLAIM_SEA: case ENG_TOOL_FLATTEN:
            macro=d->radius==950 && d->delta_h==0;
            break;
        default: break;
    }
    return macro && d->wx==d->ex && d->wz==d->ez && d->width_m==0;
}

bool qlcg_eng_install_saved_deltas(MacroWorld *w, const SparseDeltaRecord *records,
                                   const uint8_t *origins, int count) {
    if (!qlcg_eng_has_delta_capacity(w,0) || count<0 || count>MAX_MACRO_DELTAS) return false;
    for (int i=0;i<w->delta_count;i++) {
        if (w->delta_origin[i]!=QLCG_DELTA_PROJECTED) continue;
        if (i>=count || origins[i]!=QLCG_DELTA_MACRO || memcmp(records+i,w->deltas+i,sizeof(*records)))
            return false;
    }
    if (count) memcpy(w->deltas,records,(size_t)count*sizeof(*records));
    for (int i=0;i<count;i++) {
        if (i<w->delta_count && w->delta_origin[i]==QLCG_DELTA_PROJECTED) continue;
        w->delta_origin[i]=origins[i];
    }
    w->delta_count=count;
    w->delta_version++;
    return true;
}

static bool project_brush(MacroWorld *w, const SparseDeltaRecord *d, const MacroCellRect *rects,
                          int count, bool *rivers_changed) {
    bool projected=false;
    for (int r=0;r<count;r++) for (int z=rects[r].z0;z<rects[r].z1;z++)
        for (int x=rects[r].x0;x<rects[r].x1;x++) {
            int i=z*w->columns+x;
            float cx,cz;
            qlcg_world_cell_center(w,i,&cx,&cz);
            float dist=hypotf(qlcg_macro_delta_x(w,cx,d->wx),cz-d->wz);
            if (dist>=d->radius) continue;
            float k=1.0f-dist/d->radius;
            float s=k*k*(3.0f-2.0f*k);
            uint8_t water=w->water[i];
            switch (d->type) {
                case ENG_TOOL_DEMOLISH:
                    w->height[i]+=d->delta_h*s;
                    w->forest_bonus[i]=qlcg_clampf(w->forest_bonus[i]-.6f*s,0,1);
                    break;
                case ENG_TOOL_AFFOREST:
                    w->forest_bonus[i]=qlcg_clampf(w->forest_bonus[i]+.75f*s,0,1);
                    w->desert_factor[i]=qlcg_clampf(w->desert_factor[i]-.65f*s,0,1);
                    w->moisture[i]=qlcg_clampf(w->moisture[i]+.25f*s,0,1);
                    break;
                case ENG_TOOL_RECLAIM_SEA:
                    if (w->height[i]<12) w->height[i]=qlcg_lerpf(w->height[i],14,s);
                    w->water[i]=0;
                    w->level[i]=w->height[i];
                    break;
                case ENG_TOOL_RAISE_MOUNT:
                    w->height[i]+=d->delta_h*s;
                    break;
                case ENG_TOOL_DIG_LAKE:
                    w->height[i]+=d->delta_h*s;
                    w->water[i]=2;
                    w->level[i]=qlcg_maxf(w->level[i],w->height[i]+18*s);
                    w->moisture[i]=qlcg_clampf(w->moisture[i]+.45f*s,0,1);
                    break;
                case ENG_TOOL_FLATTEN:
                    w->height[i]=qlcg_lerpf(w->height[i],28,s*.75f);
                    break;
                default: continue;
            }
            w->spill[i]=qlcg_maxf(w->height[i],w->level[i]);
            if (w->water[i]!=water && (w->river_width[i]>0 ||
                (w->main_river_width && w->main_river_width[i]>0))) *rivers_changed=true;
            projected=true;
        }
    return projected;
}

float qlcg_eng_macro_river_distance(const MacroWorld *w, const SparseDeltaRecord *d, float x, float z) {
    float ex=d->wx+qlcg_macro_delta_x(w,d->ex,d->wx);
    float mid=d->wx+(ex-d->wx)*.5f;
    x=mid+qlcg_macro_delta_x(w,x,mid);
    return dist_to_seg2d(x,z,d->wx,d->wz,ex,d->ez,NULL);
}

static float river_cell_strength(const MacroWorld *w, const SparseDeltaRecord *d, int cell) {
    float x,z;
    qlcg_world_cell_center(w,cell,&x,&z);
    float dist=qlcg_eng_macro_river_distance(w,d,x,z);
    return fmaxf(0,1-dist/(2.4f*d->width_m));
}

float qlcg_eng_macro_river_cut(const MacroWorld *w, const SparseDeltaRecord *d, int cell) {
    return 26*river_cell_strength(w,d,cell);
}

static bool project_river(MacroWorld *w, const SparseDeltaRecord *d, const MacroCellRect *rects,
                          int count, bool *rivers_changed) {
    bool projected=false;
    for (int r=0;r<count;r++) for (int z=rects[r].z0;z<rects[r].z1;z++)
        for (int x=rects[r].x0;x<rects[r].x1;x++) {
            int i=z*w->columns+x;
            float k=river_cell_strength(w,d,i);
            if (k<=0) continue;
            w->height[i]-=26*k;
            w->moisture[i]=qlcg_clampf(w->moisture[i]+.42f*k,0,1);
            float width=fmaxf(w->river_width[i],d->width_m*.1f*k);
            double flow=fmax(w->flow[i],95.0f*k);
            *rivers_changed|=width!=w->river_width[i] || flow!=w->flow[i];
            w->river_width[i]=width;
            w->flow[i]=flow;
            projected=true;
        }
    return projected;
}

static int delta_rects(const MacroWorld *w, const SparseDeltaRecord *d, MacroCellRect rects[2]) {
    double x=d->wx,z=d->wz,r=d->radius;
    if (d->type==ENG_TOOL_RIVER_DIVERT) {
        double ex=x+qlcg_macro_delta_x(w,d->ex,d->wx);
        r=2.4*d->width_m;
        return qlcg_macro_rects(w,fmin(x,ex)-r,fmin(z,d->ez)-r,fmax(x,ex)+r,fmax(z,d->ez)+r,rects);
    }
    return qlcg_macro_rects(w,x-r,z-r,x+r,z+r,rects);
}

static void add_atlas_river(MacroWorld *w,const SparseDeltaRecord *d) {
    if (w->river_count>=w->river_cap) return;
    float ex=d->wx+qlcg_macro_delta_x(w,d->ex,d->wx);
    w->rivers[w->river_count++]=(RiverSegment){
        .ax=d->wx,.az=d->wz,.bx=ex,.bz=d->ez,
        .mx=(d->wx+ex)*.5f,.mz=(d->wz+d->ez)*.5f,
        .width_m=d->width_m,.flow=260,.min_level=4
    };
    w->atlas_authored_river_count++;
}

void qlcg_eng_atlas_rivers(MacroWorld *w) {
    w->river_count-=w->atlas_authored_river_count;
    w->atlas_authored_river_count=0;
    for (int i=0;i<w->delta_count;i++)
        if (w->delta_origin[i]!=QLCG_DELTA_LOCAL && w->deltas[i].type==ENG_TOOL_RIVER_DIVERT)
            add_atlas_river(w,w->deltas+i);
}

void qlcg_eng_restore_macro_deltas(MacroWorld *w) {
    bool rebuild=false;
    for (int i=0;i<w->delta_count;i++) {
        const SparseDeltaRecord *d=w->deltas+i;
        if (w->delta_origin[i]!=QLCG_DELTA_MACRO) continue;
        MacroCellRect rects[2];
        int count=delta_rects(w,d,rects);
        bool rivers_changed=false;
        bool projected=d->type==ENG_TOOL_RIVER_DIVERT?project_river(w,d,rects,count,&rivers_changed):
                       project_brush(w,d,rects,count,&rivers_changed);
        if (projected) {
            w->delta_origin[i]=QLCG_DELTA_PROJECTED;
            rebuild=true;
        }
    }
    if (rebuild) qlcg_world_rebuild_atlas_fields(w);
    else qlcg_eng_atlas_rivers(w);
}

EngResult qlcg_eng_macro_brush(MacroWorld *w, EngToolType tool, float wx, float wz, float radius) {
    if (!w || !isfinite(wx) || !isfinite(wz) || !isfinite(radius) || radius<=0 ||
        tool<=ENG_TOOL_NONE || tool>=ENG_TOOL_COUNT || tool==ENG_TOOL_RIVER_DIVERT)
        return ENG_NO_CHANGE;
    if (!qlcg_eng_has_delta_capacity(w,1)) return ENG_CAPACITY;
    float dh=tool==ENG_TOOL_DEMOLISH?-95:tool==ENG_TOOL_RAISE_MOUNT?145:tool==ENG_TOOL_DIG_LAKE?-110:0;
    SparseDeltaRecord d={tool,wx,wz,wx,wz,radius,dh,0};
    MacroCellRect rects[2];
    int count=delta_rects(w,&d,rects);
    if (!count) return ENG_NO_CHANGE;
    uint32_t before_delta=w->delta_version,before_surface=w->surface_revision;
    bool rivers_changed=false;
    bool projected=project_brush(w,&d,rects,count,&rivers_changed);
    qlcg_eng_append_delta(w,d,projected?QLCG_DELTA_PROJECTED:QLCG_DELTA_MACRO);
    w->delta_version++;
    if (projected) qlcg_world_rebuild_atlas_region(w,rects,count,
                    rivers_changed?MACRO_RIVERS_WATER:MACRO_RIVERS_UNCHANGED);
    double pad=(double)radius+(projected?(double)w->circumference/w->columns:0);
    qlcg_world_macro_surface_dirty(w,before_delta,before_surface,wx-pad,wz-pad,wx+pad,wz+pad);
    return ENG_APPLIED;
}

EngResult qlcg_eng_macro_river(MacroWorld *w, float x0, float z0, float x1, float z1, float width_m) {
    if (!w || !isfinite(x0) || !isfinite(z0) || !isfinite(x1) || !isfinite(z1) ||
        !isfinite(width_m) || width_m<=0) return ENG_NO_CHANGE;
    float dx=qlcg_macro_delta_x(w,x1,x0);
    if (dx==0 && z0==z1) return ENG_NO_CHANGE;
    if (!qlcg_eng_has_delta_capacity(w,1) || w->river_count>=w->river_cap) return ENG_CAPACITY;
    uint32_t before=w->delta_version,before_surface=w->surface_revision;
    SparseDeltaRecord d={ENG_TOOL_RIVER_DIVERT,x0,z0,x1,z1,width_m*2,-24,width_m};
    MacroCellRect rects[2];
    int count=delta_rects(w,&d,rects);
    if (!count) return ENG_NO_CHANGE;
    bool rivers_changed=false;
    bool projected=project_river(w,&d,rects,count,&rivers_changed);
    qlcg_eng_append_delta(w,d,projected?QLCG_DELTA_PROJECTED:QLCG_DELTA_MACRO);
    w->delta_version++;
    if (projected) qlcg_world_rebuild_atlas_region(w,rects,count,
                    rivers_changed?MACRO_RIVERS_FLOW:MACRO_RIVERS_UNCHANGED);
    if (!rivers_changed) add_atlas_river(w,&d);
    double ex=(double)x0+dx;
    double r=projected?width_m*2.4+(double)w->circumference/w->columns:width_m*1.4;
    qlcg_world_macro_surface_dirty(w,before,before_surface,
                                   fmin(x0,ex)-r,fmin(z0,z1)-r,fmax(x0,ex)+r,fmax(z0,z1)+r);
    return ENG_APPLIED;
}
