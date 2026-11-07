/* 编辑仅校验给定节点 */
#include "river_edit.h"
#include "river_archive.h"
#include "surface_internal.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static double cross(double ax,double az,double bx,double bz) { return ax*bz-az*bx; }
int qlcg_river_segments_cross(const RiverVectorPoint *a,const RiverVectorPoint *b,
                              const RiverVectorPoint *c,const RiverVectorPoint *d) {
    if(fmax(a->x,b->x)+1e-6<fmin(c->x,d->x)||fmax(c->x,d->x)+1e-6<fmin(a->x,b->x)||
       fmax(a->z,b->z)+1e-6<fmin(c->z,d->z)||fmax(c->z,d->z)+1e-6<fmin(a->z,b->z)) return 0;
    double u=cross(b->x-a->x,b->z-a->z,c->x-a->x,c->z-a->z);
    double v=cross(b->x-a->x,b->z-a->z,d->x-a->x,d->z-a->z);
    double s=cross(d->x-c->x,d->z-c->z,a->x-c->x,a->z-c->z);
    double t=cross(d->x-c->x,d->z-c->z,b->x-c->x,b->z-c->z);
    return ((u<=1e-6&&v>=-1e-6)||(v<=1e-6&&u>=-1e-6))&&
           ((s<=1e-6&&t>=-1e-6)||(t<=1e-6&&s>=-1e-6));
}
int qlcg_river_edit_validate(const RiverVectorPoint *p,int count) {
    if(!p||count<2||count>SOURCE_POINT_LIMIT) return RIVER_EDIT_INVALID;
    for(int k=0;k<count;k++) {
        if(!isfinite(p[k].x)||!isfinite(p[k].z)||!isfinite(p[k].level)||!isfinite(p[k].width)||p[k].width<=0) return RIVER_EDIT_INVALID;
        if(!k) continue;
        if(hypot(p[k].x-p[k-1].x,p[k].z-p[k-1].z)<.05) return RIVER_EDIT_LOOP;
        if(p[k].level>p[k-1].level+.0001) return RIVER_EDIT_UPHILL;
        if(k>1) {
            double ax=p[k-1].x-p[k-2].x,az=p[k-1].z-p[k-2].z,bx=p[k].x-p[k-1].x,bz=p[k].z-p[k-1].z;
            if(ax*bx+az*bz<0&&fabs(cross(ax,az,bx,bz))<1e-6) return RIVER_EDIT_LOOP;
        }
        for(int j=1;j<k-1;j++) if(qlcg_river_segments_cross(p+k-1,p+k,p+j-1,p+j)) return RIVER_EDIT_LOOP;
    }
    return RIVER_EDIT_OK;
}
static double segment_distance(const SurfaceFrame *f,double wx,double wz,const RiverPathPoint *a,const RiverPathPoint *b,double *t) {
    SurfacePoint p=qlcg_frame_local(f,wx,wz),u=qlcg_frame_local(f,a->wx,a->wz),v=qlcg_frame_local(f,b->wx,b->wz);
    double dx=v.x-u.x,dz=v.z-u.z,len=dx*dx+dz*dz;
    *t=len>0?fmax(0,fmin(1,((p.x-u.x)*dx+(p.z-u.z)*dz)/len)):0;
    return hypot(p.x-u.x-dx*(*t),p.z-u.z-dz*(*t));
}
int qlcg_river_edit_pick(const MacroWorld *w,double wx,double wz,double radius,int kind,RiverEdit *out) {
    int cell=qlcg_world_cell_at_wxz(w,(float)wx,(float)wz),found=0;
    SurfaceFrame frame=qlcg_surface_frame(w,cell); double best=radius;
    if(kind==RIVER_RECORD_MAIN) {
        RiverDetailCandidates list={0}; qlcg_river_detail_candidates(w,cell,&list);
        for(int i=0;i<list.count;i++) {
            RiverPathPoint p[161]; int count=qlcg_river_path(w,list.edges[i],p,161);
            for(int k=1;k<count;k++) {
                double t,d=segment_distance(&frame,wx,wz,p+k-1,p+k,&t);
                if(d>best) continue;
                best=d; found=1; out->kind=kind; out->cell=list.edges[i]; out->source=-1; out->count=count;
                memcpy(out->nodes,p,(size_t)count*sizeof(*p));
            }
        }
    } else {
        const SourceRiverNetwork *n=qlcg_source_network(w,cell,1);
        if(!n) return 0;
        SurfacePoint p=qlcg_frame_local(&n->frame,wx,wz);
        int selected=-1;
        for(int i=0;i<n->source_count;i++) {
            double d=hypot(p.x-n->sources[i].x,p.z-n->sources[i].z);
            if(d<=best) { best=d; selected=i; }
        }
        for(int i=0;i<n->edge_count;i++) {
            double t,d;
            int at=qlcg_river_index_nearest(&n->edges[i].spatial,n->edges[i].points,p.x,p.z,&d,&t);
            if(at>=0&&d<=best) { best=d; selected=n->edges[i].source; }
        }
        if(selected>=0) {
            if(qlcg_river_craft_read(w,n,selected,out)) return 1;
            found=1; out->kind=RIVER_RECORD_SOURCE; out->cell=cell; out->source=selected; out->count=1;
            const RiverSource *s=n->sources+selected; SurfacePoint q=qlcg_frame_coordinates(&n->frame,s->x,s->z);
            out->nodes[0]=(RiverPathPoint){q.x,q.z,0,s->level,s->depth,s->half_width};
            for(int i=0;i<n->edge_count;i++) if(n->edges[i].source==selected) {
                const SourceRiverEdge *e=n->edges+i; out->count=e->count;
                for(int k=0;k<e->count;k++) {
                    const RiverVectorPoint *v=e->points+k;
                    out->nodes[k]=(RiverPathPoint){v->wx,v->wz,v->arc,v->level,v->depth,v->width};
                }
            }
        }
    }
    if(found&&kind==RIVER_RECORD_MAIN) return qlcg_river_edit_read_main(w,out->cell,out);
    return found;
}
RiverPathPoint qlcg_river_edit_point(const MacroWorld *w,double wx,double wz,double radius) {
    RiverPathPoint result={wx,wz,0,qlcg_sample_natural_height(w,(float)wx,(float)wz)-.8,1,4};
    SurfaceWater water={0}; qlcg_sample_water(w,(float)wx,(float)wz,-1,&water,NULL);
    if(water.depth>.02) result.level=water.level;
    int cell=qlcg_world_cell_at_wxz(w,(float)wx,(float)wz); SurfaceFrame frame=qlcg_surface_frame(w,cell);
    RiverEdit *pick=calloc(1,sizeof(*pick)); if(!pick) return result;
    for(int kind=1;kind<=2;kind++) if(qlcg_river_edit_pick(w,wx,wz,radius,kind,pick)) {
        for(int k=1;k<pick->count;k++) {
            const RiverPathPoint *a=pick->nodes+k-1,*b=pick->nodes+k; double t,d=segment_distance(&frame,wx,wz,a,b,&t);
            if(d>radius) continue;
            radius=d; SurfacePoint u=qlcg_frame_local(&frame,a->wx,a->wz),v=qlcg_frame_local(&frame,b->wx,b->wz);
            SurfacePoint q=qlcg_frame_coordinates(&frame,u.x+(v.x-u.x)*t,u.z+(v.z-u.z)*t);
            result=(RiverPathPoint){q.x,q.z,0,a->level+(b->level-a->level)*t,1,4};
        }
    }
    free(pick); return result;
}
