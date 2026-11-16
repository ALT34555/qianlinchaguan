#include "../../core/math_util.h"
/* 共用断面与编辑几何约束 */
#include "hydrology_channel.h"
#include <math.h>


int qlcg_hydrology_section(double distance,double radius,double depth,double natural,double level,
                         double *height,double *water) {
    return qlcg_hydrology_section_span(distance,radius,depth,natural,level,512,height,water);
}
int qlcg_hydrology_section_span(double distance,double radius,double depth,double natural,double level,
                              double span,double *height,double *water) {
    double valley=fmin(span,fmax(32,fmax(radius*4+20,fabs(natural-level)*2.5+radius*2+24)));
    if(distance>=valley||radius<=0) return 0;
    double bed=level-fmin(depth,fmin(radius*2*.22,2.5)),core=distance/radius,lip=fmin(level+.12,natural+.4);
    *height=core<1?qlcg_mixd(bed,lip,qlcg_smooth01d(core)):
        fmin(natural+.4,qlcg_mixd(lip,natural,qlcg_smooth01d((distance-radius)/fmax(1,valley-radius))));
    *water=level;
    return core<1&&*height<level-.025;
}

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
    if(!p||count<2||count>HYDROLOGY_PATH_POINTS) return RIVER_EDIT_INVALID;
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
