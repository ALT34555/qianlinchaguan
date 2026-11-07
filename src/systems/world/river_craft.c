/* 绘线整理与单向纵剖面 */
#include "river_edit.h"
#include "river_profile.h"
#include "surface_detail.h"
#include "surface_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const MacroWorld *w; SurfaceFrame frame; RiverVectorPoint *points; int count; } CraftGround;
static double bounded(double x,double a,double b) { return fmax(a,fmin(b,x)); }
static void simplify_span(const RiverVectorPoint *p,int a,int b,double tolerance,unsigned char *keep) {
    if(b<=a+1) return;
    double dx=p[b].x-p[a].x,dz=p[b].z-p[a].z,den=fmax(1e-9,dx*dx+dz*dz),best=tolerance; int chosen=-1;
    for(int k=a+1;k<b;k++) {
        double t=bounded(((p[k].x-p[a].x)*dx+(p[k].z-p[a].z)*dz)/den,0,1);
        double distance=hypot(p[k].x-p[a].x-t*dx,p[k].z-p[a].z-t*dz);
        if(distance>best) { best=distance; chosen=k; }
    }
    if(chosen>=0) { keep[chosen]=1; simplify_span(p,a,chosen,tolerance,keep); simplify_span(p,chosen,b,tolerance,keep); }
}
static int smooth_stroke(RiverVectorPoint *p,int count) {
    if(count<3) return count;
    unsigned char keep[SOURCE_POINT_LIMIT]={0}; keep[0]=keep[count-1]=1;
    simplify_span(p,0,count-1,1,keep); int used=0;
    for(int k=0;k<count;k++) if(keep[k]) p[used++]=p[k];
    count=used; if(count*2>SOURCE_POINT_LIMIT) return count;
    RiverVectorPoint *q=malloc((size_t)(count*2)*sizeof(*q)); if(!q) return count;
    used=0; q[used++]=p[0];
    for(int k=1;k<count-1;k++) {
        RiverVectorPoint a=p[k],b=p[k];
        double left=hypot(p[k].x-p[k-1].x,p[k].z-p[k-1].z),right=hypot(p[k+1].x-p[k].x,p[k+1].z-p[k].z);
        double radius=fmin(16,fmin(left,right)*.2);
        a.x+=(p[k-1].x-a.x)*radius/fmax(.01,left); a.z+=(p[k-1].z-a.z)*radius/fmax(.01,left);
        b.x+=(p[k+1].x-b.x)*radius/fmax(.01,right); b.z+=(p[k+1].z-b.z)*radius/fmax(.01,right);
        q[used++]=a; q[used++]=b;
    }
    q[used++]=p[count-1]; memcpy(p,q,(size_t)used*sizeof(*p)); free(q); return used;
}
static RiverGround craft_ground(void *context,double arc) {
    CraftGround *g=context; int lo=0,hi=g->count-1;
    while(hi-lo>1) { int mid=(lo+hi)/2; if(g->points[mid].arc>arc) hi=mid; else lo=mid; }
    RiverVectorPoint *a=g->points+lo,*b=g->points+hi;
    double t=bounded((arc-a->arc)/fmax(.01,b->arc-a->arc),0,1);
    SurfacePoint p=qlcg_frame_coordinates(&g->frame,a->x+(b->x-a->x)*t,a->z+(b->z-a->z)*t);
    return (RiverGround){qlcg_sample_natural_height(g->w,(float)p.x,(float)p.z),NAN,0};
}
static int erase_loops(RiverVectorPoint *p,int count) {
    int used=0;
    for(int k=0;k<count;k++) {
        RiverVectorPoint v=p[k];
        if(used&&hypot(v.x-p[used-1].x,v.z-p[used-1].z)<.1) continue;
        for(int j=1;j<used-1;j++) {
            RiverVectorPoint *a=p+j-1,*b=p+j,*c=p+used-1;
            if(!qlcg_river_segments_cross(a,b,c,&v)) continue;
            double ax=b->x-a->x,az=b->z-a->z,bx=v.x-c->x,bz=v.z-c->z,den=ax*bz-az*bx;
            if(fabs(den)>1e-9) {
                double t=bounded(((c->x-a->x)*bz-(c->z-a->z)*bx)/den,0,1);
                RiverVectorPoint q=v; q.x=a->x+ax*t; q.z=a->z+az*t;
                q.arc=c->arc; p[j]=q; used=j+1;
            } else used=j;
            break;
        }
        while(used>=2) {
            RiverVectorPoint *a=p+used-2,*b=p+used-1;
            double ax=b->x-a->x,az=b->z-a->z,bx=v.x-b->x,bz=v.z-b->z;
            if(ax*bx+az*bz>=0||fabs(ax*bz-az*bx)>.01) break;
            used--;
        }
        if(!used||hypot(v.x-p[used-1].x,v.z-p[used-1].z)>=.1) p[used++]=v;
    }
    return used;
}
int qlcg_river_craft_prepare(const MacroWorld *w,RiverEdit *edit) {
    if(!w||!edit||edit->cell<0||edit->cell>=w->cell_count||edit->count<1||edit->count>SOURCE_POINT_LIMIT) return RIVER_EDIT_INVALID;
    RiverVectorPoint *p=calloc(SOURCE_POINT_LIMIT,sizeof(*p)); if(!p) return RIVER_EDIT_MEMORY;
    SurfaceFrame frame=qlcg_surface_frame(w,edit->cell); int main=edit->kind==RIVER_RECORD_MAIN;
    for(int k=0;k<edit->count;k++) {
        RiverPathPoint *v=edit->nodes+k;
        if(!isfinite(v->wx)||!isfinite(v->wz)||!isfinite(v->level)) { free(p); return RIVER_EDIT_INVALID; }
        SurfacePoint q=qlcg_frame_local(&frame,v->wx,v->wz);
        p[k]=(RiverVectorPoint){v->wx,v->wz,q.x,q.z,v->arc,fmax(.5,v->half_width),0,v->level,fmax(.5,v->depth)};
    }
    RiverVectorPoint first=p[0],last=p[edit->count-1],far=first; double distance=0;
    for(int k=1;k<edit->count;k++) {
        double d=hypot(p[k].x-first.x,p[k].z-first.z); if(d>distance) { distance=d; far=p[k]; }
    }
    int count=erase_loops(p,edit->count);
    int used=0;
    for(int k=0;k<count;k++) if(!used||hypot(p[k].x-p[used-1].x,p[k].z-p[used-1].z)>=.1) p[used++]=p[k];
    count=used;
    if(count<2&&distance>.1) { p[0]=first; p[1]=far; count=2; }
    if(!main&&edit->source<0) count=erase_loops(p,smooth_stroke(p,count));
    for(int k=0;k<count;k++) { SurfacePoint q=qlcg_frame_coordinates(&frame,p[k].x,p[k].z); p[k].wx=q.x; p[k].wz=q.z; }
    if(main) {
        p[0]=first; if(count>1) p[count-1]=last;
        double previous=first.level;
        for(int k=1;k<count;k++) { p[k].level=bounded(p[k].level,last.level,previous); previous=p[k].level; }
    } else if(edit->source>=0) {
        for(int k=1;k<count;k++) p[k].level=fmin(p[k].level,p[k-1].level);
    } else if(count==1) p[0].level=qlcg_sample_natural_height(w,(float)p[0].wx,(float)p[0].wz)-.8;
    else if(count>1) {
        double start=qlcg_sample_natural_height(w,(float)p[0].wx,(float)p[0].wz);
        double end=qlcg_sample_natural_height(w,(float)p[count-1].wx,(float)p[count-1].wz);
        if(edit->source<0&&start<end) {
            for(int k=0;k<count/2;k++) { RiverVectorPoint v=p[k]; p[k]=p[count-1-k]; p[count-1-k]=v; }
        }
        p[0].arc=0;
        for(int k=1;k<count;k++) p[k].arc=p[k-1].arc+hypot(p[k].x-p[k-1].x,p[k].z-p[k-1].z);
        CraftGround context={w,frame,p,count}; RiverProfile profile={0};
        if(!qlcg_river_profile_build(&profile,p[count-1].arc,craft_ground,&context)) { free(p); return RIVER_EDIT_MEMORY; }
        for(int k=0;k<profile.count;k++) {
            double level=profile.levels[k]-.8;
            if(k) level=fmin(level,profile.levels[k-1]-.0008);
            profile.levels[k]=(float)level;
        }
        for(int k=0;k<count;k++) p[k].level=qlcg_river_profile_level(&profile,p[k].arc);
        qlcg_river_profile_free(&profile);
    }
    for(int k=0;k<count;k++) {
        SurfacePoint q=qlcg_frame_coordinates(&frame,p[k].x,p[k].z);
        edit->nodes[k]=(RiverPathPoint){q.x,q.z,p[k].arc,p[k].level,p[k].depth,p[k].width};
    }
    edit->count=count; free(p); return RIVER_EDIT_OK;
}
