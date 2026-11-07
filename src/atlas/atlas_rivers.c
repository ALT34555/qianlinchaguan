/* 舆图河流复用地表曲线 */
#include "atlas_view.h"
#include "atlas_tiles.h"
#include "../ui/ui_widgets.h"
#include "../systems/world/surface_rivers.h"
#include "../systems/world/source_rivers.h"
#include "../systems/world/surface_macro.h"
#include "../core/math_util.h"
#include <math.h>

static double short_arc(double x, double C) { return x-floor(x/C+.5)*C; }
static double map_delta(const MacroWorld *w,double x) { return qlcg_world_flat(w)?x:short_arc(x,w->circumference); }

/* 长线按视口裁剪，保留穿过部分 */
static void stroke(UiContext *ui, float ax, float ay, float bx, float by, float width,
                   uint32_t color,float x, float y, float w, float h) {
    float dx=bx-ax,dy=by-ay,lo=0,hi=1,pad=width*.5f;
    float p[4]={-dx,dx,-dy,dy};
    float q[4]={ax-x+pad,x+w-ax+pad,ay-y+pad,y+h-ay+pad};
    for (int k=0;k<4;k++) {
        if (fabsf(p[k])<1e-8f) { if(q[k]<0) return; }
        else {
            float t=q[k]/p[k];
            if(p[k]<0) lo=fmaxf(lo,t); else hi=fminf(hi,t);
        }
    }
    if(lo>hi) return;
    ui_draw_line(ui,ax+dx*lo,ay+dy*lo,ax+dx*hi,ay+dy*hi,width,color);
}

static void draw_span(const AtlasRenderer *ar,const MacroWorld *w,UiContext *ui,
                       RiverMapPoint a,RiverMapPoint b,float minimum,uint32_t color,
                       float x,float y,float width,float height) {
    double C=w->circumference,dx=map_delta(w,b.wx-a.wx),dz=b.wz-a.wz;
    int steps=(int)ceil(hypot(dx,dz)/C*6.283185307/.03);
    if(steps<1) steps=1;
    for(int k=0;k<steps;k++) {
        double t0=k/(double)steps,t1=(k+1)/(double)steps,tm=(t0+t1)*.5;
        double ax=a.wx+dx*t0,bx=a.wx+dx*t1,az=a.wz+dz*t0,bz=a.wz+dz*t1;
        double mx=a.wx+dx*tm,mz=a.wz+dz*tm;
        double coslat=qlcg_world_flat(w)?1:fmax(.01,cos(mz/C*6.283185307)),len=hypot(dx*coslat,dz);
        if(len<1e-8) continue;
        double half=a.half_width+(b.half_width-a.half_width)*tm;
        double ox=-dz/len*half/coslat,oz=dx*coslat/len*half;
        float sx0,sy0,sx1,sy1,scx,scy,sox,soy;
        if(ar->planet_view==PLANET_VIEW_GLOBE) {
            if(!qlcg_atlas_world_to_screen_vis(ar,w,ax,az,x,y,width,height,&sx0,&sy0) ||
               !qlcg_atlas_world_to_screen_vis(ar,w,bx,bz,x,y,width,height,&sx1,&sy1)) continue;
        } else {
            qlcg_atlas_world_to_screen(ar,w,ax,az,x,y,width,height,&sx0,&sy0);
            qlcg_atlas_world_to_screen(ar,w,bx,bz,x,y,width,height,&sx1,&sy1);
            float ring=calc_zoom_scale(ar,w,width)*w->circumference;
            if(!qlcg_world_flat(w)) sx1-=floorf((sx1-sx0)/ring+.5f)*ring;
        }
        bool width_ok=true;
        if(ar->planet_view==PLANET_VIEW_GLOBE) {
            width_ok=qlcg_atlas_world_to_screen_vis(ar,w,mx,mz,x,y,width,height,&scx,&scy) &&
                qlcg_atlas_world_to_screen_vis(ar,w,(mx+ox),(mz+oz),x,y,width,height,&sox,&soy);
        } else {
            qlcg_atlas_world_to_screen(ar,w,mx,mz,x,y,width,height,&scx,&scy);
            qlcg_atlas_world_to_screen(ar,w,(mx+ox),(mz+oz),x,y,width,height,&sox,&soy);
        }
        float screen_dx=sx1-sx0,screen_dy=sy1-sy0,screen_len=hypotf(screen_dx,screen_dy);
        if(screen_len<.001f) continue;
        float native=width_ok?2*fabsf((sox-scx)*screen_dy-(soy-scy)*screen_dx)/screen_len:minimum;
        if(ar->planet_view!=PLANET_VIEW_GLOBE) {
            float ring=calc_zoom_scale(ar,w,width)*w->circumference;
            float delta=sox-scx;
            if(!qlcg_world_flat(w)) delta-=floorf(delta/ring+.5f)*ring;
            native=2*fabsf(delta*screen_dy-(soy-scy)*screen_dx)/screen_len;
            if(minimum<=0 && native<.65f) continue;
            int copies=qlcg_world_flat(w)?0:1;
            for(int copy=-copies;copy<=copies;copy++)
                stroke(ui,sx0+copy*ring,sy0,sx1+copy*ring,sy1,fmaxf(minimum,native),color,x,y,width,height);
        } else if(minimum>0||native>=.65f)
            stroke(ui,sx0,sy0,sx1,sy1,fmaxf(minimum,native),color,x,y,width,height);
    }
}

void qlcg_atlas_draw_rivers(const AtlasRenderer *ar,const MacroWorld *w,UiContext *ui,
                            float x,float y,float width,float height) {
    int level=qlcg_atlas_river_level(ar,w,width,height),band=qlcg_clampi(level+3,4,8);
    float minimum=level>=12?5:3;
    AtlasTileView tiles;
    qlcg_atlas_tiles_view(ar->tiles,&tiles);
    uint32_t color=ui_rgba(51,113,139,(uint8_t)(255*(1-tiles.river_mix)));
    float scale=calc_zoom_scale(ar,w,width),span=w->circumference/w->columns;
    int any=0;
    for(int i=0;i<w->river_count;i++) if(w->rivers[i].min_level<=band) { any=1; break; }
    if(!any) band=8;
    ui_clip_push(ui,x,y,width,height);
    for(int i=0;i<w->river_count;i++) {
        const RiverSegment *rv=&w->rivers[i];
        if(rv->min_level>band) continue;
        if(ar->planet_view!=PLANET_VIEW_GLOBE) {
            double ax=map_delta(w,rv->ax-ar->cam_wx);
            double bx=ax+map_delta(w,rv->bx-rv->ax);
            double pad=span+rv->width_m;
            if(fmin(ax,bx)-pad>width*.5/scale || fmax(ax,bx)+pad<-width*.5/scale) continue;
            float sx0,sy0,sx1,sy1;
            qlcg_atlas_world_to_screen(ar,w,rv->ax,rv->az,x,y,width,height,&sx0,&sy0);
            qlcg_atlas_world_to_screen(ar,w,rv->bx,rv->bz,x,y,width,height,&sx1,&sy1);
            float margin=pad*scale*4;
            if(fminf(sy0,sy1)-margin>y+height || fmaxf(sy0,sy1)+margin<y) continue;
        }
        RiverMapPoint points[161];
        int count=0,cell=qlcg_world_cell_at_wxz(w,rv->ax,rv->az),r=w->receiver[cell];
        if(level>=9 && ar->planet_view!=PLANET_VIEW_GLOBE && r>=0 && r<w->cell_count) {
            float ax,az,bx,bz;
            qlcg_world_cell_center(w,cell,&ax,&az); qlcg_world_cell_center(w,r,&bx,&bz);
            if(hypot(map_delta(w,ax-rv->ax),az-rv->az)<1 &&
               hypot(map_delta(w,bx-rv->bx),bz-rv->bz)<1) {
                count=qlcg_river_map_path(w,cell,points,161);
                if(count<2) continue;
            }
        }
        if(count<2) {
            points[0]=(RiverMapPoint){rv->ax,rv->az,rv->width_m*.5};
            points[1]=(RiverMapPoint){rv->bx,rv->bz,rv->width_m*.5}; count=2;
        }
        for(int k=1;k<count;k++) draw_span(ar,w,ui,points[k-1],points[k],minimum,color,x,y,width,height);
    }
    ui_clip_pop(ui);
}

static uint32_t source_color(const MacroWorld *w,double wx,double wz,double level,double depth,
                               double radius,int kind,float opacity,bool show_ice) {
    double coverage=0;
    if(show_ice&&w->ice_time_valid) {
        SurfaceMacroContext context=qlcg_macro_context(w,wx,wz);
        IceSite site={0}; IceSample ice;
        site.annual_temp=(float)qlcg_read_macro(w,&context,w->temperature);
        site.latitude=(float)(-wz/qlcg_world_depth(w)*3.141592654);
        site.bed=(float)(level-depth); site.top=(float)level; site.depth=(float)depth;
        site.bank_distance=(float)radius; site.noise=.5f; site.kind=(uint8_t)kind;
        site.shore_weight=1-qlcg_smoothf(1,8,(float)radius);
        qlcg_ice_evaluate(&site,&w->ice_time,&ice); coverage=ice.coverage;
    }
    return ui_rgba((uint8_t)(51+coverage*155),(uint8_t)(113+coverage*114),
                    (uint8_t)(139+coverage*96),(uint8_t)(255*opacity));
}

void qlcg_atlas_draw_sources(const AtlasRenderer *ar,const MacroWorld *w,UiContext *ui,
                              float opacity,float x,float y,float width,float height) {
    int level=qlcg_atlas_river_level(ar,w,width,height);
    if(level<4||opacity<=0||!w->river_width) return;
    double max_width=w->columns<=256?12:w->columns<=512?24:32;
    if(level<10&&max_width*calc_zoom_scale(ar,w,width)<.325) return;
    double col0=INFINITY,col1=-INFINITY,row0=INFINITY,row1=-INFINITY;
    double cam=(ar->cam_wx/w->circumference+.5)*w->columns;
    for(int iz=0;iz<=4;iz++) for(int ix=0;ix<=4;ix++) {
        double wx,wz;
        if(!qlcg_atlas_screen_to_world_d(ar,w,x+width*ix/4,y+height*iz/4,x,y,width,height,&wx,&wz)) continue;
        double col=(wx/w->circumference+.5)*w->columns;
        if(!qlcg_world_flat(w)) col-=floor((col-cam)/w->columns+.5)*w->columns;
        double row=(wz/w->circumference+qlcg_world_z_offset(w))*w->columns;
        col0=fmin(col0,col); col1=fmax(col1,col); row0=fmin(row0,row); row1=fmax(row1,row);
    }
    if(!isfinite(col0)) return;
    int cells[32],used=0; double distances[32];
    double cx=(col0+col1)*.5,cz=(row0+row1)*.5;
    for(int row=(int)fmax(0,floor(row0)-1);row<=fmin(w->rows-1,ceil(row1)+1);row++)
        for(int col=(int)floor(col0)-1;col<=ceil(col1)+1;col++) {
            int cell=qlcg_macro_index(w,col,row);
            if(!qlcg_source_candidate(w,cell)) continue;
            double distance=(col+.5-cx)*(col+.5-cx)+(row+.5-cz)*(row+.5-cz);
            int duplicate=0; for(int k=0;k<used;k++) if(cells[k]==cell) duplicate=1;
            if(duplicate) continue;
            if(used==32&&distance>=distances[31]) continue;
            int at=used<32?used++:31;
            while(at>0&&distance<distances[at-1]) {
                if(at<32) { cells[at]=cells[at-1]; distances[at]=distances[at-1]; } at--;
            }
            cells[at]=cell; distances[at]=distance;
        }
    float minimum=level>=16?3:level>=10?1:0;
    bool show_ice=qlcg_atlas_layer_opacity(&ar->display,LAYER_ICE)>0;
    AtlasTileView tiles;
    qlcg_atlas_tiles_view(ar->tiles,&tiles);
    int budget=tiles.level>0 && ldexp(w->circumference,-tiles.level)/ATLAS_TILE_SAMPLE_PX<=32?0:1;
    ui_clip_push(ui,x,y,width,height);
    for(int k=0;k<used;k++) {
        const SourceRiverNetwork *n=qlcg_source_network(w,cells[k],0);
        if(!n&&budget>0) { n=qlcg_source_network(w,cells[k],1); budget--; }
        if(!n) continue;
        for(int j=0;j<n->edge_count;j++) {
            const SourceRiverEdge *e=n->edges+j;
            for(int v=1;v<e->count;v++) {
                const RiverVectorPoint *a=e->points+v-1,*b=a+1;
                uint32_t color=source_color(w,a->wx,a->wz,a->level,a->depth,a->width,ICE_KIND_RIVER,opacity,show_ice);
                draw_span(ar,w,ui,(RiverMapPoint){a->wx,a->wz,a->width},
                    (RiverMapPoint){b->wx,b->wz,b->width},minimum,color,x,y,width,height);
            }
            if(e->target_kind==SOURCE_TARGET_POOL) {
                RiverSource pool; qlcg_source_terminal_pool(n,e,&pool);
                SurfacePoint center=qlcg_frame_coordinates(&n->frame,pool.x,pool.z);
                uint32_t color=source_color(w,center.x,center.z,pool.pool_level,pool.pool_depth,pool.pool_rx,ICE_KIND_STATIC,opacity,show_ice);
                const SourceForm *form=qlcg_source_form(pool.spring_form);
                for(int band=0;band<16;band++) {
                    double a0=-1+band*.125,a1=a0+.125,mid=(a0+a1)*.5;
                    double radius=pool.pool_rz*sqrt(fmax(0,1-mid*mid));
                    double along0=(a0+form->shift)*pool.pool_rx,along1=(a1+form->shift)*pool.pool_rx;
                    SurfacePoint a=qlcg_frame_coordinates(&n->frame,pool.x+along0*pool.direction_x,pool.z+along0*pool.direction_z);
                    SurfacePoint b=qlcg_frame_coordinates(&n->frame,pool.x+along1*pool.direction_x,pool.z+along1*pool.direction_z);
                    draw_span(ar,w,ui,(RiverMapPoint){a.x,a.z,radius},(RiverMapPoint){b.x,b.z,radius},minimum,color,x,y,width,height);
                }
            }
        }
        for(int j=0;j<n->source_count;j++) {
            const RiverSource *s=n->sources+j;
            if(s->planned<0) continue;
            if(!s->connected&&(!s->pool||s->damp_only)) continue;
            const SourceForm *form=qlcg_source_form(s->spring_form);
            int pooled=s->connected&&form->pooled;
            double radius=s->connected?(pooled?qlcg_source_pool_radius(s):s->spring_radius):s->pool_rx;
            double level_m=s->connected?s->surface_level:s->pool_level;
            double depth=s->connected?(pooled?form->depth*s->finish.depth:.5):s->pool_depth;
            SurfacePoint a=qlcg_frame_coordinates(&n->frame,s->x-radius*.4,s->z);
            SurfacePoint b=qlcg_frame_coordinates(&n->frame,s->x+radius*.4,s->z);
            uint32_t color=source_color(w,a.x,a.z,level_m,depth,radius,ICE_KIND_STATIC,opacity,show_ice);
            draw_span(ar,w,ui,(RiverMapPoint){a.x,a.z,radius},(RiverMapPoint){b.x,b.z,radius},
                minimum,color,x,y,width,height);
        }
    }
    ui_clip_pop(ui);
}
