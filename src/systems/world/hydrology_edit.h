#ifndef QLCG_HYDROLOGY_EDIT_H
#define QLCG_HYDROLOGY_EDIT_H
#include "hydrology_types.h"
#include "hydrology_channel.h"

struct SourceRiverNetwork;
#include "hydrology_archive.h"

int qlcg_source_detach_prepare(const struct SourceRiverNetwork *network,const unsigned char *detached,RiverBuffer *out);
int qlcg_river_craft_prepare(const MacroWorld *w,RiverEdit *edit);
int qlcg_river_craft_source(MacroWorld *w,const RiverEdit *edit,int remove);
int qlcg_river_craft_read(const MacroWorld *w,const struct SourceRiverNetwork *network,int source,RiverEdit *edit);
int qlcg_river_edit_validate(const RiverVectorPoint *points,int count);
int qlcg_river_segments_cross(const RiverVectorPoint *a,const RiverVectorPoint *b,
                              const RiverVectorPoint *c,const RiverVectorPoint *d);
int qlcg_river_edit_pick(const MacroWorld *w,double wx,double wz,double radius,int kind,RiverEdit *out);
RiverPathPoint qlcg_river_edit_point(const MacroWorld *w,double wx,double wz,double radius);
int qlcg_river_edit_main(MacroWorld *w,const RiverEdit *edit);
int qlcg_river_edit_read_main(const MacroWorld *w,int cell,RiverEdit *edit);
int qlcg_river_edit_source(MacroWorld *w,const RiverEdit *edit,int remove);
#endif
