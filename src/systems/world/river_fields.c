#include <string.h>
#include "river_fields.h"

void qlcg_river_RiverJunction(RiverBuffer *b, RiverJunction *v) {
    qlcg_river_values(b,&v->first,1,4);
    qlcg_river_values(b,&v->second,1,4);
    qlcg_river_values(b,&v->mirror,1,4);
    qlcg_river_values(b,&v->seed,1,4);
    qlcg_river_values(b,&v->east,1,8);
    qlcg_river_values(b,&v->south,1,8);
    qlcg_river_values(b,&v->radius,1,8);
    qlcg_river_values(b,&v->level,1,8);
    qlcg_river_values(b,&v->depth,1,8);
    qlcg_river_values(b,&v->width,1,8);
    qlcg_river_values(b,&v->blend,1,8);
}

void qlcg_river_SourceMaterial(RiverBuffer *b, SourceMaterial *v) {
    qlcg_river_values(b,&v->across,1,8);
    qlcg_river_values(b,&v->depth,1,8);
    qlcg_river_values(b,&v->relief,1,8);
    qlcg_river_values(b,&v->roughness,1,8);
    qlcg_river_values(b,&v->sediment,1,8);
}

void qlcg_river_RiverSource(RiverBuffer *b, RiverSource *v) {
    qlcg_river_values(b,&v->x,1,8);
    qlcg_river_values(b,&v->z,1,8);
    qlcg_river_values(b,&v->level,1,8);
    qlcg_river_values(b,&v->half_width,1,8);
    qlcg_river_values(b,&v->min_half_width,1,8);
    qlcg_river_values(b,&v->depth,1,8);
    qlcg_river_values(b,&v->discharge,1,8);
    qlcg_river_values(b,&v->direction_x,1,8);
    qlcg_river_values(b,&v->direction_z,1,8);
    qlcg_river_values(b,&v->spring_radius,1,8);
    qlcg_river_values(b,&v->surface_level,1,8);
    qlcg_river_values(b,&v->pool_rx,1,8);
    qlcg_river_values(b,&v->pool_rz,1,8);
    qlcg_river_values(b,&v->pool_yaw,1,8);
    qlcg_river_values(b,&v->pool_level,1,8);
    qlcg_river_values(b,&v->pool_depth,1,8);
    qlcg_river_values(b,&v->pool_phase,1,8);
    qlcg_river_values(b,&v->connected,1,4);
    qlcg_river_values(b,&v->snowmelt,1,4);
    qlcg_river_values(b,&v->spring_form,1,4);
    qlcg_river_values(b,&v->spring_style,1,4);
    qlcg_river_values(b,&v->mirror,1,4);
    qlcg_river_values(b,&v->pool,1,4);
    qlcg_river_values(b,&v->damp_only,1,4);
    qlcg_river_values(b,&v->planned,1,4);
    qlcg_river_values(b,&v->plan_kind,1,4);
    qlcg_river_values(b,&v->seed,1,4);
    qlcg_river_values(b,&v->material,1,4);
    qlcg_river_padding(b,4);
    qlcg_river_SourceMaterial(b,&v->finish);
}

void qlcg_river_RiverMouthPoint(RiverBuffer *b, RiverMouthPoint *v) {
    qlcg_river_values(b,&v->wx,1,8);
    qlcg_river_values(b,&v->wz,1,8);
    qlcg_river_values(b,&v->arc,1,8);
    qlcg_river_values(b,&v->level,1,8);
    qlcg_river_values(b,&v->blend,1,8);
    qlcg_river_values(b,&v->half_width,1,8);
    qlcg_river_values(b,&v->kind,1,4);
    qlcg_river_padding(b,4);
}

void qlcg_river_SurfaceFrame(RiverBuffer *b, SurfaceFrame *v) {
    qlcg_river_values(b,&v->lon,1,8);
    qlcg_river_values(b,&v->lat,1,8);
    qlcg_river_values(b,v->anchor,3,8);
    qlcg_river_values(b,v->east,3,8);
    qlcg_river_values(b,v->south,3,8);
    qlcg_river_values(b,&v->radius,1,8);
    qlcg_river_values(b,&v->C,1,8);
    uint8_t flat=v->flat?1:0;
    qlcg_river_values(b,&flat,1,1);
    if(flat>1) b->failed=1;
    v->flat=flat==1;
    qlcg_river_padding(b,7);
}

void qlcg_river_OxbowPlan(RiverBuffer *b, OxbowPlan *v) {
    qlcg_river_SurfaceFrame(b,&v->frame);
    qlcg_river_values(b,&v->x,1,8);
    qlcg_river_values(b,&v->z,1,8);
    qlcg_river_values(b,&v->axis_x,1,8);
    qlcg_river_values(b,&v->axis_z,1,8);
    qlcg_river_values(b,&v->radius,1,8);
    qlcg_river_values(b,&v->aspect,1,8);
    qlcg_river_values(b,&v->half_angle,1,8);
    qlcg_river_values(b,&v->half_width,1,8);
    qlcg_river_values(b,&v->tip,1,8);
    qlcg_river_values(b,&v->extent,1,8);
    qlcg_river_values(b,&v->depth,1,8);
    qlcg_river_values(b,&v->level,1,8);
    qlcg_river_values(b,v->radius_offset,7,4);
    qlcg_river_values(b,v->inner_bank,25,4);
    qlcg_river_values(b,v->outer_bank,25,4);
    qlcg_river_values(b,&v->type,1,4);
    qlcg_river_values(b,&v->variant,1,4);
    qlcg_river_values(b,&v->material,1,4);
    qlcg_river_values(b,&v->downstream_side,1,4);
    qlcg_river_padding(b,4);
}

void qlcg_river_values(RiverBuffer *b, void *data, size_t count, unsigned width) {
    unsigned char *p=data;
    if (width!=1 && width!=2 && width!=4 && width!=8) { b->failed=1; return; }
    for (size_t i=0;i<count && !b->failed;i++) {
        uint64_t bits=0;
        if (!b->reading) {
            if(width==8) memcpy(&bits,p+i*width,8);
            else if(width==4) { uint32_t x; memcpy(&x,p+i*width,4); bits=x; }
            else if(width==2) { uint16_t x; memcpy(&x,p+i*width,2); bits=x; }
            else bits=p[i];
        }
        unsigned char bytes[8]={0};
        for(unsigned j=0;j<width;j++) bytes[j]=(unsigned char)(bits>>(8*j));
        qlcg_river_io(b,bytes,width);
        if(b->reading && !b->failed) {
            bits=0; for(unsigned j=0;j<width;j++) bits|=(uint64_t)bytes[j]<<(8*j);
            if(width==8) memcpy(p+i*width,&bits,8);
            else if(width==4) { uint32_t x=(uint32_t)bits; memcpy(p+i*width,&x,4); }
            else if(width==2) { uint16_t x=(uint16_t)bits; memcpy(p+i*width,&x,2); }
            else p[i]=(unsigned char)bits;
        }
    }
}
void qlcg_river_padding(RiverBuffer *b, size_t count) {
    unsigned char zero[8]={0};
    if(count>sizeof(zero)) { b->failed=1; return; }
    qlcg_river_io(b,zero,count);
}
