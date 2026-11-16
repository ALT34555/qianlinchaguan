#include "hydrology_sources.h"
/* 源流字段序列化 */
#include "hydrology_source_fields.h"

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
