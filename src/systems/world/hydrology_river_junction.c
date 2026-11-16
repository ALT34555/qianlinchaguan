/* 主河汇流计划与地貌应用 */
#include "hydrology_river_internal.h"
#include "hydrology_lakes.h"

static int build_junction(const MacroWorld *w,int cell,JunctionSlot *slot) {
    if (w->water[cell]==1 || qlcg_lake_overlaps_cell(w,cell)) return 0;
    int count=1;
    slot->edges[0]=cell;
    for (int k=0;k<8;k++) {
        int i=w->neighbors[cell*8+k];
        if (w->receiver[i]!=cell || !qlcg_river_main_width(w,i)) continue;
        if (count==3) return 0;
        slot->edges[count++]=i;
    }
    if (count!=3) return 0;
    RiverCurve *down=qlcg_river_curve_at(w,cell);
    if (!down || down->sea_mouth) return 0;
    RiverJunction *plan=&slot->plan;
    slot->frame=down->frame;
    double span=w->circumference/w->columns*cos(slot->frame.lat);
    double last=down->points[down->count-1].arc;
    plan->width=down->points[0].width;
    plan->radius=fmin(fmin(5.5*plan->width,.34*span),last*.42);
    if (plan->radius<32 || plan->radius<1.45*plan->width || !qlcg_river_ensure_profile(w,down)) return 0;
    plan->level=qlcg_river_curve_level(down,0);
    plan->depth=fmax(1.2,down->depth_a);
    Point p=qlcg_river_curve_position(down,plan->radius);
    if (hypot(p.x,p.z)<1) return 0;
    qlcg_junction_configure(plan,w->opt.seed^((uint32_t)cell*0x9e3779b9u),p.x,p.z);
    return 1;
}

const JunctionSlot *qlcg_river_junction_at(const MacroWorld *w,int cell) {
    if (!qlcg_river_main_width(w,cell) || w->receiver[cell]<0) return NULL;
    JunctionSlot *slot=qlcg_river_junction_slot(w,cell);
    if (!slot) return NULL;
    if (slot->key!=cell+1) {
        slot->key=cell+1;
        slot->valid=build_junction(w,cell,slot);
    }
    return slot->valid?slot:NULL;
}

int qlcg_river_junction_edge(const JunctionSlot *join,int cell) {
    return cell==join->edges[0] || cell==join->edges[1] || cell==join->edges[2];
}

void qlcg_river_decorate_junction(const RiverJunction *plan,Point point,double weight,
                               double core,RiverSurfaceSample *out) {
    if (out->kind!=WATER_KIND_RIVER || out->height>=out->level-.02f) return;
    JunctionRelief field;
    qlcg_junction_relief(plan,point.x,point.z,&field);
    double raise=1.58*field.bar+.64*field.shelf;
    double bed=out->height+plan->depth*weight*(raise-.58*field.scour-.42*field.branch);
    double guard=qlcg_smooth01d((core+.12)/.24);
    double dry_limit=out->level+plan->depth*.14;
    double wet_limit=out->level-fmin(.35,fmax(.05,plan->depth*.08));
    if (raise>0) {
        double emergence=(1-guard)*qlcg_smooth01d((field.bar-.14)/.35);
        double ceiling=qlcg_mixd(wet_limit,dry_limit,emergence);
        bed=fmin(bed,fmax(out->height,ceiling));
    }
    out->height=(float)bed;
    out->sediment=(float)qlcg_clampd(weight*(1.6*field.bar+field.shelf),0,1);
    if (bed>=out->level-.02) {
        out->kind=WATER_KIND_DRY;
        out->level=-1e9f;
    }
}
