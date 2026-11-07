/* 泉眼湿点与湖盆池缘复用现有岩石 */
#include "source_rivers.h"
#include "rock_distribution.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

typedef struct { double along,side,scale; int rock; } SourceRock;
static const SourceRock PATTERNS[6][4]={
    {{-1,-.65,1.4,0},{-.55,1,1.2,3},{.25,-1,1,7},{-1.15,.25,1.3,0}},
    {{-1.5,-.4,1.3,3},{-1.2,.65,1.1,0},{.15,-.75,.8,7},{-.4,.9,1,3}},
    {{-1,-.9,1.2,0},{-.7,.95,1.3,3},{-1.2,0,1,0},{.15,-1,.8,7}},
    {{-1.1,-.4,2.1,2},{-.95,.75,1.6,1},{-.65,0,1.2,0},{.05,-1,.7,7}},
    {{-.8,-.65,1.8,3},{-.9,.65,1.9,1},{-1.5,0,1.5,2},{.3,.65,.7,7}},
    {{-1.1,-.8,1.6,2},{-.9,.75,1.3,0},{-.1,-1.2,.8,7},{.15,1.15,.9,3}}
};
static const uint8_t FORM_ROCKS[SOURCE_FORM_COUNT]={
    0,0,4,3,0,2,1,1,0,0,5,5,3,4,3,1,
    2,1,4,3,2,1,0,3
};
static void add_spring(RegionData *reg,const MacroWorld *w,const SourceRiverNetwork *n,const RiverSource *s) {
    if(!reg->source_effects||reg->spring_count>=4||s->damp_only) return;
    if(!s->connected&&!s->pool) return;
    SurfacePoint g=qlcg_frame_coordinates(&n->frame,s->x,s->z);
    double x=g.x-reg->center_x,z=g.z-reg->center_z;
    x-=floor(x/w->circumference+.5)*w->circumference;
    double half=CHUNK_SIZE*.5;
    if(x< -half||x>=half||z< -half||z>=half) return;
    reg->springs[reg->spring_count++]=(SpringEmitter){(float)x,(float)z,s->seed};
}

static void add_rock(RegionData *reg,const MacroWorld *w,const SourceRiverNetwork *n,
                       double x,double z,double scale,double yaw,int archetype) {
    if(reg->rock_count>=reg->rock_cap) return;
    SurfacePoint g=qlcg_frame_coordinates(&n->frame,x,z);
    double dx=g.x-reg->center_x; dx-=floor(dx/w->circumference+.5)*w->circumference;
    double dz=g.z-reg->center_z,half=(reg->width-1)*reg->step*.5;
    if(dx< -half||dx>=half||dz< -half||dz>=half) return;
    if(qlcg_rock_slope_factor(qlcg_region_slope_at(reg,(float)dx,(float)dz))<.5f) return;
    float h=qlcg_region_height_at(reg,(float)dx,(float)dz); if(!isfinite(h)) return;
    float water=qlcg_region_water_at(reg,(float)dx,(float)dz);
    if(isfinite(water)&&water>-1e8f&&h<water+.05f) return;
    PropInstance *p=reg->rocks+reg->rock_count++; memset(p,0,sizeof(*p));
    p->x=(float)dx; p->y=h-.15f; p->z=(float)dz;
    p->scale_x=p->scale_y=p->scale_z=(float)scale; p->rot_y=(float)yaw;
    p->archetype=(uint8_t)archetype; p->r=.46f; p->g=.49f; p->b=.43f;
    snprintf(p->label,sizeof(p->label),"泉畔湿岩");
}

static void source_rim(RegionData *reg,const MacroWorld *w,const SourceRiverNetwork *n,
                       const RiverSource *s) {
    const SourceForm *form=qlcg_source_form(s->spring_form);
    int pooled=form->pooled&&s->connected;
    double radius=pooled?qlcg_source_pool_radius(s):s->spring_radius;
    if(radius<=0) return;
    int count=pooled?6:4;
    int pattern=FORM_ROCKS[s->spring_form];
    double yaw=atan2(s->direction_z,s->direction_x);
    for(int k=0;k<count;k++) {
        int slot=k&3, ring=k<4?1.12:1.30;
        SourceRock p=PATTERNS[pattern][slot];
        double side=p.side*s->mirror*radius*ring;
        double along=p.along*radius*ring;
        add_rock(reg,w,n,s->x+along*s->direction_x-side*s->direction_z,
                  s->z+along*s->direction_z+side*s->direction_x,
                  p.scale*fmax(.8,fmin(1.8,radius/4)),yaw+k*1.73,p.rock);
    }
}

static void pool_rim(RegionData *reg,const MacroWorld *w,const SourceRiverNetwork *n,
                     const RiverSource *s,int terminal) {
    if(s->damp_only||s->pool_rx<=0) return;
    int count=s->pool_rx>15?5:4;
    for(int k=0;k<count;k++) {
        double a=s->pool_phase+k*2.4,r=(terminal?1.08:1.05)+(k%2)*.15;
        double x=cos(a)*s->pool_rx*r,z=sin(a)*s->pool_rz*r;
        double c=cos(s->pool_yaw),sn=sin(s->pool_yaw);
        double scale=s->material==5?.45+(k%3)*.22:.65+(k%3)*.4;
        if(s->material==4) scale*=.60;
        int rock=s->material==5?7:(k%2?3:0);
        if(s->material==4) rock=7;
        add_rock(reg,w,n,s->x+x*c-z*sn,s->z+x*sn+z*c,scale,a+s->pool_yaw,rock);
    }
}

void qlcg_source_rocks(RegionData *reg,const MacroWorld *w) {
    reg->spring_count=0;
    double half=(reg->width-1)*reg->step*.5; int cells[4],count=0;
    for(int iz=0;iz<2;iz++) for(int ix=0;ix<2;ix++) {
        int cell=qlcg_world_cell_at_wxz(w,reg->center_x+(ix?1:-1)*(half+32),reg->center_z+(iz?1:-1)*(half+32));
        int duplicate=0; for(int k=0;k<count;k++) if(cells[k]==cell) duplicate=1;
        if(!duplicate) cells[count++]=cell;
    }
    for(int i=0;i<count;i++) {
        const SourceRiverNetwork *n=qlcg_source_network(w,cells[i],1); if(!n) continue;
        for(int j=0;j<n->source_count;j++) {
            const RiverSource *s=n->sources+j;
            add_spring(reg,w,n,s);
            if(s->material==3) continue;
            if(s->connected&&!s->snowmelt) source_rim(reg,w,n,s);
            else if(!s->connected&&s->pool) pool_rim(reg,w,n,s,0);
        }
        for(int j=0;j<n->edge_count;j++) {
            const SourceRiverEdge *e=n->edges+j;
            if(e->target_kind!=SOURCE_TARGET_POOL) continue;
            RiverSource pool; qlcg_source_terminal_pool(n,e,&pool);
            pool_rim(reg,w,n,&pool,1);
        }
    }
}
