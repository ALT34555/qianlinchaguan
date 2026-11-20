#include "app/app_state.h"
#include "hud_layout.h"
#include "app/app_doll_control.h"
#include "ui/ui_sprite.h"
#include "ui/ui_scroll.h"
#include "systems/dolls/doll_life.h"
#include "systems/world/faction_naming.h"
#include "systems/world/world_infinite.h"
#include "terrain3d/terrain_dolls.h"

static const float inspect_reach=QLCG_DOLL_INTERACT_REACH;

static bool can_inspect(void) {
    return g_app.screen==SCREEN_IN_GAME&&g_app.view_mode==VIEW_MODE_TERRAIN&&g_app.world&&g_app.region&&
        g_app.world->opt.artificial_enabled&&!g_app.esc_paused&&!g_app.settings_in_drawer&&!g_app.chat_open&&
        !g_app.inventory_ui.open&&!g_app.touch_layout_edit&&!settings_confirm_active();
}

static const QlcgDollLifeResident *find_resident(const TerrainDollRef *ref,const TerritoryFaction **out_faction,
                                                const QlcgDollLifeCamp **out_camp) {
    if(!ref->id||!ref->camp_uid[0]) return NULL;
    const MacroWorld *owner=qlcg_infinite_find(g_app.world,ref->plane_x,ref->plane_z);
    if(!owner||!owner->opt.artificial_enabled) return NULL;
    for(int i=0;i<owner->faction_count;i++) {
        const TerritoryFaction *faction=owner->factions+i;
        if(!faction->active||strncmp(faction->uid,ref->camp_uid,sizeof(ref->camp_uid))) continue;
        const QlcgDollLifeResident *resident=qlcg_doll_life_find(owner,faction,ref->id);
        if(!resident) return NULL;
        if(out_faction) *out_faction=faction;
        if(out_camp) *out_camp=qlcg_doll_life_camp(owner,faction);
        return resident;
    }
    return NULL;
}

static bool selected(void) {
    return g_app.doll_panel.resident.id&&g_app.doll_panel.world==g_app.world&&g_app.doll_panel.region==g_app.region;
}

enum { DOLL_GIFTS_SCROLL=0xD01102, DOLL_INFO_SCROLL=0xD01103, DOLL_NAME_INPUT=0xD01104 };
typedef enum { DOLL_MENU, DOLL_GIFTS, DOLL_SWITCH_CONFIRM, DOLL_STATUS } DollPanelPage;
static DollPanelPage page;
static float anchor_x,anchor_y;
static bool editing_name;
static char name_draft[QLCG_DOLL_NAME_BYTES];

bool game_doll_panel_open(void) {
    return g_app.screen==SCREEN_IN_GAME && g_app.view_mode==VIEW_MODE_TERRAIN && selected();
}

bool game_doll_panel_close(void) {
    bool open=selected();
    if (g_app.ui.input_id==DOLL_NAME_INPUT) app_text_focus_clear();
    g_app.doll_panel.resident=(TerrainDollRef){0};
    g_app.doll_panel.world=NULL;
    g_app.doll_panel.region=NULL;
    page=DOLL_MENU;
    return open;
}

bool game_doll_panel_back(void) {
    if (!game_doll_panel_open()) return false;
    if (page==DOLL_MENU) return game_doll_panel_close();
    if (g_app.ui.input_id==DOLL_NAME_INPUT) app_text_focus_clear();
    page=DOLL_MENU;
    return true;
}

static UiRect inspect_button(const UiContext *ui) {
    float size=ui->met.size,w=fminf(150*size,ui->win_w*.70f);
    return (UiRect){(ui->win_w-w)*.5f,ui->win_h*.58f,w,40*size};
}

static UiRect panel_rect(UiContext *ui) {
    float z=ui->met.size, left=ui->safe_edges[0], right=ui->win_w-ui->safe_edges[2];
    float top=ui->safe_edges[1], bottom=ui->win_h-ui->safe_edges[3];
    float w=fminf(418*z,(right-left)*.94f);
    float h=fminf((page==DOLL_SWITCH_CONFIRM?172:380)*z,(bottom-top)*.94f);
    return (UiRect){(left+right-w)*.5f,(top+bottom-h)*.5f,w,h};
}

static bool aim_has_resident(void) {
    const AimProbeResult *aim=&g_app.terrain.aim;
    return (!g_app.terrain.external_camera || g_app.terrain.camera_reticle_visible) &&
           aim->kind==AIM_HIT_DOLL && aim->dist_m<=inspect_reach && aim->doll.id;
}

bool game_doll_panel_over_ui(UiContext *ui) {
    if (!can_inspect()) return false;
    if (selected()) return true;
    if (!g_app.settings.touch_controls || !aim_has_resident()) return false;
    UiRect r=inspect_button(ui);
    return ui_pointer_in_rect(ui,r.x,r.y,r.w,r.h);
}

bool game_doll_panel_interact(void) {
    if (!can_inspect() || selected() ||
        (g_app.terrain.external_camera && !g_app.terrain.camera_reticle_visible)) return false;
    TerrainToolHit hit;
    if (!qlcg_terrain_tool_target(&g_app.terrain,g_app.region,g_app.world,inspect_reach,&hit) || hit.kind!=AIM_HIT_DOLL)
        return false;
    if (!app_doll_interact(hit.doll)) return false;
    g_app.doll_panel.world=g_app.world;
    g_app.doll_panel.region=g_app.region;
    g_app.doll_panel.resident=hit.doll;
    page=DOLL_MENU;
    anchor_x=g_app.terrain.external_camera?g_app.terrain.camera_reticle[0]:.5f;
    anchor_y=g_app.terrain.external_camera?g_app.terrain.camera_reticle[1]:.5f;
    app_key_track_clear();
    app_set_mouse_lock(false);
    return true;
}

void game_doll_panel_input(UiContext *ui) {
    if (!can_inspect() || selected() || !g_app.settings.touch_controls || !aim_has_resident()) return;
    UiRect r=inspect_button(ui);
    bool blocked=ui->input_blocked;
    if (game_doll_panel_over_ui(ui) && !game_chat_over_ui(ui) && !game_debug_over_ui(ui) && !ui->toast_blocked)
        ui->input_blocked=false;
    bool clicked=ui_button(ui,r.x,r.y,r.w,r.h,tr("doll.inspect"),false,true);
    const SpriteDef *sprite=ui_sprite_by_name("btn-interact");
    float icon=r.h*.73f;
    if (sprite) ui_sprite_icon4(ui,sprite,r.x+r.h*.14f,r.y+(r.h-icon)*.5f,icon,
                                tk_of(ui,QL_SURFACE),tk_of(ui,QL_PRIMARY_UI),tk_of(ui,QL_ACCENT_UI));
    ui->input_blocked=blocked;
    if (clicked) { game_doll_panel_interact(); ui_pointer_consume(ui); }
}

static int need_percent(int value) { return (100*value+QLCG_LIFE_NEED_MAX/2)/QLCG_LIFE_NEED_MAX; }

static const char *task_name(int task) {
    static const char *const keys[]={"doll.task.routine","doll.task.rest","doll.task.meal","doll.task.tea",
        "doll.task.harvest","doll.task.shift"};
    return task>=0&&task<DOLL_LIFE_TASK_COUNT?tr(keys[task]):tr("doll.target.none");
}

static const char *reason_name(int reason) {
    static const char *const keys[]={"doll.reason.schedule","doll.reason.tired","doll.reason.hungry",
        "doll.reason.lonely","doll.reason.crop","doll.reason.cover"};
    return reason>=0&&reason<DOLL_LIFE_REASON_COUNT?tr(keys[reason]):tr("doll.target.none");
}

static const char *target_name(int target) {
    static const char *const keys[]={"doll.target.none","doll.target.home","doll.target.hearth","doll.target.chair",
        "doll.target.field","doll.target.watch","doll.target.table","doll.target.granary"};
    return target>=0&&target<DOLL_LIFE_TARGET_COUNT?tr(keys[target]):tr("doll.target.none");
}

static const char *routine_name(int state) {
    static const char *const keys[]={"doll.state.walk","doll.state.work","doll.state.fire","doll.state.sleep",
        "doll.state.watch","doll.state.approach","doll.state.return","doll.state.protect","doll.state.patrol"};
    return state>=0&&state<(int)(sizeof(keys)/sizeof(keys[0]))?tr(keys[state]):task_name(DOLL_LIFE_ROUTINE);
}

static void draw_need(UiContext *ui,UiRect r,float y,float line,float font,const char *label,int value) {
    float z=ui->met.size,gap=12*z;
    float tx=r.x+gap,bar_x=r.x+r.w*.48f,bar_w=r.w*.33f;
    float bar_h=fmaxf(2,line*.22f),bar_y=y+(line-bar_h)*.5f;
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,font,false,tx,y,bar_x-tx-5*z,0,label,ui->pal.text_sub);
    ui_draw_rect(ui,bar_x,bar_y,bar_w,bar_h,ui->pal.border_soft);
    ui_draw_rect(ui,bar_x,bar_y,bar_w*value/100,bar_h,ui->pal.accent_tea);
    char percent[12];snprintf(percent,sizeof(percent),"%d%%",value);
    ui_draw_text_px_right(ui,UI_FACE_MONO,font,false,r.x+r.w-gap,y,percent,ui->pal.text_main);
}

typedef struct { int count; InventoryPosition first; } GiftChoice;

static void add_choice(GiftChoice choices[], InventoryItem item, InventoryPosition pos) {
    if (item<=INV_ITEM_NONE || item>=INV_ITEM_COUNT) return;
    if (!choices[item].count) choices[item].first=pos;
    choices[item].count++;
}

static void gift_list(UiContext *ui,UiRect r,TerrainDollRef ref,const Inventory *recipient) {
    const Inventory *inv=&g_app.world->inventory;
    GiftChoice choices[INV_ITEM_COUNT]={0};
    const int grids[]={INV_GRID_POCKET,INV_GRID_PACK};
    for (int g=0;g<2;g++) {
        int w,h,grid=grids[g];
        inventory_grid_dimensions(inv,grid,&w,&h);
        for (int y=0;y<h;y++) for (int x=0;x<w;x++)
            add_choice(choices,inventory_grid_item(inv,grid,x,y),(InventoryPosition){x,y,grid});
    }
    for (int slot=0;slot<INV_SLOT_COUNT;slot++) {
        InventoryItem item=inventory_equipped(inv,(InventorySlot)slot);
        if (!item) continue;
        Inventory copy=*inv;
        InventoryPosition pos={0,0,slot};
        if (inventory_discard(&copy,pos)) add_choice(choices,item,pos);
    }
    float z=ui->met.size, row=fmaxf(40*z,18*ui->met.scale), gap=4*z;
    float offset=ui_scroll_begin(ui,DOLL_GIFTS_SCROLL,r.x,r.y,r.w,r.h),y=r.y-offset;
    int lines=0;
    for (int id=1;id<INV_ITEM_COUNT;id++) {
        if (!choices[id].count) continue;
        Inventory pocket=*recipient;
        unsigned available=inventory_add_count(&pocket,(InventoryItem)id,(unsigned)choices[id].count);
        if (!available) continue;
        if (y+row>r.y && y<r.y+r.h) {
            const char *name=tr(inventory_item_info((InventoryItem)id)->name_key);
            char label[160];
            snprintf(label,sizeof(label),"%s  ×%u",name,available);
            bool picked=ui_button(ui,r.x,y,r.w,row,label,false,false);
            if (game_inventory_atlas_ready(ui)) {
                float uv[4],sz=row-6*z;
                game_inventory_tile((InventoryItem)id,false,false,uv);
                ui_use_sprite_tex(ui,ui->inventory_tex);
                ui_draw_sprite_rect(ui,r.x+4*z,y+3*z,sz,sz,uv[0],uv[1],uv[2],uv[3],ui_rgba(255,255,255,255));
            }
            if (picked) {
                bool ok=app_doll_give(ref,choices[id].first);
                if (ok) snprintf(label,sizeof(label),tr("doll.given"),name);
                app_push_toast_k(ok?UI_TOAST_SUCCESS:UI_TOAST_WARNING,NULL,ok?label:tr("doll.giveFailed"));
                ui_pointer_consume(ui);
            }
        }
        ++lines;
        y+=row+gap;
    }
    if (!lines) ui_draw_text_ellipsis(ui,UI_FACE_BODY,12*ui->met.scale,false,r.x,r.y,r.w,1,tr("doll.noItems"),ui->pal.text_sub);
    ui_scroll_end(ui,DOLL_GIFTS_SCROLL,fmaxf(0,lines*(row+gap)-gap));
}

static void interaction_menu(UiContext *ui,const char *identity,bool leisure) {
    static const struct { const char *label; DollPanelPage page; } actions[]={
        {"doll.give",DOLL_GIFTS},{"doll.possess",DOLL_SWITCH_CONFIRM},{"doll.viewStatus",DOLL_STATUS}
    };
    int count=leisure?3:1;
    float z=ui->met.size,margin=8*z;
    float left=ui->safe_edges[0]+margin,right=ui->win_w-ui->safe_edges[2]-margin;
    float top=ui->safe_edges[1]+margin,bottom=ui->win_h-ui->safe_edges[3]-margin;
    float w=fminf(246*z,right-left),available=bottom-top;
    if (w<=0 || available<=0) return;
    float pad=fminf(10*z,fminf(w,available)*.08f),gap=pad*.5f;
    float px=14*ui->met.scale;
    float row=fminf(ui_ctl_height(38*z,px),(available-2*pad-count*gap)/(count+1));
    px=fminf(px,row*.48f);
    float h=2*pad+(count+1)*row+count*gap;
    float target_x=anchor_x*ui->win_w;
    float x=target_x+2*margin;
    if (x+w>right) x=target_x-w-2*margin;
    x=qlcg_clampf(x,left,right-w);
    float y=qlcg_clampf(anchor_y*ui->win_h-h*.5f,top,bottom-h);

    ui_draw_rect(ui,0,0,(float)ui->win_w,(float)ui->win_h,ui_rgba(0,0,0,55));
    ui_panel(ui,x,y,w,h,8*z,with_alpha(ui->pal.bg_card,.99f),ui->pal.border_soft,ui_rgba(0,0,0,60));
    ui_draw_rect(ui,x,y,3*z,h,tk_of(ui,QL_PRIMARY_UI));
    x+=pad; y+=pad; w-=2*pad;
    const char *close=tr("doll.close");
    float close_w=fminf(w*.38f,ui_text_width_px(ui,UI_FACE_BODY,px,false,close)+18*z);
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,px,true,x,y+(row-px*1.4f)*.5f,w-close_w-gap,0,identity,ui->pal.text_main);
    ui_clip_push(ui,x+w-close_w,y,close_w,row);
    bool closed=ui_btn(ui,x+w-close_w,y,close_w,row,close,BTN_QUIET,false,px,false);
    ui_clip_pop(ui);
    if (closed) {
        game_doll_panel_close();ui_pointer_consume(ui);return;
    }
    for (int i=0;i<count;i++) {
        y+=row+gap;
        if (ui_btn(ui,x,y,w,row,tr(actions[i].label),BTN_GHOST,true,px,false)) {
            page=actions[i].page;
            if (page==DOLL_GIFTS) ui_scroll_reset(ui,DOLL_GIFTS_SCROLL);
            if (page==DOLL_STATUS) ui_scroll_reset(ui,DOLL_INFO_SCROLL);
            ui_pointer_consume(ui);
        }
    }
}

static void name_row(UiContext *ui,TerrainDollRef ref,const char *identity,
                     float x,float y,float text_w,float btn,float gap,float close_w,float px) {
    float edit_w=text_w-close_w-gap;
    if (!editing_name) {
        ui_draw_text_ellipsis(ui,UI_FACE_BODY,px,false,x,y+(btn-px*1.4f)*.5f,edit_w,0,identity,tk_of(ui,QL_PRIMARY_TEXT));
        if (ui_button(ui,x+edit_w+gap,y,close_w,btn,tr("doll.name"),false,false)) {
            editing_name=true;
            ui_pointer_consume(ui);
        }
        return;
    }
    ui_text_input(ui,DOLL_NAME_INPUT,x,y,edit_w,btn,name_draft,sizeof(name_draft),40,tr("doll.nameHint"));
    const char *name=name_draft;
    while (*name) {
        if (*name==' ' || *name=='\t') name++;
        else if (!strncmp(name,"　",3)) name+=3;
        else break;
    }
    if (ui_button(ui,x+edit_w+gap,y,close_w,btn,tr("game.pause.save"),false,false) && *name) {
        bool ok=app_doll_rename(ref,name);
        if (ok) { editing_name=false; app_text_focus_clear(); }
        app_push_toast_k(ok?UI_TOAST_SUCCESS:UI_TOAST_WARNING,NULL,tr(ok?"doll.named":"doll.unavailable"));
        ui_pointer_consume(ui);
    }
}

static void status_details(UiContext *ui,UiRect r,const TerrainDollRef *ref,const TerritoryFaction *faction,
                           const QlcgDollLifeCamp *camp,const QlcgDollLifeResident *resident) {
    float x=r.x,text_w=r.w,line=fmaxf(18*ui->met.size,16*ui->met.scale),font=fminf(13*ui->met.scale,line*.69f);
    float y=r.y-ui_scroll_begin(ui,DOLL_INFO_SCROLL,r.x,r.y,r.w,r.h),info_start=y;
    uint32_t ink=ui->pal.text_main,muted=ui->pal.text_sub;
    char text[384];
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,font,false,x,y,text_w,0,qlcg_faction_display_name(faction),muted);y+=line;
    int watch_state;float watch_position[3];
    bool watching=qlcg_terrain_dolls_watch_response(&g_app.terrain,ref,&watch_state,watch_position);
    const char *work=watching?routine_name(watch_state):resident->task==DOLL_LIFE_ROUTINE?
        routine_name(resident->routine_state):task_name(resident->task);
    snprintf(text,sizeof(text),tr("doll.doing"),work);
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,font,true,x,y,text_w,0,text,ink);y+=line;
    snprintf(text,sizeof(text),tr("doll.reason"),reason_name(resident->reason));
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,font,false,x,y,text_w,0,text,muted);y+=line;
    const char *target=target_name(resident->target);
    if(resident->task==DOLL_LIFE_TEA&&resident->target==DOLL_LIFE_CHAIR&&!resident->moving&&camp->chair_owner==resident->id&&
       hypotf(resident->x-camp->chair_x,resident->z-camp->chair_z)<.08f)
        snprintf(text,sizeof(text),tr("doll.seated"),target);
    else snprintf(text,sizeof(text),tr(resident->moving?"doll.going":"doll.target"),target);
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,font,false,x,y,text_w,0,text,muted);y+=line;
    const char *needs[]={tr("doll.satiety"),tr("doll.energy"),tr("doll.social")};
    int values[]={resident->satiety,resident->energy,resident->social};
    for(int i=0;i<3;i++,y+=line) draw_need(ui,r,y,line,font,needs[i],need_percent(values[i]));
    if(resident->partner_id) snprintf(text,sizeof(text),tr("doll.partner"),resident->partner_id,need_percent(resident->closeness));
    else snprintf(text,sizeof(text),"%s",tr("doll.noPartner"));
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,font,false,x,y,text_w,0,text,muted);y+=line;
    double px=(faction->cx+.5)*(double)CHUNK_SIZE+resident->x;
    double pz=(faction->cz+.5)*(double)CHUNK_SIZE+resident->z;
    if(qlcg_world_flat(g_app.world)) {
        double stride=g_app.world->circumference+(double)g_app.world->circumference/g_app.world->columns;
        px+=((double)ref->plane_x-g_app.world->opt.plane_x)*stride;
        pz+=((double)ref->plane_z-g_app.world->opt.plane_z)*stride;
    } else {
        double player_x=(double)g_app.region->center_x+g_app.terrain.x;
        px=player_x+remainder(px-player_x,g_app.world->circumference);
    }
    float py;
    if(watching) {
        px=(double)g_app.region->center_x+watch_position[0];py=watch_position[1];
        pz=(double)g_app.region->center_z+watch_position[2];
    } else py=qlcg_region_height_at(g_app.region,(float)(px-g_app.region->center_x),(float)(pz-g_app.region->center_z));
    if(isfinite(py)) snprintf(text,sizeof(text),tr("doll.position"),px,(double)py,pz);
    else snprintf(text,sizeof(text),tr("doll.positionGround"),px,pz);
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,font,false,x,y,text_w,0,text,muted);y+=line;
    if(resident->resume_task!=resident->task) {
        snprintf(text,sizeof(text),tr("doll.resume"),task_name(resident->resume_task));
        ui_draw_text_ellipsis(ui,UI_FACE_BODY,font,false,x,y,text_w,0,text,muted);
    }
    y+=line;
    ui_scroll_end(ui,DOLL_INFO_SCROLL,y-info_start);
}

void game_doll_panel_draw(UiContext *ui) {
    if (!selected()) {
        game_doll_panel_close();
        if (can_inspect() && !g_app.settings.touch_controls && aim_has_resident()) {
            const char *hint=tr("doll.inspectClick");
            float z=ui->met.size,px=13*ui->met.scale;
            float w=ui_text_width_px(ui,UI_FACE_BODY,px,true,hint)+20*z,h=px*1.45f+6*z;
            float cx=g_app.terrain.external_camera?g_app.terrain.camera_reticle[0]*ui->win_w:ui->win_w*.5f;
            float cy=g_app.terrain.external_camera?g_app.terrain.camera_reticle[1]*ui->win_h:ui->win_h*.5f;
            float x=qlcg_clampf(cx-w*.5f,ui->safe_edges[0],ui->win_w-ui->safe_edges[2]-w);
            float y=qlcg_clampf(cy+12*z,ui->safe_edges[1],ui->win_h-ui->safe_edges[3]-h);
            ui_draw_rect(ui,x,y,w,h,with_alpha(ui->pal.bg_card,.96f));
            ui_draw_border(ui,x,y,w,h,1,ui->pal.border_soft);
            ui_draw_text_px_center(ui,UI_FACE_BODY,px,true,x+w*.5f,y+3*z,hint,ui->pal.text_main);
        }
        return;
    }
    if (!can_inspect()) { game_doll_panel_close(); return; }
    const TerritoryFaction *faction=NULL;
    const QlcgDollLifeCamp *camp=NULL;
    const TerrainDollRef *ref=&g_app.doll_panel.resident;
    const QlcgDollLifeResident *resident=find_resident(ref,&faction,&camp);
    if (!resident||!camp) { game_doll_panel_close(); return; }
    const QlcgDollType *type=qlcg_doll_type_for(resident->role,resident->id);
    const char *role=type?qlcg_i18n_content_text(type->name):tr("doll.resident");
    char identity[384];
    if (resident->name[0]) snprintf(identity,sizeof(identity),"%s · %s",resident->name,role);
    else snprintf(identity,sizeof(identity),"%s · %s",role,tr("doll.unnamed"));
    bool leisure=g_app.world->opt.play_mode==PLAY_MODE_LEISURE;
    if (!leisure && (page==DOLL_STATUS || page==DOLL_SWITCH_CONFIRM)) {
        game_doll_panel_back();
        ui_pointer_consume(ui);
    }
    if (page==DOLL_MENU) {
        interaction_menu(ui,identity,leisure);
        if (page==DOLL_STATUS || page==DOLL_GIFTS) {
            editing_name=false;
            snprintf(name_draft,sizeof(name_draft),"%s",resident->name);
        }
        return;
    }
    UiRect r=panel_rect(ui);
    float z=ui->met.size,gap=fminf(10*z,r.h*.035f),btn=fminf(38*z,r.h*.23f);
    float px=fminf(14*ui->met.scale,btn*.48f);
    float x=r.x+gap,y=r.y+gap,w=r.w-2*gap,close_w=fminf(76*z,w*.28f);
    ui_draw_rect(ui,0,0,(float)ui->win_w,(float)ui->win_h,ui_rgba(0,0,0,76));
    ui_panel(ui,r.x,r.y,r.w,r.h,8*z,with_alpha(ui->pal.bg_card,.99f),ui->pal.border_soft,ui_rgba(0,0,0,60));
    ui_draw_rect(ui,r.x,r.y,3*z,r.h,ui_rgb_hex(faction->main_color,255));
    bool confirm=page==DOLL_SWITCH_CONFIRM;
    const char *title=tr(confirm?"doll.possess":page==DOLL_STATUS?"doll.viewStatus":"doll.give");
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,px*1.18f,true,x,y,w-(confirm?0:close_w+gap),0,title,ui->pal.text_main);
    if (!confirm && ui_button(ui,x+w-close_w,y,close_w,btn,tr("doll.back"),false,false)) {
        game_doll_panel_back();ui_pointer_consume(ui);return;
    }
    y+=btn+gap;
    if (confirm) {
        ui_draw_text_ellipsis(ui,UI_FACE_BODY,px,false,x,y,w,0,identity,tk_of(ui,QL_PRIMARY_TEXT));
        y+=px*1.5f+gap;
        float footer=r.y+r.h-gap-btn,half=(w-gap)*.5f;
        ui_draw_text_ellipsis(ui,UI_FACE_BODY,px,true,x,y,w,0,tr("doll.switchPrompt"),ui->pal.text_main);
        if (ui_button(ui,x,footer,half,btn,tr("doll.no"),false,false)) {
            game_doll_panel_back();
            ui_pointer_consume(ui);
        } else if (ui_button(ui,x+half+gap,footer,half,btn,tr("settings.confirm.yes"),true,false)) {
            if (app_doll_possess(*ref)) game_doll_panel_close();
            else {
                game_doll_panel_back();
                app_push_toast_k(UI_TOAST_WARNING,NULL,tr("doll.unavailable"));
            }
            ui_pointer_consume(ui);
        }
        return;
    }
    name_row(ui,*ref,identity,x,y,w,btn,gap,close_w,px);
    y+=btn+gap;
    if (page==DOLL_GIFTS) {
        ui_draw_text_ellipsis(ui,UI_FACE_BODY,px,false,x,y,w,0,tr("doll.chooseGift"),ui->pal.text_sub);
        y+=px*1.5f+gap;
    }
    UiRect body={x,y,w,r.y+r.h-gap-y};
    if (body.h<=0) return;
    if (page==DOLL_GIFTS) gift_list(ui,body,*ref,&resident->inventory);
    else status_details(ui,body,ref,faction,camp,resident);
}
