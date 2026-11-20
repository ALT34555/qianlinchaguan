#include "build_internal.h"
#include "systems/world/world_infinite.h"
#include "ui/ui_sprite.h"
#include "systems/world/model/plant_material.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

BuildEditor build;
typedef struct {
    int cx,cz;
    bool had_lot;
    QlcgLot lot;
    ChdLotSnapshot *ground;
} BuildAction;
static BuildAction history[32];
static int history_count,cursor;

const QlcgLot *build_lot(void) {return qlcg_lot_at(build.world,build.cx,build.cz);}
bool game_build_open(void) {return build.region && build.world==g_app.world && g_app.screen==SCREEN_IN_GAME;}
static void dispose(BuildAction *a) {free(a->lot.parts);qlcg_chd_lot_dispose(a->ground);*a=(BuildAction){0};}
static bool capture(BuildAction *a,bool terrain) {
    const QlcgLot *old=build_lot();
    *a=(BuildAction){.cx=build.cx,.cz=build.cz,.had_lot=old!=NULL};
    if(!qlcg_lot_copy(&a->lot,old)) return false;
    if(terrain && !(a->ground=qlcg_chd_lot_snapshot(build.world,a->cx,a->cz))) {dispose(a);return false;}
    return true;
}
static void remember(BuildAction a) {
    while(history_count>cursor) dispose(history+--history_count);
    if(history_count==32) {
        dispose(history);memmove(history,history+1,31*sizeof(*history));history_count--;
    }
    history[history_count++]=a;cursor=history_count;
    qlcg_region_stream_sync_changed(build.region,build.region->climate_season,&g_app.settings);
}
void build_history(bool redo) {
    if(redo?cursor==history_count:cursor==0) return;
    BuildAction *a=history+(redo?cursor:cursor-1);
    const QlcgLot *old=qlcg_lot_at(build.world,a->cx,a->cz);
    bool had=old!=NULL;
    QlcgLot current;
    if(!qlcg_lot_copy(&current,old)) return;
    if(!qlcg_lot_commit(build.world,a->cx,a->cz,a->had_lot?&a->lot:NULL)) {free(current.parts);return;}
    if(a->ground) qlcg_chd_lot_swap(build.world,a->cx,a->cz,a->ground);
    free(a->lot.parts);a->lot=current;a->had_lot=had;cursor+=redo?1:-1;
    build.moving=-1;build.has_start=false;
    if(build.mode && (!build_lot() || build_lot()->type!=LOT_CUSTOM)) build_mode(BUILD_MACRO);
    qlcg_region_stream_sync_changed(build.region,build.region->climate_season,&g_app.settings);
    snprintf(build.message,sizeof(build.message),"%s",redo?"已重做":"已撤销");
}
static bool can_enter(void) {
    return g_app.screen==SCREEN_IN_GAME && g_app.view_mode==VIEW_MODE_TERRAIN && g_app.world && g_app.region &&
        g_app.world->opt.play_mode==PLAY_MODE_LEISURE && g_app.world->opt.artificial_enabled &&
        !g_app.esc_paused && !g_app.chat_open && !g_app.inventory_ui.open && !g_app.settings_in_drawer &&
        !g_app.touch_layout_edit && !game_doll_panel_open() && !settings_confirm_active() &&
        g_app.terrain.eng_tool==ENG_TOOL_NONE && (!g_app.terrain.external_camera || g_app.terrain.camera_reticle_visible);
}
static const TerritoryFaction *flag_owner(float wx,float wz) {
    QlcgCivTerritory here,flag;
    const RegionData *r=g_app.region;
    int px=r->center_cx+(int)floorf((g_app.terrain.x+16)/32),pz=r->center_cz+(int)floorf((g_app.terrain.z+16)/32);
    if(!qlcg_civ_territory_at(g_app.world,px,pz,&here) || !here.faction->is_player ||
       !qlcg_civ_territory_at(g_app.world,(int)floorf(wx/32),(int)floorf(wz/32),&flag) ||
       flag.type!=QLCG_FLAG_CHUNK || flag.faction!=here.faction) return NULL;
    return flag.faction;
}
static bool prompted(void) {
    AimProbeResult *aim=&g_app.terrain.aim;
    return can_enter() && aim->kind==AIM_HIT_FLAG && aim->dist_m<=QLCG_FLAG_INTERACT_REACH && flag_owner(aim->wx,aim->wz);
}
static UiRect prompt_rect(UiContext *ui) {
    float w=fminf(164*ui->met.size,ui->win_w*.7f),h=38*ui->met.size;
    float x=g_app.terrain.external_camera?g_app.terrain.camera_reticle[0]:.5f;
    float y=g_app.terrain.external_camera?g_app.terrain.camera_reticle[1]:.5f;
    return (UiRect){qlcg_clampf(x*ui->win_w-w*.5f,0,ui->win_w-w),fminf(ui->win_h-h,y*ui->win_h+24*ui->met.size),w,h};
}
bool game_build_prompt_over(UiContext *ui) {
    if(!g_app.settings.touch_controls || !prompted()) return false;
    UiRect r=prompt_rect(ui);return ui_pointer_in_rect(ui,r.x,r.y,r.w,r.h);
}
void game_build_prompt(UiContext *ui) {
    if(!prompted()) return;
    UiRect r=prompt_rect(ui);
    if(g_app.settings.touch_controls) {
        if(ui_button(ui,r.x,r.y,r.w,r.h,tr("build.editTerritory"),true,false)) {
            game_build_interact();ui_pointer_consume(ui);
        }
        const SpriteDef *sprite=ui_sprite_by_name("btn-interact");
        if(sprite) ui_sprite_icon4(ui,sprite,r.x+4,r.y+4,r.h-8,tk_of(ui,QL_SURFACE),tk_of(ui,QL_PRIMARY_UI),tk_of(ui,QL_ACCENT_UI));
    } else {
        ui_draw_rect(ui,r.x,r.y,r.w,r.h,with_alpha(ui->pal.bg_card,.94f));
        ui_draw_text_px_center(ui,UI_FACE_BODY,14*ui->met.scale,true,r.x+r.w*.5f,r.y+r.h*.21f,
            tr("build.editTerritoryKey"),ui->pal.text_main);
    }
}
bool game_build_interact(void) {
    TerrainToolHit hit;
    if(game_build_open() || !can_enter() ||
       !qlcg_terrain_tool_target(&g_app.terrain,g_app.region,g_app.world,QLCG_FLAG_INTERACT_REACH,&hit) || hit.kind!=AIM_HIT_FLAG) return false;
    const TerritoryFaction *f=flag_owner(hit.wx,hit.wz);
    if(!f) return false;
    int cx=(int)floorf(hit.wx/32),cz=(int)floorf(hit.wz/32);
    RegionData *region=qlcg_region_sample(g_app.world,cx,cz,g_app.region->climate_season,&g_app.settings);
    if(!region) return false;
    build=(BuildEditor){.world=g_app.world,.region=region,.faction=f,.cx=cx,.cz=cz,.flag_cx=cx,.flag_cz=cz,
        .fx=region->center_x,.fz=region->center_z,.pitch=-.96f,.yaw=.32f,.distance=90,.grid=true,
        .lot_type=LOT_CUSTOM,.action=B_SELECT,.moving=-1,.brush=4,.strength=.75f,.season=-1,
        .seed=g_app.world->opt.seed,.entered_ms=qlcg_time_ms(),.last_x=g_app.ui.mouse_x,.last_y=g_app.ui.mouse_y};
    qlcg_lot_models_init();
    app_key_track_clear();ui_touch_reset(&g_app.touch);app_set_mouse_lock(false);
    return true;
}
void game_build_close(void) {
    if(!build.region) return;
    while(history_count) dispose(history+--history_count);
    cursor=0;qlcg_terrain_clear_chunks(&g_app.terrain);
    qlcg_region_free(build.region);build=(BuildEditor){0};
    g_app.terrain.probe_cache.valid=false;app_key_track_clear();
}
void build_mode(int mode) {
    if(mode && (!build_lot() || build_lot()->type!=LOT_CUSTOM)) {
        snprintf(build.message,sizeof(build.message),"先选择己方区块，再点“编辑此用地”");return;
    }
    bool changed_layer=(!mode)!=(!build.mode);
    build.mode=mode;build.action=mode?B_PLACE:B_SELECT;build.moving=-1;build.has_start=false;build.page=build.filter=0;
    build.manual_yaw=false;build.area_drag=false;build.styles=false;build.message[0]=0;
    if(changed_layer) {
        build.entered_ms=0;
        build.fx=(build.cx+.5f)*32;build.fz=(build.cz+.5f)*32;
        build.distance=mode?39:90;build.pitch=-.96f;build.view=0;
    }
    if(mode==BUILD_BUY) build.model=LOT_TEA;
    if(mode==BUILD_HOUSE) for(int ordinal=0;;ordinal++) {
        int i=qlcg_lot_style_unit(build.pack,ordinal);
        if(i<0) break;
        const QlcgBuildingModel *model=qlcg_lot_model(i);
        if(model->kind==QLCG_BUILDING_ROOM) {build.model=i;break;}
    }
}
void build_back(void) {
    if(build.styles) {build.styles=false;return;}
    if(build.moving>=0 || build.has_start || build.area_drag) {build.moving=-1;build.has_start=false;build.area_drag=false;}
    else if(build.mode) build_mode(BUILD_MACRO);
    else game_build_close();
}
void build_rotate(void) {
    float step=build.mode==BUILD_BUY?(qlcg_platform_key_down(QL_KEY_MENU)?1:45):90;
    build.angle=fmodf(build.angle+step,360);build.manual_yaw=true;
}
void game_build_key(unsigned key,bool repeat) {
    if(key==QL_KEY_ESCAPE && !repeat) {build_back();return;}
    bool ctrl=qlcg_platform_key_down(QL_KEY_CONTROL);
    if(!repeat && ctrl && (key=='Z'||key=='Y')) {build_history(key=='Y');return;}
    if(!repeat && key=='R') build_rotate();
    if(!repeat && key=='G') build.grid=!build.grid;
    if(build.mode && !repeat && (key=='V'||key=='X'||key=='K'||key==QL_KEY_DELETE)) {
        build.action=key=='V'?B_MOVE:key=='K'?B_COPY:B_REMOVE;build.moving=-1;build.has_start=false;
    }
}
bool build_candidate(int cx,int cz) {
    QlcgCivTerritory t;
    if(qlcg_civ_territory_at(build.world,cx,cz,&t)) return false;
    double lx,lz;
    MacroWorld *w=qlcg_infinite_resolve(build.world,(cx+.5)*32,(cz+.5)*32,&lx,&lz);
    if(!w) return false;
    for(int i=0;i<4;i++)
        if(qlcg_civ_territory_at(build.world,cx+qlcg_claim_directions[i][0],cz+qlcg_claim_directions[i][1],&t) &&
           t.owner==w && t.faction==build.faction) return qlcg_claims_land(w,(int)floor(lx/32),(int)floor(lz/32));
    return false;
}
static void macro_click(int cx,int cz,bool twice) {
    QlcgCivTerritory t;
    if(!qlcg_civ_territory_at(build.world,cx,cz,&t)) {
        if(build.action!=B_EXPAND || !build_candidate(cx,cz)) return;
        double lx,lz;MacroWorld *w=qlcg_infinite_resolve(build.world,(cx+.5)*32,(cz+.5)*32,&lx,&lz);
        if(w && qlcg_claims_add(w,(int)floor(lx/32),(int)floor(lz/32),build.faction->id,QLCG_TERRITORY_CHUNK)) {
            w->territory_revision++;if(w!=build.world) build.world->territory_revision++;
            build.cx=cx;build.cz=cz;
        }
        return;
    }
    if(t.faction!=build.faction) return;
    build.cx=cx;build.cz=cz;
    const QlcgLot *old=build_lot();
    if(twice && old && old->type==LOT_CUSTOM) {build_mode(BUILD_HOUSE);return;}
    if(build.action==B_RELEASE) {
        if(t.type==QLCG_FLAG_CHUNK) return;
        MacroWorld *owner=(MacroWorld *)t.owner;
        /* 归属仍有效时移除覆盖项 */
        QlcgLot copy;
        if(!qlcg_lot_copy(&copy,old)) return;
        if(old && !qlcg_lot_commit(build.world,cx,cz,NULL)) {free(copy.parts);return;}
        if(qlcg_claims_release(owner,t.cx,t.cz,t.faction->id)) {
            owner->territory_revision++;if(owner!=build.world) build.world->territory_revision++;
            while(history_count) dispose(history+--history_count);
            cursor=0;
        } else if(old) qlcg_lot_commit(build.world,cx,cz,&copy);
        free(copy.parts);return;
    }
    if(build.action!=B_PLACE) return;
    BuildAction a;
    if(!capture(&a,build.lot_type==LOT_LAND)) return;
    QlcgLot next={.type=build.lot_type,.turn=(int)(build.angle/90)%4};
    if(next.type==LOT_PRESET) {
        if(!qlcg_lot_preset(&next,build.pack,build.seed,next.turn)) {dispose(&a);return;}
    } else if(next.type==LOT_CUSTOM) {
        if(old && old->type==LOT_CUSTOM) {dispose(&a);return;}
        if(old && old->type==LOT_PRESET) {
            if(!qlcg_lot_copy(&next,old)) {dispose(&a);return;}
            next.type=LOT_CUSTOM;
        } else if(!old && t.type==QLCG_FLAG_CHUNK) {
            QlcgSettlementPart parts[QLCG_SETTLEMENT_PARTS];
            QlcgLot original={.type=LOT_CUSTOM,.count=qlcg_settlement_layout(t.faction,parts),.parts=parts};
            if(!qlcg_lot_copy(&next,&original)) {dispose(&a);return;}
        }
    } else if(next.type>=9010) {
        const QlcgCropType *crop=build.crop>=0?qlcg_crop_type_at(build.crop):NULL;
        const char *id=crop?crop->id:build.crop<0?qlcg_plant_species_id(-1-build.crop):NULL;
        if(!id) {dispose(&a);return;}
        snprintf(next.crop,sizeof(next.crop),"%s",id);
    }
    if(next.type==LOT_PRESET && t.type==QLCG_FLAG_CHUNK) {
        float yaw=qlcg_settlement_yaw(t.faction);
        float fx=old && old->flag_moved?old->flag_x:QLCG_SETTLEMENT_PLAZA_X*cosf(yaw);
        float fz=old && old->flag_moved?old->flag_z:QLCG_SETTLEMENT_PLAZA_X*sinf(yaw);
        if(qlcg_lot_pick(&next,fx,fz,.85f)>=0) {
            snprintf(build.message,sizeof(build.message),"主旗挡住了院落，请先移旗或旋转预设");
            free(next.parts);dispose(&a);return;
        }
    }
    if(old) {next.flag_moved=old->flag_moved;next.flag_x=old->flag_x;next.flag_z=old->flag_z;}
    if(qlcg_lot_commit(build.world,cx,cz,&next)) {
        if(next.type==LOT_LAND) qlcg_chd_lot_reset(build.world,cx,cz);
        remember(a);
    } else dispose(&a);
    free(next.parts);
}
static RegionData *selected_chunk(void) {
    return qlcg_region_chunk_at(build.region,(build.cx-build.region->center_cx)*32.f,(build.cz-build.region->center_cz)*32.f,NULL,NULL);
}
static bool ground_allows(const RegionData *chunk,const QlcgSettlementPart *p) {
    if(!chunk) return false;
    if(chunk->flag_count) {
        QlcgLot one={.parts=(QlcgSettlementPart *)p,.count=1};
        if(qlcg_lot_pick(&one,chunk->flags[0].x,chunk->flags[0].z,.85f)>=0) return false;
    }
    /* 平台与台阶可跨越池塘 */
    if(p->model==LOT_DECK || p->model==LOT_STEPS) return true;
    return qlcg_region_water_at(chunk,p->x,p->z)<=qlcg_region_height_at(chunk,p->x,p->z)+.45f;
}
bool build_line_tool(void) {
    return build.mode && build.action==B_PLACE &&
        ((build.mode==BUILD_TERRAIN && !build.prop && build.brush_tool==LOT_RECT) ||
         (build.mode!=BUILD_TERRAIN && qlcg_lot_is_wall(build.model)));
}
void build_remove_area(float x1,float z1,float x2,float z2) {
    BuildAction a;
    if(!build.mode || build.action!=B_REMOVE || !capture(&a,false)) return;
    QlcgLot next;
    if(!qlcg_lot_copy(&next,build_lot())) {dispose(&a);return;}
    for(int i=next.count-1;i>=0;i--) {
        QlcgSettlementPart *p=next.parts+i;
        if(p->x>=fminf(x1,x2) && p->x<=fmaxf(x1,x2) && p->z>=fminf(z1,z2) && p->z<=fmaxf(z1,z2))
            qlcg_lot_remove(&next,i);
    }
    if(next.count!=a.lot.count && qlcg_lot_commit(build.world,build.cx,build.cz,&next)) remember(a);
    else dispose(&a);
    free(next.parts);
}
bool build_preview(float x,float z,QlcgSettlementPart *out) {
    const QlcgLot *lot=build_lot();
    if(!lot) return false;
    QlcgSettlementPart p=build.moving>=0 && build.moving<lot->count?lot->parts[build.moving]:
        (QlcgSettlementPart){.model=build.model,.slot=-1,.parent=-1,.scale=1};
    const QlcgBuildingModel *m=qlcg_lot_model(p.model);
    if(!m) return false;
    bool furniture=build.mode==BUILD_BUY || p.model>=LOT_TEA;
    float step=furniture?.5f:2;
    if(qlcg_platform_key_down(QL_KEY_MENU)) step=.01f;
    else if(!furniture && m->kind!=QLCG_BUILDING_DOOR) {
        step=p.scale>1 || m->plot_size[0]>12 || m->plot_size[2]>12?16:8;x-=step*.5f;z-=step*.5f;
    }
    p.x=roundf(x/step)*step;p.z=roundf(z/step)*step;
    if(step>=8) {p.x+=step*.5f;p.z+=step*.5f;}
    if(!furniture && m->kind!=QLCG_BUILDING_DOOR) {
        p.slot=qlcg_clampi((int)floorf((p.z+16)/8),0,3)*4+qlcg_clampi((int)floorf((p.x+16)/8),0,3);
        if(m->kind==QLCG_BUILDING_ROOM && (build.moving<0 || build.foundation))
            p.flags=build.foundation?QLCG_PART_FLAT:QLCG_PART_SLOPE;
    }
    p.yaw=build.angle*.01745329252f;p.parent=-1;
    if(!build.manual_yaw && m->kind==QLCG_BUILDING_ROOM) {
        float near=12;
        for(int i=0;i<lot->count;i++) if(qlcg_lot_model(lot->parts[i].model)->kind==QLCG_BUILDING_YARD) {
            float dx=lot->parts[i].x-p.x,dz=lot->parts[i].z-p.z,d=hypotf(dx,dz);
            if(d<near) {near=d;p.yaw=roundf(atan2f(-dx,dz)/1.5707963f)*1.5707963f;}
        }
    }
    bool mounted=m->kind!=QLCG_BUILDING_DOOR || qlcg_lot_door(lot,&p);
    *out=p;
    return mounted && qlcg_lot_placeable(lot,&p,build.moving) && ground_allows(selected_chunk(),&p);
}
static bool landscape(float x,float z) {
    RegionData *c=selected_chunk();
    if(!c) return false;
    if(build.prop) {
        if(fabsf(x)>15.98f || fabsf(z)>15.98f) return false;
        return qlcg_chd_plant(build.world,c->center_x+x,c->center_z+z,build.prop);
    }
    ChdLotBrush brush={.tool=build.brush_tool,.material=build.material,.x=x,.z=z,.radius=build.brush,
        .strength=build.strength,.square=build.square};
    if(brush.tool==LOT_RECT) {
        brush.end_x=x;brush.end_z=z;brush.x=build.start_x;brush.z=build.start_z;
    }
    return qlcg_chd_lot_brush(build.world,c,&brush);
}
void build_click(int cx,int cz,float x,float z,bool twice) {
    if(!build.mode) {macro_click(cx,cz,twice);return;}
    if(cx!=build.cx || cz!=build.cz) return;
    const QlcgLot *old=build_lot();
    if(!old || old->type!=LOT_CUSTOM) return;
    int pick=qlcg_lot_pick(old,x,z,0);
    if(build.action==B_COPY || (build.action==B_MOVE && build.moving<0)) {
        if(pick<0) return;
        QlcgSettlementPart p=old->parts[pick];
        build.model=p.model;build.angle=p.yaw*57.29578f;build.manual_yaw=true;
        const QlcgBuildingModel *model=qlcg_lot_model(p.model);
        for(int i=0;i<qlcg_lot_style_count();i++) if(!strcmp(model->pack,qlcg_lot_style_id(i))) build.pack=i;
        bool structural=qlcg_lot_is_wall(p.model) || p.model==LOT_GATE || p.model==LOT_WIDE_GATE ||
            p.model==LOT_STEPS || p.model==LOT_DECK || model->kind==QLCG_BUILDING_ROOM || model->kind==QLCG_BUILDING_DOOR;
        build.mode=structural?BUILD_HOUSE:BUILD_BUY;
        if(build.action==B_COPY) build.action=B_PLACE;
        else build.moving=pick;
        return;
    }
    bool terrain=build.mode==BUILD_TERRAIN && build.action==B_PLACE;
    bool linear=build.action==B_PLACE && qlcg_lot_is_wall(build.model) && !terrain;
    if((linear || (terrain && !build.prop && build.brush_tool==LOT_RECT)) && !build.has_start) {
        build.start_x=roundf(x/2)*2;build.start_z=roundf(z/2)*2;build.has_start=true;return;
    }
    BuildAction a;
    if(!capture(&a,terrain || build.foundation)) return;
    QlcgLot next;
    if(!qlcg_lot_copy(&next,old)) {dispose(&a);return;}
    bool ok=false;
    if(terrain) ok=landscape(x,z);
    else if(build.action==B_REMOVE && pick>=0) {qlcg_lot_remove(&next,pick);ok=true;}
    else if(build.action==B_FLAG && cx==build.flag_cx && cz==build.flag_cz &&
            fabsf(x)<=14 && fabsf(z)<=15 && qlcg_lot_pick(&next,x,z,.85f)<0 && selected_chunk() &&
            qlcg_region_water_at(selected_chunk(),x,z)<=qlcg_region_height_at(selected_chunk(),x,z)+.3f) {
        next.flag_moved=true;next.flag_x=roundf(x*100)*.01f;next.flag_z=roundf(z*100)*.01f;ok=true;
    } else if(linear) {
        float dx=roundf(x/2)*2-build.start_x,dz=roundf(z/2)*2-build.start_z;
        bool along=fabsf(dx)>=fabsf(dz);
        int n=(int)(fabsf(along?dx:dz)/2);
        ok=n>0;
        for(int i=0;ok && i<n;i++) {
            float d=copysignf(2*i+1.f,along?dx:dz);
            QlcgSettlementPart p={.model=build.model,.parent=-1,.scale=1,.yaw=along?0:1.5707963f,
                .x=build.start_x+(along?d:0),.z=build.start_z+(along?0:d)};
            ok=ground_allows(selected_chunk(),&p) && qlcg_lot_put(&next,p,-1);
        }
    } else if(build.action==B_TEMPLATE) {
        QlcgLot prefab={0};
        for(int i=0;i<160;i++) {
            if(!qlcg_lot_preset(&prefab,build.pack,build.seed+i,(int)(build.angle/90)%4)) break;
            int rooms=0;
            for(int j=0;j<prefab.count;j++) if(qlcg_lot_model(prefab.parts[j].model)->kind==QLCG_BUILDING_ROOM)
                rooms+=prefab.parts[j].scale>1?4:1;
            if(!build.rooms || rooms==build.rooms) {
                float ox=qlcg_clampf(roundf(x/8)*8,-8,8),oz=qlcg_clampf(roundf(z/8)*8,-8,8);
                int start=next.count;ok=true;
                for(int j=0;j<prefab.count && ok;j++) {
                    QlcgSettlementPart p=prefab.parts[j];p.x+=ox;p.z+=oz;if(p.parent>=0) p.parent+=start;
                    ok=ground_allows(selected_chunk(),&p) && qlcg_lot_put(&next,p,-1);
                }
                free(prefab.parts);break;
            }
            free(prefab.parts);
        }
    } else if(build.action==B_PLACE || (build.action==B_MOVE && build.moving>=0)) {
        QlcgSettlementPart p;
        ok=build_preview(x,z,&p) && qlcg_lot_put(&next,p,build.moving);
        if(ok && build.foundation && qlcg_lot_model(p.model)->kind==QLCG_BUILDING_ROOM) {
            float half=4*p.scale;
            ChdLotBrush brush={.tool=LOT_RECT,.x=p.x-half,.z=p.z-half,.end_x=p.x+half,.end_z=p.z+half,.radius=half};
            qlcg_chd_lot_brush(build.world,selected_chunk(),&brush);
        }
    }
    build.has_start=false;
    if(ok && !terrain && !qlcg_lot_commit(build.world,cx,cz,&next)) {
        if(a.ground) qlcg_chd_lot_swap(build.world,cx,cz,a.ground);
        ok=false;
    }
    if(ok) {remember(a);build.moving=-1;build.message[0]=0;}
    else {dispose(&a);snprintf(build.message,sizeof(build.message),"此处无法放置或没有可修改的目标");}
    free(next.parts);
}
void game_build_draw(UiContext *ui) {
    float h=ui->win_h-ui->safe_edges[1]-ui->safe_edges[3];
    float row=fminf(32*ui->met.size,h*.070f);
    float top=ui->safe_edges[1]+row*2+6,bottom=ui->win_h-ui->safe_edges[3]-row*(build.compact?1:3)-6;
    bool canvas=!build.styles && ui->mouse_y>top && ui->mouse_y<bottom && !ui->toast_blocked;
    build_camera(ui,canvas);build_scene(ui,canvas);
    build_toolbar(ui,top,bottom,row);
}
