/* 泉眼湿点与湖盆池缘复用现有岩石 */
#include "hydrology_sources.h"
#include "rock_distribution.h"
#include "surface_detail.h"
#include "climate_season.h"
#include "core/math_util.h"
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
    double radius=s->connected?qlcg_source_pool_radius(s):s->pool_rx;
    double shift=s->connected?radius*.5:0;
    SurfacePoint pool=qlcg_frame_coordinates(&n->frame,s->x-shift*s->direction_x,s->z-shift*s->direction_z);
    double px=pool.x-reg->center_x;px-=floor(px/w->circumference+.5)*w->circumference;
    const HeadwaterPlan *h=s->planned==2||s->planned==3||s->planned==SOURCE_REGION_SPRING?qlcg_headwater_plan(w,n->cell):NULL;
    QlcgClimateSeason climate;qlcg_climate_season_at(w,n->cell,w->plant_calendar,&climate);
    reg->springs[reg->spring_count++]=(SpringEmitter){(float)x,(float)z,s->seed,
        (float)px,(float)(pool.z-reg->center_z),(float)(s->connected?s->surface_level:s->pool_level),
        (float)radius,qlcg_headwater_activity(h,climate.temperature),
        h&&(h->kind==HEAD_CLIFF||h->kind==HEAD_STEPS||h->kind>=HEAD_SNOW)?1.0f:.3f,
        (uint8_t)(h?h->kind+1:0)};
}

static void add_rock(RegionData *reg,const MacroWorld *w,const SourceRiverNetwork *n,
                       double x,double z,double scale,double yaw,int archetype) {
    if(reg->rock_count>=reg->rock_cap) return;
    SurfacePoint g=qlcg_frame_coordinates(&n->frame,x,z);
    double dx=g.x-reg->center_x; dx-=floor(dx/w->circumference+.5)*w->circumference;
    double dz=g.z-reg->center_z,half=(reg->width-1)*reg->step*.5;
    if(dx< -half||dx>=half||dz< -half||dz>=half) return;
    int landmark=archetype<0;if(landmark) archetype=-archetype-1;
    if(!landmark&&qlcg_rock_slope_factor(qlcg_region_slope_at(reg,(float)dx,(float)dz))<.5f) return;
    float h=qlcg_region_height_at(reg,(float)dx,(float)dz); if(!isfinite(h)) return;
    float water=qlcg_region_water_at(reg,(float)dx,(float)dz);
    if(isfinite(water)&&water>-1e8f&&h<water+.05f&&(!landmark||water-h>scale*.35)) return;
    PropInstance *p=reg->rocks+reg->rock_count++; memset(p,0,sizeof(*p));
    p->x=(float)dx; p->y=h-.15f; p->z=(float)dz;
    p->scale_x=p->scale_y=p->scale_z=(float)scale; p->rot_y=(float)yaw;
    p->archetype=(uint8_t)archetype; p->r=.46f; p->g=.49f; p->b=.43f;
    snprintf(p->label,sizeof(p->label),"泉畔湿岩");
}

static void landmark_rocks(RegionData *reg,const MacroWorld *w,const SourceRiverNetwork *n,const HeadwaterPlan *h) {
    SurfacePoint center=qlcg_frame_local(&n->frame,reg->center_x,reg->center_z);
    if(hypot(center.x-h->x,center.z-h->z)>h->radius+40) return;
    int major=h->kind==HEAD_SAND||h->kind==HEAD_MARSH?3:12;
    for(int k=0;k<major+24;k++) {
        uint32_t seed=qlcg_hash_u32(h->seed^0x726f636bu^((k+1)*0x9e3779b9u));
        double a=(seed&65535)*6.283185307/65536,u=(seed>>16)/65536.0;
        double along,side,scale;
        if(k<major) {
            along=-(.2+.55*u)*h->pool_radius;
            side=(k&1?1:-1)*(h->pool_radius*.75+u*h->radius*.18);
            scale=h->kind==HEAD_CLIFF||h->kind>=HEAD_SNOW?3+u*6:1.2+u*3;
            if(k<2) { along=-h->pool_radius*1.4;side=(k?1:-1)*h->pool_radius*.25; }
        } else {
            along=cos(a)*h->pool_radius*1.3-h->pool_radius*.5;side=sin(a)*h->pool_radius*1.5;
            scale=.4+u*1.1;
        }
        if(along>0&&fabs(side)<8+scale) continue;
        if(h->kind==HEAD_SAND||h->kind==HEAD_MARSH) scale*=.4;
        add_rock(reg,w,n,h->x+along*h->dx-side*h->dz,h->z+along*h->dz+side*h->dx,
            scale,a,-1-(k<major?(k%4):7));
    }
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
        const HeadwaterPlan *head=qlcg_headwater_plan(w,cells[i]);
        if(head&&head->design!=2) landmark_rocks(reg,w,n,head);
        for(int j=0;j<n->source_count;j++) {
            const RiverSource *s=n->sources+j;
            if(s->planned>=SOURCE_REGION_CHANNEL&&s->planned!=SOURCE_REGION_SPRING)continue;
            add_spring(reg,w,n,s);
            if(s->planned==2) continue;
            if(s->material==3) continue;
            if(s->connected) source_rim(reg,w,n,s);
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
