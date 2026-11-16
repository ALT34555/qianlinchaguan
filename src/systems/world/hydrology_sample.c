#include "hydrology_oxbow.h"
/* 湖河源流的单点协调 */
#include "hydrology_sample.h"
#include "hydrology_river.h"
#include "hydrology_sources.h"
#include <string.h>

void qlcg_hydrology_sample(const MacroWorld *w,const int corners[4],float wx,float wz,
                           float natural, const LakeShoreField *lf, HydrologySample *nw, int source_mode) {
    memset(nw,0,sizeof(*nw));
    RiverSurfaceSample river={0};
    river.height=natural; river.level=-1e9f;
    int cell=qlcg_world_cell_at_wxz(w,wx,wz);
    bool sea=w->water[cell]==1;
    for (int k=0;k<4;k++) sea=sea||w->water[corners[k]]==1;
    if (natural<0 && sea) { river.level=0; river.kind=WATER_KIND_SEA; }
    /* 湖体内按实际湖床铺水 */
    if (river.kind!=WATER_KIND_SEA && lf && lf->lake &&
        lf->d < 0.0f && lf->level > natural + 0.02f) {
        river.level=lf->level; river.kind=WATER_KIND_LAKE;
    }
    const SourceRiverNetwork *sources=NULL;
    if(source_mode>=0&&river.kind==WATER_KIND_DRY)
        sources=qlcg_source_network(w,cell,source_mode);
    HydrologyWidthQuery widths=qlcg_source_width_query(sources);
    qlcg_river_surface_with_widths(w,wx,wz,natural,&widths,&river);
    qlcg_oxbow_surface(w,wx,wz,&river);
    if(!river.fresh) qlcg_source_surface_network(w,sources,wx,wz,natural,&river);
    nw->shore_level=river.has_bank_level?river.bank_level:
        (sea?0.0f:(lf&&lf->lake?lf->level:-1e9f));
    nw->level=river.level; nw->cut=natural-river.height;
    nw->ve=river.east; nw->vn=river.north;
    nw->discharge=river.discharge; nw->width_m=river.width;
    nw->grade=river.grade; nw->kind=(uint8_t)river.kind;
    nw->bank_distance=river.bank_distance; nw->lateral=river.lateral;
    nw->decorative=river.decorative; nw->damp=river.damp; nw->sediment=river.sediment;
    nw->fresh=(uint8_t)river.fresh;
    nw->source_effects=river.source_effects;
    nw->channel_depth=river.channel_depth; nw->waterfall_rock=river.waterfall_rock;
}

void qlcg_source_surface_mode(const MacroWorld *w,float wx,float wz,float natural,
                              RiverSurfaceSample *out,int build) {
    if(out->kind==WATER_KIND_SEA||out->kind==WATER_KIND_LAKE) return;
    int cell=qlcg_world_cell_at_wxz(w,wx,wz);
    qlcg_source_surface_network(w,qlcg_source_network(w,cell,build),wx,wz,natural,out);
}

void qlcg_source_surface(const MacroWorld *w,float wx,float wz,float natural,RiverSurfaceSample *out) {
    qlcg_source_surface_mode(w,wx,wz,natural,out,1);
}
