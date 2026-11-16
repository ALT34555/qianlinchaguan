/* 主河编辑准备与原子事务 */
#include "hydrology_river_internal.h"
#include "hydrology_edit.h"
#include "hydrology_sources.h"
#include <stdlib.h>
#include <string.h>

typedef struct { RiverCurve *before,*after; } HydrologyMainEdit;

int qlcg_river_edit_read_main(const MacroWorld *w,int cell,RiverEdit *edit) {
    RiverCurve *c=qlcg_river_curve_at(w,cell); if(!c||!qlcg_river_ensure_profile(w,c)) return 0;
    edit->kind=RIVER_RECORD_MAIN; edit->cell=cell; edit->source=-1; edit->count=c->count;
    double previous=qlcg_river_profile_level(&c->profile,0);
    double bottom=qlcg_river_profile_level(&c->profile,c->profile.arc);
    for(int k=0;k<c->count;k++) {
        const CurvePoint *p=c->points+k;
        double parameter=qlcg_river_curve_profile_arc(c,p->arc);
        previous=qlcg_clampd(qlcg_river_profile_level(&c->profile,parameter),bottom,previous);
        edit->nodes[k]=(RiverPathPoint){p->wx,p->wz,parameter,previous,qlcg_river_curve_depth(c,p->arc),p->width};
    }
    return 1;
}

typedef struct { GroundContext ground; double begin; } EditGroundContext;
static RiverGround edit_ground_at(void *context,double arc) {
    EditGroundContext *g=context;
    return qlcg_river_curve_ground(&g->ground,qlcg_river_curve_geometry_arc(g->ground.c,g->begin+arc));
}

static int copy_profile(RiverProfile *out,const RiverProfile *in) {
    *out=*in; out->levels=NULL;
    if(!in->count) return 1;
    out->levels=malloc((size_t)in->count*sizeof(float)); if(!out->levels) return 0;
    memcpy(out->levels,in->levels,(size_t)in->count*sizeof(float)); return 1;
}

static int edit_profiles(const MacroWorld *w,RiverCurve *c,const RiverCurve *old,const RiverEdit *edit) {
    if(!copy_profile(&c->profile,&old->profile)||!copy_profile(&c->ground_profile,&old->ground_profile)) return 0;
    unsigned char changed[RIVER_CURVE_POINTS]={0}; c->edit_mode=RIVER_CURVE_LOCAL_EDIT;
    for(int k=0;k<c->count;k++) {
        c->parameters[k]=edit->nodes[k].arc; c->saved_curvature[k]=c->points[k].curvature; c->edited_segments[k]=0;
        double before=qlcg_river_curve_geometry_arc(old,c->parameters[k]); Point p=qlcg_river_curve_position(old,before);
        if(hypot(p.x-c->points[k].x,p.z-c->points[k].z)<.001) {
            int hi=qlcg_river_curve_arc_end(old,before),lo=hi?hi-1:0;
            double t=qlcg_clampd((before-old->points[lo].arc)/fmax(1e-9,old->points[hi].arc-old->points[lo].arc),0,1);
            c->points[k].curvature=c->saved_curvature[k]=qlcg_mixd(old->points[lo].curvature,old->points[hi].curvature,t);
        } else { changed[k]=1; if(k+1<c->count) changed[k+1]=1; }
        if(k) {
            double a=qlcg_river_curve_geometry_arc(old,c->parameters[k-1]),b=before;
            if(fabs((b-a)-(c->points[k].arc-c->points[k-1].arc))>.001) changed[k]=1;
            for(int j=qlcg_river_curve_arc_end(old,a);j<=qlcg_river_curve_arc_end(old,b)&&j<old->count;j++)
                if(old->edit_mode==RIVER_CURVE_LEGACY||(old->edit_mode==RIVER_CURVE_LOCAL_EDIT&&old->edited_segments[j])) c->edited_segments[k]=1;
        }
    }
    for(int k=1;k<c->count;k++) if(changed[k]) c->edited_segments[k]=1;
    for(int first=1;first<c->count;first++) {
        if(!changed[first]) continue;
        int last=first; while(last+1<c->count&&changed[last+1]) last++;
        double begin=c->parameters[first-1],end=c->parameters[last];
        EditGroundContext context={{w,c},begin}; RiverProfile ground={0};
        if(!qlcg_river_profile_build(&ground,end-begin,edit_ground_at,&context)) return 0;
        double top=qlcg_river_curve_level(old,qlcg_river_curve_geometry_arc(old,begin)),bottom=qlcg_river_curve_level(old,qlcg_river_curve_geometry_arc(old,end)),previous=top;
        for(int j=(int)ceil(begin/8);j<c->profile.count&&j*8.0<=end;j++) {
            double at=j*8.0,g=qlcg_river_profile_level(&ground,at-begin),depth=fmin(3,fmax(.6,qlcg_river_curve_depth(c,qlcg_river_curve_geometry_arc(c,at))*.5));
            double water=top>=bottom?qlcg_clampd(g-depth,bottom,previous):qlcg_river_curve_level(old,qlcg_river_curve_geometry_arc(old,at));
            c->profile.levels[j]=(float)water; previous=water;
            if(c->ground_profile.count) c->ground_profile.levels[j]=(float)g;
        }
        qlcg_river_profile_free(&ground); first=last;
    }
    if(isfinite(c->inlet.arc)) c->inlet.arc=qlcg_river_curve_geometry_arc(c,qlcg_river_curve_profile_arc(old,old->inlet.arc));
    if(isfinite(c->outlet.arc)) c->outlet.arc=qlcg_river_curve_geometry_arc(c,qlcg_river_curve_profile_arc(old,old->outlet.arc));
    return 1;
}

static int prepare_main_edit(MacroWorld *w,const RiverEdit *edit,HydrologyMainEdit *transaction,RiverBuffer *record) {
    if(!w||!edit||edit->cell<0||edit->cell>=w->cell_count||edit->count<2||edit->count>RIVER_CURVE_POINTS) return RIVER_EDIT_INVALID;
    RiverCurve *old=qlcg_river_curve_at(w,edit->cell); if(!old||!qlcg_river_ensure_profile(w,old)) return RIVER_EDIT_INVALID;
    int unchanged=edit->count==old->count;
    double expected=qlcg_river_profile_level(&old->profile,0),bottom=qlcg_river_profile_level(&old->profile,old->profile.arc);
    for(int k=0;unchanged&&k<edit->count;k++) {
        const RiverPathPoint *p=edit->nodes+k; const CurvePoint *q=old->points+k;
        expected=qlcg_clampd(qlcg_river_profile_level(&old->profile,qlcg_river_curve_profile_arc(old,q->arc)),bottom,expected);
        if(p->wx!=q->wx||p->wz!=q->wz||p->half_width!=q->width||fabs(p->level-expected)>1e-7) unchanged=0;
    }
    if(unchanged) return RIVER_EDIT_OK;
    RiverCurve *c=malloc(sizeof(*c)); if(!c) return RIVER_EDIT_MEMORY;
    *c=*old; c->waterfall=(MacroWaterfall){.state=1}; c->spatial=(RiverVectorIndex){0}; c->profile=(RiverProfile){0}; c->ground_profile=(RiverProfile){0}; c->count=edit->count;
    c->edit_mode=RIVER_CURVE_NATURAL;
    int status=RIVER_EDIT_OK;
    for(int k=0;k<c->count;k++) {
        RiverPathPoint p=edit->nodes[k];
        if(!isfinite(p.wx)||!isfinite(p.wz)||!isfinite(p.level)||!isfinite(p.half_width)) { qlcg_river_curve_free(c); return RIVER_EDIT_INVALID; }
        SurfacePoint q=qlcg_frame_local(&c->frame,p.wx,p.wz);
        int cell=qlcg_world_cell_at_wxz(w,(float)p.wx,(float)p.wz);
        if(cell!=c->cell&&cell!=c->receiver) {
            int nearby=0; for(int j=0;j<8;j++) if(w->neighbors[c->cell*8+j]==cell) nearby=1;
            if(!nearby) status=RIVER_EDIT_OUTSIDE;
        }
        if(p.half_width<=0||p.half_width>qlcg_river_width_limit(w,p.wz)*.5) status=RIVER_EDIT_INVALID;
        c->points[k]=(CurvePoint){p.wx,p.wz,q.x,q.z,0,p.half_width,0,p.level,p.depth};
    }
    for(int i=0;i<2;i++) {
        const CurvePoint *a=c->points+(i?c->count-1:0),*b=old->points+(i?old->count-1:0);
        double level=qlcg_river_profile_level(&old->profile,qlcg_river_curve_profile_arc(old,b->arc));
        if(!i) level=fmax(level,qlcg_river_profile_level(&old->profile,old->profile.arc));
        if(hypot(a->x-b->x,a->z-b->z)>.01||fabs(a->level-level)>.002) status=RIVER_EDIT_ENDPOINT;
    }
    if(!status) status=qlcg_river_edit_validate(c->points,c->count);
    old->profile_building=1;
    RiverDetailCandidates candidates={0};
    if(!status&&!qlcg_river_detail_candidates(w,c->cell,&candidates)) status=RIVER_EDIT_MEMORY;
    for(int i=0;!status&&i<candidates.count;i++) {
        int id=candidates.edges[i]; if(id==c->cell) continue;
        RiverPathPoint path[RIVER_CURVE_POINTS]; int count=qlcg_river_path(w,id,path,RIVER_CURVE_POINTS); CurvePoint p[RIVER_CURVE_POINTS];
        for(int j=0;j<count;j++) { SurfacePoint q=qlcg_frame_local(&c->frame,path[j].wx,path[j].wz); p[j]=(CurvePoint){0}; p[j].x=q.x; p[j].z=q.z; }
        for(int k=1;!status&&k<c->count;k++) for(int j=1;j<count;j++) {
            if(!qlcg_river_segments_cross(c->points+k-1,c->points+k,p+j-1,p+j)) continue;
            int joined=(k==1&&(hypot(c->points[0].x-p[j].x,c->points[0].z-p[j].z)<.05||hypot(c->points[0].x-p[j-1].x,c->points[0].z-p[j-1].z)<.05))||
                (k==c->count-1&&(hypot(c->points[k].x-p[j].x,c->points[k].z-p[j].z)<.05||hypot(c->points[k].x-p[j-1].x,c->points[k].z-p[j-1].z)<.05));
            if(!joined) { status=RIVER_EDIT_LOOP; break; }
        }
    }
    if(!status&&!qlcg_river_curve_finish(w,c,1)) status=RIVER_EDIT_MEMORY;
    if(!status) {
        RiverCurve *previous=qlcg_river_preview_swap(w,c);
        if(!edit_profiles(w,c,old,edit)) status=RIVER_EDIT_MEMORY;
        qlcg_river_preview_swap(w,previous);
    }
    if(!status&&!qlcg_river_curve_record(w,c,record)) status=RIVER_EDIT_MEMORY;
    if(status) { old->profile_building=0; qlcg_river_curve_free(c); return status; }
    transaction->before=old; transaction->after=c;
    return RIVER_EDIT_OK;
}

static void finish_main_edit(MacroWorld *w,HydrologyMainEdit *transaction,int status) {
    RiverCurve *old=transaction->before;
    RiverCurve *candidate=transaction->after;
    old->profile_building=0; qlcg_river_curve_free(candidate);
    if(!status) w->surface_revision++;
    memset(transaction,0,sizeof(*transaction));
}

int qlcg_river_edit_main(MacroWorld *w,const RiverEdit *edit) {
    HydrologyMainEdit transaction={0};
    RiverArchiveUpdate updates[19]={0}; int update_count=1,visited[18],visited_count=0;
    int status=prepare_main_edit(w,edit,&transaction,&updates[0].buffer);
    if(!transaction.after) { qlcg_river_buffer_free(&updates[0].buffer); return status; }
    updates[0].kind=RIVER_RECORD_MAIN; updates[0].cell=transaction.after->cell;
    for(int side=0;!status&&side<2;side++) for(int j=-1;!status&&j<8;j++) {
        int cell=side?transaction.after->receiver:transaction.after->cell; if(j>=0) cell=w->neighbors[cell*8+j];
        int seen=0; for(int k=0;k<visited_count;k++) if(visited[k]==cell) seen=1;
        if(seen) continue; visited[visited_count++]=cell;
        const SourceRiverNetwork *n=qlcg_source_network(w,cell,0); if(!n) continue;
        unsigned char detached[4096]={0}; int detach_count=0;
        for(int e=0;e<n->edge_count;e++) if(n->edges[e].target_kind==0&&n->edges[e].target==transaction.after->cell) {
            const RiverVectorPoint *end=n->edges[e].points+n->edges[e].count-1;
            SurfacePoint q=qlcg_frame_local(&transaction.after->frame,end->wx,end->wz); double distance,t,previous=0,u;
            int near=qlcg_river_index_nearest(&transaction.after->spatial,transaction.after->points,q.x,q.z,&distance,&t);
            int before=qlcg_river_index_nearest(&transaction.before->spatial,transaction.before->points,q.x,q.z,&previous,&u);
            if(near<0) { detached[e]=1; detach_count++; continue; }
            double level=qlcg_river_curve_level(transaction.after,qlcg_mixd(transaction.after->points[near].arc,transaction.after->points[near+1].arc,t));
            double was=before<0?level:qlcg_river_curve_level(transaction.before,qlcg_mixd(transaction.before->points[before].arc,transaction.before->points[before+1].arc,u));
            if((distance>qlcg_mixd(transaction.after->points[near].width,transaction.after->points[near+1].width,t)+.05&&distance>previous+.05)||
               (level>was+.02&&level>end->level+.02)) { detached[e]=1; detach_count++; }
        }
        if(detach_count) {
            RiverArchiveUpdate *update=updates+update_count++;
            update->kind=RIVER_RECORD_SOURCE; update->cell=cell;
            if(!qlcg_source_detach_prepare(n,detached,&update->buffer)) status=RIVER_EDIT_MEMORY;
        }
    }
    if(!status&&!qlcg_river_archive_commit(w,updates,update_count)) status=RIVER_EDIT_MEMORY;
    for(int k=0;k<update_count;k++) qlcg_river_buffer_free(&updates[k].buffer);
    finish_main_edit(w,&transaction,status);
    return status;
}
