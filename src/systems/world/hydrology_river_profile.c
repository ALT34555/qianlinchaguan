/* 主河水位与地形纵剖面 */
#include "hydrology_river_internal.h"
#include "surface_internal.h"
#include "surface_detail.h"
#include <stdlib.h>

RiverGround qlcg_river_curve_ground(void *context,double arc) {
    GroundContext *g=context; Point p=qlcg_river_curve_position(g->c,arc),q=qlcg_frame_coordinates(&g->c->frame,p.x,p.z);
    double h=qlcg_river_archive_terrain(g->w)>=5?qlcg_natural_base_height(g->w,q.x,q.z):
        qlcg_sample_natural_height(g->w,q.x,q.z);
    int i=qlcg_world_cell_at_wxz(g->w,(float)q.x,(float)q.z);
    int sea=g->w->water[i]==1 || (g->c->sea_mouth && h<=0);
    double lake=NAN;
    if (g->w->water[i]==2 && g->w->lake_id && g->w->lake_id[i]>=0 && g->w->lake_id[i]<g->w->lake_count)
        lake=g->w->lakes[g->w->lake_id[i]].level;
    return (RiverGround){sea?fmax(0,h):h,lake,sea};
}

static int ensure_ground(const MacroWorld *w,RiverCurve *c) {
    if(c->edit_mode==RIVER_CURVE_LEGACY) return 1;
    if(c->ground_profile.count) return 1;
    if(c->profile_building) return 0;
    GroundContext context={w,c};c->profile_building=1;
    int ok=qlcg_river_profile_build(&c->ground_profile,c->profile.arc,qlcg_river_curve_ground,&context);
    c->profile_building=0;
    if(ok) ok=qlcg_river_curve_store(w,c);
    if(!ok) qlcg_river_profile_free(&c->ground_profile);
    return ok;
}

double qlcg_river_head_receiver_level(const MacroWorld *w,const RiverCurve *c,double arc) {
    if(c->fixed_water&&!c->edit_mode)return qlcg_river_curve_level(c,arc);
    Point p=qlcg_river_curve_position(c,arc),world=qlcg_frame_coordinates(&c->frame,p.x,p.z);
    return fmin(qlcg_river_curve_level(c,arc),qlcg_natural_base_height(w,(float)world.x,(float)world.z)+.15);
}

static int head_profile(const MacroWorld *w,RiverCurve *c) {
    if(!ensure_ground(w,c)) return 0;
    if(qlcg_river_archive_terrain(w)>=9&&!c->edit_mode&&!c->fixed_water) {
        double previous=INFINITY;
        for(int k=0;k<c->profile.count;k++) {
            c->profile.levels[k]=(float)fmin(previous,c->profile.levels[k]);previous=c->profile.levels[k];
        }
        c->fixed_water=1;
    }
    if(!c->head_run&&!c->edit_mode) {
        const HeadwaterPlan *p=qlcg_headwater_plan(w,c->cell);
        if(p&&p->design==3&&!p->shape.geometry_ready)return 1;
        if(p&&p->run>0) {
            double run=fmin(p->run,c->points[c->count-1].arc*.15);
            c->head_end=qlcg_river_head_receiver_level(w,c,run);
            c->head_level=p->outlet_level;c->head_run=run;
        }
    }
    return 1;
}

double qlcg_river_crossfall_level(const MacroWorld *w,RiverCurve *c,Point point,Point direction,
                              double arc,double radius,double offset,double natural,double level,
                              double depth,double blend,double *grade) {
    if(!c->ground_profile.count) return level;
    double reach=qlcg_clampd(radius*1.1,96,384);
    double a=qlcg_river_profile_level(&c->ground_profile,qlcg_river_curve_profile_arc(c,arc-reach));
    double b=qlcg_river_profile_level(&c->ground_profile,qlcg_river_curve_profile_arc(c,arc+reach));
    double gate=qlcg_smooth01d((a-b-20)/24);
    if(gate<=0) return level;
    double high=qlcg_river_curve_level(c,arc-reach),low=qlcg_river_curve_level(c,arc+reach);
    if(high<=low+20) return level;
    double center=qlcg_river_profile_level(&c->ground_profile,qlcg_river_curve_profile_arc(c,arc));
    double freeboard=qlcg_clampd(qlcg_river_curve_level(c,arc)-center,-3,.15),target=qlcg_clampd(natural+freeboard,low,high);
    level=qlcg_mixd(level,fmax(level,target),gate);
    *grade=fmax(*grade,gate*qlcg_smooth01d((level-low)/4)*qlcg_smooth01d((high-level)/4));
    if(qlcg_river_archive_terrain(w)>=3 && !qlcg_river_curve_is_legacy(c)) {
        double length=c->points[c->count-1].arc;
        double fade=qlcg_smooth01d(arc/128)*qlcg_smooth01d((length-arc)/128)*blend;
        double recess=gate*fade*qlcg_macro_fall_recess(w->opt.seed,c->cell,qlcg_river_curve_profile_arc(c,arc),offset,radius);
        if(recess>.01) {
            Point sample=qlcg_frame_coordinates(&c->frame,point.x+direction.x*recess,point.z+direction.z*recess);
            int building=c->profile_building;c->profile_building=1;
            double eroded=qlcg_natural_detail_height(w,sample.x,sample.z);
            c->profile_building=building;
            double limit=fmax(20,fmin(32,depth*6));
            double next=arc+recess,water=qlcg_river_curve_level(c,next);
            double ground=qlcg_river_profile_level(&c->ground_profile,qlcg_river_curve_profile_arc(c,next));
            double fitted=qlcg_clampd(eroded+qlcg_clampd(water-ground,-3,.15),low,high);
            water=fmin(water,eroded+.15);
            water=qlcg_mixd(water,fmax(water,fitted),gate);
            level=fmin(level,fmax(fmax(low,natural-limit+depth),water));
        }
    }
    return level;
}

int qlcg_river_ensure_profile(const MacroWorld *w,RiverCurve *c) {
    if (c->profile_ready) return head_profile(w,c);
    if (c->profile_building) return 0;
    double length=c->points[c->count-1].arc; GroundContext ctx={w,c};
    double start=w->river_level?w->river_level[c->cell]:NAN;
    double end=w->river_level?w->river_level[c->receiver]:NAN;
    if (isfinite(c->outlet.arc)) start=c->outlet.level;
    if (c->inlet.kind) end=c->inlet.level;
    int bound=isfinite(start);
    if (bound && !isfinite(end)) {
        int r=c->receiver;
        end=w->water[r]==1?0:w->water[r]==2?w->level[r]:w->spill[r];
    }
    if (bound) end=fmin(start,end);
    c->profile_building=1;
    int built;
    if (bound) {
        c->profile.count=(int)fmax(2,ceil(length/8)+1); c->profile.arc=length;
        c->profile.levels=malloc((size_t)c->profile.count*sizeof(float));
        c->profile.stations=2; built=c->profile.levels!=NULL;
    } else built=qlcg_river_profile_build(&c->profile,length,qlcg_river_curve_ground,&ctx);
    c->profile_building=0;
    if (!built) return 0;
    double previous=INFINITY;
    for (int i=0;i<c->profile.count;i++) {
        double arc=i*8.0;
        if (bound) {
            double finish=isfinite(c->inlet.arc)?fmax(1,c->inlet.arc-c->inlet.blend*.25):length;
            double begin=isfinite(c->outlet.arc)?c->outlet.arc:0;
            if (finish<=begin && isfinite(c->inlet.arc)) {
                begin=isfinite(c->outlet.arc)?c->outlet.arc:0; finish=c->inlet.arc;
            }
            c->profile.levels[i]=(float)qlcg_mixd(start,end,qlcg_smooth01d((arc-begin)/fmax(1,finish-begin)));
            continue;
        }
        double raw=c->profile.levels[i],fb=fmin(1.2+qlcg_river_curve_depth(c,arc)*.35,3.2);
        double level=c->sea_mouth?fmax(0,raw-fb*(1-qlcg_clampd((arc/fmax(1,length)-.55)/.45,0,1))):
            raw>=0?fmax(0,raw-fb):raw-fb;
        if (isfinite(end)) {
            level=fmin(previous,fmax(end,level));
            double finish=isfinite(c->inlet.arc)?fmax(0,c->inlet.arc-c->inlet.blend*.25):length;
            double begin=isfinite(c->inlet.arc)?fmax(0,finish-c->inlet.blend):length*.75;
            level=qlcg_mixd(level,end,qlcg_smooth01d((arc-begin)/fmax(1,finish-begin)));
            previous=level;
        }
        c->profile.levels[i]=(float)level;
    }
    c->profile_ready=1;
    c->waterfall=(MacroWaterfall){.state=1};
    if(head_profile(w,c)) return 1;
    c->profile_ready=0; qlcg_river_profile_free(&c->profile); return 0;
}
