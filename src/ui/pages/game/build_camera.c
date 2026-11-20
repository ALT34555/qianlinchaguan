#include "build_internal.h"
#include "terrain3d/terrain_settlement.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static float ground(float wx,float wz) {
    return qlcg_region_height_at(build.region,wx-build.region->center_x,wz-build.region->center_z);
}
void game_build_gesture(float zoom,float angle) {
    if(!game_build_open()) return;
    build.distance=qlcg_clampf(build.distance*zoom,build.mode?6:48,build.mode?45:120);
    build.yaw+=angle;
}
void build_camera(UiContext *ui,bool canvas) {
    double now=qlcg_time_ms();
    float dt=(float)fmin(.08,fmax(0,(now-build.last_time)*.001));
    build.last_time=now;
    float c=cosf(build.yaw),s=sinf(build.yaw),v=dt*build.distance*.8f;
    float side=v*(qlcg_platform_key_down('D')-qlcg_platform_key_down('A'));
    float ahead=v*(qlcg_platform_key_down('W')-qlcg_platform_key_down('S'));
    build.yaw+=dt*1.45f*(qlcg_platform_key_down('E')-qlcg_platform_key_down('Q'));
    if(canvas) {
        float dx=(float)(ui->mouse_x-build.last_x),dy=(float)(ui->mouse_y-build.last_y);
        if(ui->right_down) {
            build.yaw+=dx*.007f;build.pitch=qlcg_clampf(build.pitch-dy*.006f,-1.5359f,-.2618f);build.view=0;
        }
        if(qlcg_platform_key_down(4) || (ui->pointer_touch && ui->mouse_down && !ui->mouse_pressed && !build.area_drag)) {
            float scale=build.distance/(float)ui->win_h;
            side-=dx*scale;ahead+=dy*scale;
        }
        if(ui->wheel_delta) game_build_gesture(powf(.83f,ui->wheel_delta/120.f),0);
    }
    build.last_x=ui->mouse_x;build.last_y=ui->mouse_y;
    build.fx+=c*side+s*ahead;build.fz+=s*side-c*ahead;
    if(build.mode) {
        float x=(build.cx+.5f)*32,z=(build.cz+.5f)*32;
        build.fx=qlcg_clampf(build.fx,x-20,x+20);build.fz=qlcg_clampf(build.fz,z-20,z+20);
    }
}
static bool project(UiContext *ui,float x,float y,float z,float *sx,float *sy) {
    float a[]={x-build.eye[0],y-build.eye[1],z-build.eye[2]};
    float d=qlcg_dot3(a,build.forward);
    if(!isfinite(d) || d<.15f) return false;
    float f=build.focal/(build.view==1?build.distance:d);
    *sx=ui->win_w*.5f+f*qlcg_dot3(a,build.right);
    *sy=ui->win_h*.5f-f*qlcg_dot3(a,build.up);
    return true;
}
static bool pick_surface(UiContext *ui,float *wx,float *wz) {
    float u=(ui->mouse_x-ui->win_w*.5f)/build.focal,v=(ui->win_h*.5f-ui->mouse_y)/build.focal;
    float origin[3],dir[3];
    for(int i=0;i<3;i++) {
        float offset=u*build.right[i]+v*build.up[i];
        origin[i]=build.eye[i]+(build.view==1?offset*build.distance:0);
        dir[i]=build.forward[i]+(build.view==1?0:offset);
    }
    float previous=0;
    for(float t=.2f;t<320;) {
        float x=origin[0]+dir[0]*t,z=origin[2]+dir[2]*t;
        float diff=origin[1]+dir[1]*t-ground(x,z);
        if(isfinite(diff) && diff<=0) {
            float hi=t,lo=previous;
            for(int j=0;j<12;j++) {
                float m=(lo+hi)*.5f;
                float h=ground(origin[0]+dir[0]*m,origin[2]+dir[2]*m);
                if(origin[1]+dir[1]*m>h) lo=m;else hi=m;
            }
            *wx=origin[0]+dir[0]*hi;*wz=origin[2]+dir[2]*hi;return true;
        }
        previous=t;t+=isfinite(diff)?qlcg_clampf(diff*.42f,.30f,5):3;
    }
    return false;
}
static void ground_line(UiContext *ui,float x,float z,float ex,float ez,uint32_t color,float weight,bool dashed) {
    int n=(int)ceilf(hypotf(ex-x,ez-z)/2);
    float ax=0,ay=0;
    bool prev=false;
    for(int i=0;i<=n;i++) {
        float t=n?(float)i/n:0,wx=x+(ex-x)*t,wz=z+(ez-z)*t,bx=0,by=0;
        bool visible=project(ui,wx,ground(wx,wz)+.06f,wz,&bx,&by);
        if(prev && visible && (!dashed || i%2)) ui_draw_line(ui,ax,ay,bx,by,weight,color);
        prev=visible;ax=bx;ay=by;
    }
}
static void outline(UiContext *ui,int cx,int cz,uint32_t color,float weight,bool dash) {
    float x=cx*32.f,z=cz*32.f;
    ground_line(ui,x,z,x+32,z,color,weight,dash);ground_line(ui,x,z+32,x+32,z+32,color,weight,dash);
    ground_line(ui,x,z,x,z+32,color,weight,dash);ground_line(ui,x+32,z,x+32,z+32,color,weight,dash);
}
static void draw_grid(UiContext *ui) {
    uint32_t gold=ui_rgba(247,191,87,245),teal=ui_rgba(93,208,175,220);
    if(!build.mode) {
        int cx=(int)floorf(build.fx/32),cz=(int)floorf(build.fz/32);
        for(int z=cz-4;z<=cz+4;z++) for(int x=cx-4;x<=cx+4;x++) {
            QlcgCivTerritory t;
            if(qlcg_civ_territory_at(build.world,x,z,&t) && t.faction==build.faction)
                outline(ui,x,z,t.type==QLCG_FLAG_CHUNK?gold:teal,t.type==QLCG_FLAG_CHUNK?3:1.6f,false);
            else if(build_candidate(x,z)) outline(ui,x,z,ui_rgba(208,226,206,140),1,true);
        }
        outline(ui,build.cx,build.cz,ui_rgba(255,250,218,245),2,false);
    }
}

static void ghost(UiContext *ui,int cx,int cz,QlcgSettlementPart p,bool valid) {
    const QlcgBuildingModel *m=qlcg_lot_model(p.model);
    if(!m || !qlcg_extra_load(p.model) || !m->geometry) return;
    const LpGeometry *g=m->geometry;
    float wx=(cx+.5f)*32+p.x,wz=(cz+.5f)*32+p.z;
    float base=ground(wx,wz)+p.y,cs=cosf(p.yaw)*p.scale,sn=sinf(p.yaw)*p.scale;
    for(int i=0;i<g->index_count;i+=3) {
        float x[3],y[3];bool visible=true;
        for(int j=0;j<3;j++) {
            const float *v=g->positions+3*g->indices[i+j];
            if(!project(ui,wx+cs*v[0]-sn*v[2],base+p.scale*v[1],wz+sn*v[0]+cs*v[2],x+j,y+j)) {visible=false;break;}
        }
        if(visible && (x[1]-x[0])*(y[2]-y[0])<(y[1]-y[0])*(x[2]-x[0]))
            ui_draw_poly(ui,x,y,3,valid?ui_rgba(98,223,172,115):ui_rgba(230,90,66,136));
    }
}
static void render_world(UiContext *ui) {
    RegionData *r=build.region;TerrainRenderer *tr=&g_app.terrain;
    float saved[]={tr->x,tr->y,tr->z,tr->yaw,tr->pitch,tr->cam_shake};
    int season=tr->plant_season;bool force=build.world->plant_season_forced;
    GameSettings settings=g_app.settings;settings.camera_view=CAMERA_FIRST_PERSON;settings.fov=52;
    tr->x=build.fx-r->center_x;tr->z=build.fz-r->center_z;tr->yaw=build.yaw;tr->cam_shake=0;
    if(build.season>=0) {build.world->plant_season_forced=true;tr->plant_season=build.season;}
    qlcg_terrain_prepare_frame(tr,r,&settings);
    qlcg_terrain_settlement_prepare_editor(tr,r,build.world);
    float pitch=build.view==1?-1.5707f:build.view==2?-.04f:build.pitch;
    float c=cosf(build.yaw),s=sinf(build.yaw),cp=cosf(pitch),sp=sinf(pitch);
    memcpy(build.forward,(float[]){s*cp,sp,-c*cp},sizeof(build.forward));
    memcpy(build.right,(float[]){c,0,s},sizeof(build.right));
    memcpy(build.up,(float[]){-s*sp,cp,c*sp},sizeof(build.up));
    float base=ground(build.fx,build.fz);if(!isfinite(base)) base=saved[1];
    float distance=build.view==2?0:build.distance;
    if(!build.mode && build.view==0 && build.entered_ms>0 && !ui->reduce_motion) {
        float t=qlcg_clampf((float)((qlcg_time_ms()-build.entered_ms)/450.0),0,1);
        distance=qlcg_lerpf(9,distance,t*t*(3-2*t));
    }
    build.eye[0]=build.fx-distance*build.forward[0];build.eye[2]=build.fz-distance*build.forward[2];
    build.eye[1]=base-distance*sp+(build.view==2?1.65f:0);
    build.focal=ui->win_h*.5f/tanf(26*.01745329252f);
    tr->x=build.eye[0]-r->center_x;tr->y=build.eye[1];tr->z=build.eye[2]-r->center_z;tr->pitch=pitch;
    tr->lot_grid_cx=build.cx;tr->lot_grid_cz=build.cz;
    tr->lot_grid_detail=build.mode && build.grid && build.view!=2?(build.mode==BUILD_BUY || build.distance<22?2:1):0;
    tr->build_camera=true;tr->build_ortho=build.view==1?distance:0;tr->dolls_paused=true;
    CalendarClock clock=g_app.clock;
    if(build.day) clock.unix_ms+=43200000;
    ui_flush(ui);
    qlcg_terrain_render(tr,r,build.world,&clock,g_app.snap.utc_offset_min,&settings,ui->win_w,ui->win_h);
    tr->build_camera=false;tr->build_ortho=0;
    tr->x=saved[0];tr->y=saved[1];tr->z=saved[2];tr->yaw=saved[3];tr->pitch=saved[4];tr->cam_shake=saved[5];
    tr->plant_season=season;build.world->plant_season_forced=force;
}
void build_scene(UiContext *ui,bool canvas) {
    render_world(ui);
    if(build.grid && !build.mode && build.view!=2) draw_grid(ui);
    if(!canvas || build.view==2) {
        if(!ui->mouse_down) {if(build.area_drag) build.has_start=false;build.area_drag=false;}
        return;
    }
    float wx,wz;
    if(!pick_surface(ui,&wx,&wz)) return;
    int cx=(int)floorf(wx/32),cz=(int)floorf(wz/32);
    float x=wx-(cx+.5f)*32,z=wz-(cz+.5f)*32;
    if(!build.mode) {
        QlcgCivTerritory t;
        bool own=qlcg_civ_territory_at(build.world,cx,cz,&t) && t.faction==build.faction;
        uint32_t ink=own || (build.action==B_EXPAND && build_candidate(cx,cz))?ui_rgba(117,229,168,245):ui_rgba(228,132,106,220);
        outline(ui,cx,cz,ink,2,false);
        if(build.action==B_PLACE && own) {
            float sx[4],sy[4];bool visible=true;
            for(int i=0;i<4;i++) {
                float px=(cx+(i==1 || i==2))*32.f,pz=(cz+(i>=2))*32.f;
                visible&=project(ui,px,ground(px,pz)+.15f,pz,sx+i,sy+i);
            }
            if(visible) ui_draw_poly(ui,sx,sy,4,ui_rgba(118,210,160,50));
            if(build.lot_type==LOT_PRESET) {
                QlcgLot preview={0};
                if(qlcg_lot_preset(&preview,build.pack,build.seed,(int)(build.angle/90)%4)) {
                    for(int i=0;i<preview.count;i++) ghost(ui,cx,cz,preview.parts[i],true);
                    free(preview.parts);
                }
            }
        }
    }
    else if(cx==build.cx && cz==build.cz) {
        if((build.action==B_PLACE && build.mode!=BUILD_TERRAIN) || (build.action==B_MOVE && build.moving>=0)) {
            QlcgSettlementPart p={0};bool valid=build_preview(x,z,&p);ghost(ui,cx,cz,p,valid);
        } else if(build.action==B_COPY || build.action==B_REMOVE || build.action==B_MOVE) {
            const QlcgLot *lot=build_lot();int index=qlcg_lot_pick(lot,x,z,0);
            if(index>=0) ghost(ui,cx,cz,lot->parts[index],build.action!=B_REMOVE);
        } else if(build.mode==BUILD_TERRAIN && !build.prop) {
            float lastx=wx+build.brush,lastz=wz;
            for(int i=1;i<=24;i++) {
                float a=i*.26179939f,xx=wx+cosf(a)*build.brush,zz=wz+sinf(a)*build.brush;
                ground_line(ui,lastx,lastz,xx,zz,ui_rgba(240,209,120,210),1.4f,false);lastx=xx;lastz=zz;
            }
        }
        if(build.has_start || (build.area_drag && build.action==B_REMOVE)) {
            float ax=(cx+.5f)*32+build.start_x,az=(cz+.5f)*32+build.start_z;
            uint32_t ink=ui_rgba(246,207,111,230);
            if(build.mode==BUILD_TERRAIN || build.action==B_REMOVE) {
                ground_line(ui,ax,az,wx,az,ink,2,false);ground_line(ui,wx,az,wx,wz,ink,2,false);
                ground_line(ui,wx,wz,ax,wz,ink,2,false);ground_line(ui,ax,wz,ax,az,ink,2,false);
            } else ground_line(ui,ax,az,wx,wz,ink,2,false);
        }
    }
    if(build.mode && cx==build.cx && cz==build.cz && (build_line_tool() || build.action==B_REMOVE)) {
        if(ui->mouse_pressed && !build.area_drag) {
            build.press_x=ui->mouse_x;build.press_y=ui->mouse_y;
            if(build.action==B_REMOVE) {
                build.start_x=x;build.start_z=z;build.area_drag=true;
            } else if(!build.has_start) {
                build_click(cx,cz,x,z,false);build.area_drag=true;
            } else if(!ui->pointer_touch) {
                build_click(cx,cz,x,z,false);ui_pointer_consume(ui);return;
            }
        }
        if(build.area_drag) {
            if(!ui->mouse_down) {
                bool moved=hypotf((float)(ui->mouse_x-build.press_x),(float)(ui->mouse_y-build.press_y))>5*ui->met.size;
                if(moved && build.action==B_REMOVE) build_remove_area(build.start_x,build.start_z,x,z);
                else if(moved || build.action==B_REMOVE) build_click(cx,cz,x,z,false);
                build.area_drag=false;
            }
            return;
        }
    } else if(build.area_drag && !ui->mouse_down) {build.area_drag=false;build.has_start=false;return;}
    if(ui->mouse_clicked || ui->mouse_dblclicked) {
        build_click(cx,cz,x,z,ui->mouse_dblclicked || ui->touch_double_tap);
        ui_pointer_consume(ui);
    }
}
