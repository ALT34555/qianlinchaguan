#ifndef QLCG_RIVER_EDIT_H
#define QLCG_RIVER_EDIT_H
#include "source_rivers.h"
#include "river_archive.h"

typedef struct {
    int kind,cell,source,count;
    RiverPathPoint nodes[SOURCE_POINT_LIMIT];
} RiverEdit;
enum { RIVER_EDIT_OK, RIVER_EDIT_INVALID, RIVER_EDIT_UPHILL, RIVER_EDIT_LOOP,
       RIVER_EDIT_OUTSIDE, RIVER_EDIT_TARGET, RIVER_EDIT_ATTACHED, RIVER_EDIT_MEMORY, RIVER_EDIT_ENDPOINT };
int qlcg_source_detach_prepare(const SourceRiverNetwork *network,const unsigned char *detached,RiverBuffer *out);
int qlcg_river_craft_prepare(const MacroWorld *w,RiverEdit *edit);
int qlcg_river_craft_source(MacroWorld *w,const RiverEdit *edit,int remove);
int qlcg_river_craft_read(const MacroWorld *w,const SourceRiverNetwork *network,int source,RiverEdit *edit);
int qlcg_river_edit_validate(const RiverVectorPoint *points,int count);
int qlcg_river_segments_cross(const RiverVectorPoint *a,const RiverVectorPoint *b,
                              const RiverVectorPoint *c,const RiverVectorPoint *d);
int qlcg_river_edit_pick(const MacroWorld *w,double wx,double wz,double radius,int kind,RiverEdit *out);
RiverPathPoint qlcg_river_edit_point(const MacroWorld *w,double wx,double wz,double radius);
int qlcg_river_edit_main(MacroWorld *w,const RiverEdit *edit);
int qlcg_river_edit_read_main(const MacroWorld *w,int cell,RiverEdit *edit);
int qlcg_river_edit_source(MacroWorld *w,const RiverEdit *edit,int remove);
#endif
