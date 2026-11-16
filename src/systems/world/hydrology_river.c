/* 主河查询、候选与点级水面 */
#include "hydrology_river_internal.h"
#include <stdlib.h>
#include "hydrology_lakes.h"
#include "surface_macro.h"

typedef struct {
    int cell;
    double arc,offset,radius,depth,level,curvature,span,ground;
} JunctionBank;
static int crosses_cell(const MacroWorld *w,int cell,const RiverCurve *c) {
    SurfacePoint origin=qlcg_river_cell_center(w,cell);
    double half=w->circumference/(double)w->columns/2,C=w->circumference;
    for (int k=1;k<c->count;k++) {
        const CurvePoint *a=&c->points[k-1],*b=&c->points[k];
        double ax=a->wx-origin.x,bx=b->wx-origin.x;
        ax-=floor(ax/C+.5)*C; bx-=floor(bx/C+.5)*C;
        double az=a->wz-origin.z,bz=b->wz-origin.z,dx=bx-ax,dz=bz-az,lo=0,hi=1;
        double p[4]={-dx,dx,-dz,dz},q[4]={ax+half,half-ax,az+half,half-az};
        int valid=1;
        for (int j=0;j<4;j++) {
            if (fabs(p[j])<1e-9) { if (q[j]<0) valid=0; }
            else { double t=q[j]/p[j]; if (p[j]<0) lo=fmax(lo,t); else hi=fmin(hi,t); }
        }
        if (valid && lo<=hi) return 1;
    }
    return 0;
}

int qlcg_macro_cell_has_water(const MacroWorld *w,int cell) {
    if (w->water[cell]==1 || w->water[cell]==2 || qlcg_river_main_width(w,cell)>0) return 1;
    for (int k=0;k<8;k++) {
        int i=w->neighbors[cell*8+k];
        if (!qlcg_river_main_width(w,i)) continue;
        RiverCurve *c=qlcg_river_curve_at(w,i);
        if (c && crosses_cell(w,cell,c)) return 1;
    }
    int edges[25],count=qlcg_lake_river_candidates(w,cell,edges,25);
    for (int k=0;k<count;k++) {
        if (!qlcg_river_main_width(w,edges[k])) continue;
        RiverCurve *c=qlcg_river_curve_at(w,edges[k]);
        if (c && crosses_cell(w,cell,c)) return 1;
    }
    return 0;
}

static int append_river_candidate(const MacroWorld *w,Point origin,int edge,RiverDetailCandidates *out) {
    int r=w->receiver[edge];
    if (r<0 || r>=w->cell_count || !qlcg_river_main_width(w,edge)) return 1;
    RiverCurve *c=qlcg_river_curve_at(w,edge);
    if (!c) return 0;
    ReachBounds b=c->detail_bounds;
    double half=w->circumference/w->columns*.5,C=w->circumference;
    double lon=origin.x-c->origin_x;
    lon-=floor(lon/C+.5)*C;
    if (origin.z+half<b.top || origin.z-half>b.bottom ||
        lon+half<b.left || lon-half>b.right) return 1;
    for (int i=0;i<out->count;i++) if (out->edges[i]==edge) return 1;
    if (out->count<25) out->edges[out->count++]=edge;
    return 1;
}

int qlcg_river_detail_candidates(const MacroWorld *w,int cell,RiverDetailCandidates *out) {
    RiverCellCache *entry=qlcg_river_candidate_slot(w,cell);
    if (!entry) return 0;
    if (entry->key==cell+1) { *out=entry->list; return 1; }
    Point origin=qlcg_river_cell_center(w,cell);
    int x=cell%w->columns,z=cell/w->columns;
    *out=(RiverDetailCandidates){0};
    for (int k=-1;k<8;k++) {
        int edge=k<0?cell:w->neighbors[cell*8+k];
        if (!append_river_candidate(w,origin,edge,out)) return 0;
    }
    int edges[25],count=qlcg_lake_river_candidates(w,cell,edges,25);
    for (int k=0;k<count;k++) if (!append_river_candidate(w,origin,edges[k],out)) return 0;
    for (int dz=-2;dz<=2;dz++) for (int dx=-2;dx<=2;dx++) {
        if (abs(dx)<2 && abs(dz)<2) continue;
        if (!append_river_candidate(w,origin,qlcg_macro_index(w,x+dx,z+dz),out)) return 0;
    }
    entry->key=cell+1;
    entry->list=*out;
    return 1;
}

static int in_detail_bounds(const RiverCurve *c,double wx,double wz) {
    ReachBounds b=c->detail_bounds;
    double C=c->frame.C,lon=wx-c->origin_x;
    lon-=floor(lon/C+.5)*C;
    return wz>=b.top && wz<=b.bottom && lon>=b.left && lon<=b.right;
}

static double detail_gate(const MacroWorld *w,const RiverDetailCandidates *list,double wx,double wz,
                           RiverValleySample *valley,double fade) {
    double gate=1;
    if(valley)*valley=(RiverValleySample){1,1};
    RiverCurve *preview=qlcg_river_preview(w);
    for (int i=preview?-1:0;i<list->count;i++) {
        if(i>=0 && preview && list->edges[i]==preview->cell) continue;
        RiverCurve *c=i<0?preview:qlcg_river_curve_at(w,list->edges[i]);
        if (!c) return 0;
        if (fade<=0&&!in_detail_bounds(c,wx,wz)) continue;
        Point p=qlcg_frame_local(&c->frame,wx,wz);
        double distance,t;
        if (qlcg_river_index_nearest(&c->spatial,c->points,p.x,p.z,&distance,&t)<0) continue;
        gate=fmin(gate,qlcg_smooth01d((distance-c->detail_clear)/fmax(c->detail_fade,fade)));
        if (gate==0) break;
        if(!valley)continue;
        double edge=distance-c->detail_clear-12;
        valley->protection=fmin(valley->protection,qlcg_smooth01d(edge/48));
        if (c->valley_span>0 && w->water[c->cell]!=1 && w->water[c->cell]!=2 &&
            w->water[c->receiver]!=2)
            valley->position=fmin(valley->position,edge/c->valley_span);
    }
    return gate;
}
double qlcg_river_detail_gate(const MacroWorld *w,const RiverDetailCandidates *list,double wx,double wz,
                              RiverValleySample *valley) {
    return detail_gate(w,list,wx,wz,valley,0);
}
double qlcg_river_relief_gate(const MacroWorld *w,const RiverDetailCandidates *list,double wx,double wz,double fade) {
    return detail_gate(w,list,wx,wz,NULL,fade);
}

static CurvePoint curve_station(const RiverCurve *c,double arc) {
    int hi=qlcg_river_curve_arc_end(c,arc),lo=hi?hi-1:0;
    const CurvePoint *a=c->points+lo,*b=c->points+hi;
    double t=b->arc>a->arc?qlcg_clampd((arc-a->arc)/(b->arc-a->arc),0,1):0;
    if (t==0) return *a;
    if (t==1) return *b;
    CurvePoint p={0}; p.x=qlcg_mixd(a->x,b->x,t); p.z=qlcg_mixd(a->z,b->z,t);
    Point q=qlcg_frame_coordinates(&c->frame,p.x,p.z);
    p.wx=q.x; p.wz=q.z; p.arc=arc; p.width=qlcg_mixd(a->width,b->width,t);
    return p;
}

static int visible_path(const RiverCurve *c,CurvePoint *out,int capacity) {
    double first=isfinite(c->outlet.arc)?c->outlet.arc:0;
    double last=isfinite(c->inlet.arc)?c->inlet.arc:c->points[c->count-1].arc;
    if (capacity<2 || last<=first) return 0;
    int n=0; out[n++]=curve_station(c,first);
    for (int k=0;k<c->count && n<capacity-1;k++)
        if (c->points[k].arc>first && c->points[k].arc<last) out[n++]=c->points[k];
    out[n++]=curve_station(c,last);
    return n;
}

int qlcg_river_head_constraint(const MacroWorld *w,int cell,double *run,double *level) {
    RiverCurve *c=qlcg_river_curve_at(w,cell);if(!c||!qlcg_river_ensure_profile(w,c))return 0;
    double desired=*run,best=INFINITY;
    for(int k=0;k<3;k++) {
        double arc=fmin(desired*(1+.5*k),c->points[c->count-1].arc*.15);
        double at=qlcg_river_head_receiver_level(w,c,arc),after=qlcg_river_head_receiver_level(w,c,arc+4),grade=after-at;
        if(grade<best) { best=grade;*run=arc;*level=at; }
        if(grade<=0)break;
    }
    if(best>0) { *run=0;*level=qlcg_river_head_receiver_level(w,c,0);return 2; }
    return 1;
}

int qlcg_river_map_path(const MacroWorld *w,int cell,RiverMapPoint *out,int capacity) {
    if (cell<0 || cell>=w->cell_count || !out || capacity<=0) return 0;
    RiverCurve *c=qlcg_river_curve_at(w,cell); if (!c) return 0;
    if(qlcg_river_archive_terrain(w)>=5&&qlcg_is_river_head(w,cell)&&!qlcg_river_ensure_profile(w,c))return 0;
    CurvePoint points[RIVER_CURVE_POINTS];
    int count=visible_path(c,points,capacity<RIVER_CURVE_POINTS?capacity:RIVER_CURVE_POINTS);
    for (int k=0;k<count;k++) {
        const CurvePoint *p=points+k;
        out[k]=(RiverMapPoint){p->wx,p->wz,qlcg_river_curve_head_width(c,p->arc,p->width)};
    }
    return count;
}

int qlcg_river_mouth(const MacroWorld *w,int cell,int outlet,RiverMouthPoint *out) {
    if (cell<0 || cell>=w->cell_count || !out) return 0;
    RiverCurve *c=qlcg_river_curve_at(w,cell); if (!c) return 0;
    const RiverMouthPoint *m=outlet?&c->outlet:&c->inlet;
    if (!m->kind || !isfinite(m->arc)) return 0;
    *out=*m; return 1;
}

int qlcg_river_path(const MacroWorld *w,int cell,RiverPathPoint *out,int capacity) {
    if (cell<0 || cell>=w->cell_count) return 0;
    RiverCurve *c=qlcg_river_curve_at(w,cell); if (!c || !qlcg_river_ensure_profile(w,c)) return 0;
    CurvePoint points[RIVER_CURVE_POINTS];
    int count=visible_path(c,points,capacity<RIVER_CURVE_POINTS?capacity:RIVER_CURVE_POINTS);
    for (int k=0;k<count;k++) {
        CurvePoint *p=points+k;
        out[k]=(RiverPathPoint){p->wx,p->wz,p->arc,qlcg_river_curve_level(c,p->arc),qlcg_river_curve_depth(c,p->arc),qlcg_river_curve_head_width(c,p->arc,p->width)};
    }
    return count;
}

int qlcg_river_join_station(const MacroWorld *w,int cell,double wx,double wz,double *arc) {
    if(cell<0||cell>=w->cell_count) return 0;
    RiverCurve *curve=qlcg_river_curve_at(w,cell);
    if(!curve||!in_detail_bounds(curve,wx,wz)) return 0;
    Point p=qlcg_frame_local(&curve->frame,wx,wz);
    double distance,t;
    int near=qlcg_river_index_nearest(&curve->spatial,curve->points,p.x,p.z,&distance,&t);
    if(near<0) return 0;
    *arc=qlcg_mixd(curve->points[near].arc,curve->points[near+1].arc,t);
    return !qlcg_river_curve_is_legacy(curve);
}

void qlcg_river_surface_with_widths(const MacroWorld *w,float wx,float wz,float natural,
                                    const HydrologyWidthQuery *widths,RiverSurfaceSample *out) {
    int lake=out->kind==WATER_KIND_LAKE,sea=out->kind==WATER_KIND_SEA;
    double best=INFINITY,height=natural;
    int cell=qlcg_world_cell_at_wxz(w,wx,wz);
    const JunctionSlot *join=lake||sea?NULL:qlcg_river_junction_at(w,cell);
    Point join_point={0};
    double weight=0,first=-INFINITY,second=-INFINITY,core=-INFINITY;
    int winner=-1;
    JunctionBank bank={0};
    if (join) {
        join_point=qlcg_frame_local(&join->frame,wx,wz);
        weight=qlcg_junction_weight(&join->plan,hypot(join_point.x,join_point.z));
    }
    RiverDetailCandidates list={0};
    int ready=qlcg_river_detail_candidates(w,cell,&list);
    for (int k=0;k<(ready?list.count:9);k++) {
        int i=ready?list.edges[k]:(k?w->neighbors[cell*8+k-1]:cell);
        if (!(qlcg_river_main_width(w,i)>0) || w->receiver[i]<0) continue;
        RiverCurve *c=qlcg_river_curve_at(w,i); if (!c || !in_detail_bounds(c,wx,wz)) continue;
        Point p=qlcg_frame_local(&c->frame,wx,wz);
        double distance,t;
        int near=qlcg_river_index_nearest(&c->spatial,c->points,p.x,p.z,&distance,&t);
        if (near<0) continue;
        CurvePoint *a=&c->points[near],*b=&c->points[near+1];
        double radius=qlcg_mixd(a->width,b->width,t);
        int legacy=qlcg_river_curve_is_legacy(c);
        int fixed=qlcg_river_archive_terrain(w)>=9&&c->edit_mode==RIVER_CURVE_NATURAL&&!lake&&!sea;
        if (distance>radius+(legacy?512:fixed?256:48)) continue;
        if ((lake||sea) && isfinite(c->inlet.arc) && c->inlet.arc==0 &&
            (!c->outlet.kind || c->outlet.arc>=c->points[c->count-1].arc)) continue;
        if (lake && c->outlet.kind && c->outlet.arc>=c->points[c->count-1].arc && !c->inlet.kind) continue;
        double arc=qlcg_mixd(a->arc,b->arc,t),level=out->level,depth=qlcg_river_curve_depth(c,arc);
        if (!lake && !sea) {
            if (!qlcg_river_ensure_profile(w,c)) continue;
            level=qlcg_river_curve_level(c,arc);
        }
        radius=qlcg_river_curve_head_width(c,arc,radius);
        if(!legacy&&!lake&&!sea)
            radius=qlcg_hydrology_receiver_width(widths,0,i,arc,radius);
        int headreach=c->head_run>0&&arc<c->head_run&&!legacy&&!lake&&!sea;
        double water_blend=1;
        if (lake || sea) {
            water_blend=0;
            if (c->inlet.kind==out->kind && isfinite(c->inlet.arc) && fabs(c->inlet.level-out->level)<.02)
                water_blend=fmax(water_blend,1-qlcg_smooth01d(fabs(arc-c->inlet.arc)/fmax(1,c->inlet.blend)));
            if (c->outlet.kind==out->kind && isfinite(c->outlet.arc) && fabs(c->outlet.level-out->level)<.02)
                water_blend=fmax(water_blend,1-qlcg_smooth01d(fabs(c->outlet.arc-arc)/fmax(1,c->outlet.blend)));
            if (water_blend<=0) continue;
            level=out->level;
        }
        double vx=b->x-a->x,vz=b->z-a->z,span=hypot(vx,vz);
        double side=((p.x-a->x)*(-vz)+(p.z-a->z)*vx)>=0?1:-1;
        double curvature=qlcg_mixd(a->curvature,b->curvature,t);
        double fall_grade=0;
        if(!lake && !sea && !legacy) {
            double ahead=fixed?qlcg_river_curve_level(c,arc-16):qlcg_river_profile_level(&c->ground_profile,qlcg_river_curve_profile_arc(c,arc)-16);
            double below=fixed?qlcg_river_curve_level(c,arc+16):qlcg_river_profile_level(&c->ground_profile,qlcg_river_curve_profile_arc(c,arc)+16);
            double upper=fixed?qlcg_river_curve_level(c,arc-64):qlcg_river_profile_level(&c->ground_profile,qlcg_river_curve_profile_arc(c,arc)-64);
            double lower=fixed?qlcg_river_curve_level(c,arc+64):qlcg_river_profile_level(&c->ground_profile,qlcg_river_curve_profile_arc(c,arc)+64);
            fall_grade=fmax(0,(ahead-below)/32)*qlcg_smooth01d((upper-lower-20)/20);
            depth=fmin(4,depth);
            if(!headreach&&!fixed) level=fmin(level,natural+.15);
            if(fall_grade>.7) out->waterfall_rock=1;
        }
        if(!lake && !sea && !headreach && !fixed && qlcg_river_archive_terrain(w)>=2)
            level=qlcg_river_crossfall_level(w,c,p,(Point){vx/span,vz/span},arc,radius,side*distance,
                                 natural,level,depth,1-weight,&fall_grade);
        double bank_span=fixed?qlcg_clampd((depth+fabs(natural-level))*2.5,6,256):0;
        double ground=natural;
        if(fixed)ground+=fmax(0,level+.35-natural)*(1-qlcg_smooth01d((distance-radius)/bank_span));
        else if(headreach)ground+=fmax(0,level+.35-natural)*(1-qlcg_smooth01d((distance-radius)/qlcg_clampd(radius*3+24,32,96)));
        if (weight>0 && qlcg_river_junction_edge(join,i)) {
            level=qlcg_mixd(level,join->plan.level,weight);
            double margin=radius-distance;
            core=fmax(core,.55-distance/fmax(1,radius));
            if (margin>first) {
                second=first;
                first=margin;
                bank=(JunctionBank){i,arc,side*distance,radius,depth,level,curvature,bank_span,ground};
            } else second=fmax(second,margin);
        }
        double bed; int wet;
        if(legacy) {
            bed=natural;
            wet=qlcg_hydrology_section_span(distance,radius,depth,natural,level,radius+512,&bed,&level);
            fall_grade=fmax(fall_grade,fmax(0,(qlcg_river_curve_level(c,arc-16)-qlcg_river_curve_level(c,arc+16))/32));
        } else {
            if(!qlcg_river_bank_section_span(w->opt.seed,i,qlcg_river_curve_profile_arc(c,arc),side*distance,radius,depth,level,ground,curvature,bank_span,&bed,&wet)) {
                if(headreach&&ground>natural) { bed=ground;wet=0; }
                else continue;
            }
        }
        if(!lake && !sea && !legacy) {
            if(qlcg_river_archive_terrain(w)>=2 && fall_grade>.7 && distance<radius)
                bed=fmin(bed,level-fmin(.5,depth)*qlcg_smooth01d((radius-distance)/6));
            double limit=fmax(20,fmin(32,depth*6));
            if(!headreach&&!fixed)bed=fmax(bed,natural-limit);
            wet=distance<radius && bed<level-.02;
        }
        if((legacy||headreach||fixed)&&distance<best) height=qlcg_mixd(natural,bed,water_blend);
        else height=fmin(height,qlcg_mixd(natural,bed,water_blend));
        if (lake || sea) continue;
        if(!wet && (!out->has_bank_level || level-bed>out->bank_level-height)) {
            out->bank_level=(float)fmin(level,bed);
            out->has_bank_level=1;
        }
        if (!wet || distance>=best) continue;
        best=distance;
        winner=i;
        out->level=(float)(out->kind==WATER_KIND_SEA?fmax(out->level,level):level);
        if (out->kind!=WATER_KIND_SEA) out->kind=fall_grade>.7?WATER_KIND_FALL:WATER_KIND_RIVER;
        double lon=wx/w->circumference*2*QLCG_RIVER_PI,lat=-wz/w->circumference*2*QLCG_RIVER_PI;
        double east[3]={-sin(lon),0,cos(lon)},north[3]={-sin(lat)*cos(lon),cos(lat),-sin(lat)*sin(lon)};
        double e=0,n=0;
        for (int d=0;d<3;d++) {
            double v=(c->frame.east[d]*vx+c->frame.south[d]*vz)/span;
            e+=v*east[d]; n+=v*north[d];
        }
        out->east=(float)(c->frame.flat?vx/span:e); out->north=(float)(c->frame.flat?-vz/span:n); out->width=(float)(radius*2);
        out->bank_distance=(float)fmax(0.0,radius-distance);
        out->lateral=(float)qlcg_clampd(distance/fmax(1e-6,radius),0.0,1.0);
        out->discharge=(float)w->flow[i];
        out->channel_depth=w->river_level && isfinite(w->river_level[i])?(float)depth:0;
        out->grade=(float)fmax(fall_grade,fmax(0,(qlcg_river_curve_level(c,arc-24)-qlcg_river_curve_level(c,arc+24))/48));
    }
    if (weight>0 && isfinite(second)) {
        double band=fmin(42,fmax(6,.16*bank.radius));
        double overlap=fmax(0,1-(first-second)/band);
        double rounded=.25*band*overlap*overlap*weight;
        double bed;
        int wet;
        if (rounded>0 && qlcg_river_bank_section_span(w->opt.seed,bank.cell,bank.arc,bank.offset,
                    bank.radius+rounded,bank.depth,bank.level,bank.span>0?bank.ground:natural,bank.curvature,bank.span,&bed,&wet)) {
            height=fmin(height,bed);
            if (wet && out->kind==WATER_KIND_DRY) {
                winner=bank.cell;
                out->kind=WATER_KIND_RIVER;
                out->level=(float)bank.level;
                out->east=(float)join->plan.east;
                out->north=(float)-join->plan.south;
                out->discharge=(float)w->flow[bank.cell];
                out->width=(float)(bank.radius*2);
                out->lateral=1;
            }
        }
    }
    out->height=(float)height;
    if (weight>0 && qlcg_river_junction_edge(join,winner))
        qlcg_river_decorate_junction(&join->plan,join_point,weight,core,out);
}

void qlcg_river_surface(const MacroWorld *w,float wx,float wz,float natural,RiverSurfaceSample *out) {
    qlcg_river_surface_with_widths(w,wx,wz,natural,NULL,out);
}
