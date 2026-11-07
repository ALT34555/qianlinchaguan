/* 舆图河道草稿编辑器 */
#include "app_state.h"
#include "systems/world/river_archive.h"
#include "atlas/atlas_tiles.h"
#include "ui/pages/game/hud_layout.h"
#include "ui/pages/game/view_atlas_layers.h"

static bool craft_allowed(void) {
    return g_app.world&&(g_app.world->opt.play_mode==PLAY_MODE_LEISURE||g_app.cheats_unlocked);
}
static void craft_clear(void) {
    g_app.river_craft.draft=(RiverEdit){0};
    g_app.river_craft.draft.cell=-1; g_app.river_craft.draft.source=-1;
    g_app.river_craft.selected=-1; g_app.river_craft.dirty=false;
    g_app.river_craft.drawing=false;
}
static void craft_message(int result) {
    static const char *keys[]={"river.saved","river.invalid","river.uphill","river.loop","river.outside","river.target","river.attached","river.memory","river.endpoint"};
    if(result<0||result>8) result=RIVER_EDIT_INVALID;
    app_push_toast(tr(keys[result]));
}
void app_river_craft_command(const char *arg) {
    while(*arg==' ') arg++;
    if(!*arg) {
        g_app.river_craft.active=false; craft_clear(); app_push_chat(tr("river.off")); return;
    }
    if(strcmp(arg,"craft")) { app_push_chat("/world.river craft | /world.river"); return; }
    if(!g_app.world||g_app.screen!=SCREEN_IN_GAME) { app_push_chat(tr("chat.world.noWorld")); return; }
    if(!craft_allowed()) { app_push_chat(tr("chat.commandsBlocked")); return; }
    if(g_app.river_craft.active) { app_push_chat(tr("river.on")); return; }
    g_app.river_craft.active=true; g_app.river_craft.tool=2; craft_clear();
    g_app.atlas.eng_tool=ENG_TOOL_NONE; g_app.terrain.eng_tool=ENG_TOOL_NONE;
    g_app.atlas.dragging=false; g_app.atlas.river_Release_pending=false;
    g_app.terrain.river_pt_count=0; g_app.view_mode=VIEW_MODE_ATLAS;
    g_app.atlas.planet_view=PLANET_VIEW_PROJECTION;
    for(int i=0;i<LAYER_ID_COUNT;i++) if(g_app.atlas.display.layers[i].id==LAYER_SOURCE_RIVERS) g_app.atlas.display.layers[i].on=true;
    app_push_chat(tr("river.on"));
}
static int nearest_node(double wx,double wz,double radius) {
    RiverEdit *e=&g_app.river_craft.draft;
    if(e->cell<0) return -1;
    SurfaceFrame f=qlcg_surface_frame(g_app.world,e->cell); SurfacePoint p=qlcg_frame_local(&f,wx,wz);
    int selected=-1;
    for(int k=0;k<e->count;k++) {
        SurfacePoint q=qlcg_frame_local(&f,e->nodes[k].wx,e->nodes[k].wz);
        double distance=hypot(p.x-q.x,p.z-q.z);
        if(distance<=radius) { selected=k; radius=distance; }
    }
    return selected;
}
void app_river_craft_stroke(double wx,double wz,double radius,bool begin,bool down) {
    RiverCraft *c=&g_app.river_craft; RiverEdit *e=&c->draft;
    if(!c->active||c->tool!=11||!craft_allowed()) return;
    if(begin) {
        if(c->dirty) { app_push_toast(tr("river.pending")); return; }
        craft_clear(); c->drawing=true; c->spacing=fmax(2,radius*.5);
        e->kind=RIVER_RECORD_SOURCE; e->cell=qlcg_world_cell_at_wxz(g_app.world,(float)wx,(float)wz);
    }
    if(!c->drawing) return;
    if(!down) c->drawing=false;
    SurfaceFrame f=qlcg_surface_frame(g_app.world,e->cell); SurfacePoint p=qlcg_frame_local(&f,wx,wz);
    if(e->count) {
        SurfacePoint q=qlcg_frame_local(&f,e->nodes[e->count-1].wx,e->nodes[e->count-1].wz);
        if(hypot(p.x-q.x,p.z-q.z)<c->spacing) return;
    }
    if(e->count==SOURCE_POINT_LIMIT) {
        int used=1; for(int k=2;k<e->count;k+=2) e->nodes[used++]=e->nodes[k];
        e->count=used; c->spacing*=2;
    }
    e->nodes[e->count++]=(RiverPathPoint){wx,wz,0,0,1,4}; c->dirty=true;
}
void app_river_craft_pointer(bool begin,bool down) {
    RiverCraft *c=&g_app.river_craft; AtlasRenderer *a=&g_app.atlas; UiContext *ui=&g_app.ui;
    if(!c->active||c->tool!=11||g_app.screen!=SCREEN_IN_GAME||g_app.view_mode!=VIEW_MODE_ATLAS) return;
    if(g_app.esc_paused||g_app.chat_open||g_app.settings_in_drawer) { c->drawing=false; return; }
    if(begin&&game_atlas_over_ui(ui)) return;
    if(!begin&&!c->drawing) return;
    double wx=0,wz=0;
    if(qlcg_atlas_screen_to_world_d(a,g_app.world,ui->mouse_x,ui->mouse_y,a->last_vp_x,a->last_vp_y,a->last_vp_w,a->last_vp_h,&wx,&wz)) {
        double scale=calc_zoom_scale(a,g_app.world,a->last_vp_w);
        app_river_craft_stroke(wx,wz,fmin(512,fmax(2,12/fmax(1e-9,scale))),begin,down);
    } else if(!down) c->drawing=false;
}
void app_river_craft_click(double wx,double wz,double radius) {
    RiverCraft *c=&g_app.river_craft; RiverEdit *e=&c->draft; MacroWorld *w=g_app.world;
    if(!c->active||!c->tool||!w||!craft_allowed()) return;
    if(c->tool==1||c->tool==2) {
        if(c->dirty) { app_push_toast(tr("river.pending")); return; }
        craft_clear();
        if(!qlcg_river_edit_pick(w,wx,wz,radius,c->tool,e)) app_push_toast(tr("river.pickEmpty"));
        return;
    }
    if(c->tool==3) {
        craft_clear(); e->kind=RIVER_RECORD_SOURCE; e->cell=qlcg_world_cell_at_wxz(w,(float)wx,(float)wz);
        e->nodes[0]=qlcg_river_edit_point(w,wx,wz,0); e->count=1; c->tool=4; c->dirty=true; return;
    }
    if(e->cell<0||!e->count) { app_push_toast(tr("river.pickEmpty")); return; }
    if(c->tool==6) {
        int k=nearest_node(wx,wz,radius);
        if(k<0) return;
        if(e->count<=1||(e->kind==RIVER_RECORD_MAIN&&(k==0||k==e->count-1))) { craft_message(RIVER_EDIT_ENDPOINT); return; }
        memmove(e->nodes+k,e->nodes+k+1,(size_t)(e->count-k-1)*sizeof(*e->nodes)); e->count--; c->dirty=true; return;
    }
    if(c->tool==5&&c->selected<0) { c->selected=nearest_node(wx,wz,radius); return; }
    if(c->tool==5) {
        int k=c->selected; c->selected=-1;
        if(k<0||k>=e->count) return;
        if(e->kind==RIVER_RECORD_MAIN&&(k==0||k==e->count-1)) { craft_message(RIVER_EDIT_ENDPOINT); return; }
        e->nodes[k].wx=wx; e->nodes[k].wz=wz; c->dirty=true; return;
    }
    if(c->tool==4) {
        RiverPathPoint point=qlcg_river_edit_point(w,wx,wz,e->source<0&&e->kind==RIVER_RECORD_SOURCE?radius:0);
        if(e->count>=(e->kind==RIVER_RECORD_MAIN?161:SOURCE_POINT_LIMIT)) { craft_message(RIVER_EDIT_INVALID); return; }
        int at=e->count;
        if(e->kind==RIVER_RECORD_MAIN||e->source>=0) {
            SurfaceFrame f=qlcg_surface_frame(w,e->cell); SurfacePoint p=qlcg_frame_local(&f,point.wx,point.wz); double best=INFINITY;
            for(int k=1;k<e->count;k++) {
                SurfacePoint a=qlcg_frame_local(&f,e->nodes[k-1].wx,e->nodes[k-1].wz),b=qlcg_frame_local(&f,e->nodes[k].wx,e->nodes[k].wz);
                double dx=b.x-a.x,dz=b.z-a.z,t=fmax(0,fmin(1,((p.x-a.x)*dx+(p.z-a.z)*dz)/fmax(1e-9,dx*dx+dz*dz)));
                double d=hypot(p.x-a.x-dx*t,p.z-a.z-dz*t); if(d<best) {
                    best=d; at=k;
                    const RiverPathPoint *u=e->nodes+k-1,*v=e->nodes+k;
                    point.arc=u->arc+(v->arc-u->arc)*t; point.level=u->level+(v->level-u->level)*t;
                    point.depth=u->depth+(v->depth-u->depth)*t; point.half_width=u->half_width+(v->half_width-u->half_width)*t;
                }
            }
        }
        memmove(e->nodes+at+1,e->nodes+at,(size_t)(e->count-at)*sizeof(*e->nodes));
        e->nodes[at]=point; e->count++; c->dirty=true;
    }
}
static UiRect craft_rect(UiContext *ui) {
    float z=ui->met.size;
    return (UiRect){ui->safe_x_px+12*z,ui->safe_y_px+game_top_bar_height(ui)+game_atlas_tools_height(ui)+12*z,
        fminf(400*z,ui->win_w-ui->safe_x_px*2-24*z),192*z};
}
bool app_river_craft_over_ui(UiContext *ui) {
    if(!g_app.river_craft.active) return false;
    UiRect r=craft_rect(ui); return ui_hit(ui,r.x,r.y,r.w,r.h);
}
static void craft_apply(int remove) {
    RiverCraft *c=&g_app.river_craft; RiverEdit *e=&c->draft;
    if(!craft_allowed()) { app_push_toast(tr("chat.commandsBlocked")); return; }
    if(!remove&&!c->dirty&&e->count) { craft_message(RIVER_EDIT_OK); return; }
    int status=remove?RIVER_EDIT_OK:qlcg_river_craft_prepare(g_app.world,e);
    if(!status) status=e->kind==RIVER_RECORD_MAIN?(remove?RIVER_EDIT_INVALID:qlcg_river_edit_main(g_app.world,e)):
        qlcg_river_craft_source(g_app.world,e,remove);
    craft_message(status);
    if(!status) {
        qlcg_atlas_tiles_invalidate(g_app.atlas.tiles);
        craft_clear(); c->tool=2;
    }
}
void app_river_craft_draw(UiContext *ui,float x,float y,float width,float height) {
    RiverCraft *c=&g_app.river_craft; if(!c->active) return;
    RiverEdit *e=&c->draft; float z=ui->met.size,font=12*ui->met.scale;
    ui_clip_push(ui,x,y,width,height);
    float previous_x=0,previous_y=0; bool previous=false;
    for(int k=0;k<e->count;k++) {
        float sx=0,sy=0; bool visible=qlcg_atlas_world_to_screen_vis(&g_app.atlas,g_app.world,e->nodes[k].wx,e->nodes[k].wz,x,y,width,height,&sx,&sy);
        if(visible&&previous&&fabsf(sx-previous_x)<width&&fabsf(sy-previous_y)<height)
            ui_draw_line(ui,previous_x,previous_y,sx,sy,2*z,ui_rgba(237,151,40,255));
        if(visible) {
            float r=(k==c->selected?5:k==0?4:2.5f)*z;
            ui_draw_rect(ui,sx-r,sy-r,r*2,r*2,k==c->selected?ui_rgba(255,255,255,255):ui_rgba(237,151,40,255));
        }
        previous=visible; previous_x=sx; previous_y=sy;
    }
    ui_clip_pop(ui);
    UiRect r=craft_rect(ui); ui_draw_rect(ui,r.x,r.y,r.w,r.h,tk_of(ui,QL_SURFACE_HI));
    ui_draw_border(ui,r.x,r.y,r.w,r.h,z,tk_of(ui,QL_LINE));
    char line[160]; snprintf(line,sizeof(line),tr(c->dirty?"river.titleDirty":"river.title"),e->count);
    ui_draw_text_px(ui,UI_FACE_BODY,font,false,r.x+10*z,r.y+8*z,line,tk_of(ui,QL_INK));
    static const char *keys[]={"river.pan","river.main","river.source","river.new","river.addNode","river.moveNode","river.dropNode","river.apply","river.delete","river.cancel","river.exit","river.stroke"};
    float gap=5*z,bw=(r.w-25*z)/4,bh=28*z;
    for(int i=0;i<12;i++) {
        float bx=r.x+5*z+(i%4)*(bw+gap),by=r.y+32*z+(i/4)*(bh+gap);
        if(ui_btn(ui,bx,by,bw,bh,tr(keys[i]),i==c->tool?BTN_PRIMARY:BTN_GHOST,false,font,false)) {
            if(i<=6||i==11) {
                if((i==3||i==11)&&c->dirty) app_push_toast(tr("river.pending"));
                else { c->tool=i; c->selected=-1; }
            } else if(i==7) craft_apply(0);
            else if(i==8) craft_apply(1);
            else if(i==9) { craft_clear(); c->tool=2; }
            else app_river_craft_command("");
        }
    }
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,font,false,r.x+10*z,r.y+138*z,r.w-20*z,0,
        tr(c->tool==11?"river.strokeHint":c->tool==5?(c->selected<0?"river.movePick":"river.movePlace"):c->tool==4?"river.addHint":"river.hint"),tk_of(ui,QL_INK_2));
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,font,false,r.x+10*z,r.y+163*z,r.w-20*z,0,tr("river.saveHint"),tk_of(ui,QL_INK_2));
}
