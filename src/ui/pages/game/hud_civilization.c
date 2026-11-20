#include "hud_civilization.h"
#include "hud_layout.h"
#include "view_atlas_layers.h"
#include "systems/world/world_civ_economy.h"
#include "systems/world/faction_naming.h"
#include <stdio.h>

static bool visible(void) {
    if(!g_app.world||!g_app.world->opt.artificial_enabled) return false;
    for(int i=0;i<LAYER_ID_COUNT;i++) {
        const AtlasLayerSetting *layer=&g_app.atlas.display.layers[i];
        if(layer->id==LAYER_CIVILIZATION) return layer->on&&layer->opacity>0;
    }
    return false;
}

static bool summary(const TerritoryFaction **f,QlcgCivEconomySnapshot *s) {
    const MacroWorld *owner=g_app.atlas.hover_world;
    const TerritoryFaction *hover=g_app.atlas.hover_info.territory;
    if(owner&&hover&&qlcg_civ_economy_snapshot(owner,hover,s)) {*f=hover;return true;}
    if(qlcg_civ_economy_at(g_app.world,g_app.player_cx,g_app.player_cz,&owner,f,s)) return true;
    for(int i=0;i<g_app.world->faction_count;i++) {
        const TerritoryFaction *player=g_app.world->factions+i;
        if(player->active&&player->is_player&&qlcg_civ_economy_snapshot(g_app.world,player,s)) {
            *f=player;return true;
        }
    }
    return false;
}

static UiRect panel_rect(UiContext *ui) {
    float z=ui->met.size,font=12.0f*ui->met.scale;
    float available=(float)ui->win_w-ui->safe_edges[0]-ui->safe_edges[2]-24*z;
    float layer=g_app.atlas.layer_panel_open?qlcg_minf(260*z,(float)ui->win_w*.42f):0;
    UiRect r={ui->safe_x_px+12*z,ui->safe_y_px+game_top_bar_height(ui)+game_atlas_tools_height(ui)+12*z,
        qlcg_minf(360*z,available-layer),font*1.62f*8+22*z};
    return r;
}

bool game_civilization_over_ui(UiContext *ui) {
    if(!visible()) return false;
    UiRect r=panel_rect(ui);
    if(r.w<160*ui->met.size||!ui_hit(ui,r.x,r.y,r.w,r.h)) return false;
    QlcgCivEconomySnapshot s;const TerritoryFaction *f=NULL;
    return summary(&f,&s);
}

void game_civilization_draw(UiContext *ui) {
    if(!visible()) return;
    UiRect r=panel_rect(ui);float z=ui->met.size,font=12.0f*ui->met.scale,line=font*1.62f;
    if(r.w<160*z) return;
    QlcgCivEconomySnapshot s;const TerritoryFaction *f=NULL;
    if(!summary(&f,&s)) return;
    char text[8][256];
    QlcgCivCost expand=qlcg_civ_expand_cost(),upgrade=qlcg_civ_upgrade_cost(s.level);
    snprintf(text[0],sizeof(text[0]),tr("civ.settlement"),qlcg_faction_display_name(f),s.level);
    snprintf(text[1],sizeof(text[1]),tr("civ.population"),s.population,s.capacity,s.farm_count);
    snprintf(text[2],sizeof(text[2]),tr("civ.stock"),s.food,s.wood,s.fiber);
    snprintf(text[3],sizeof(text[3]),tr("civ.land"),s.land_chunks,s.expansion_used,s.expansion_limit,qlcg_civ_economy_block_name(s.expand_block));
    snprintf(text[4],sizeof(text[4]),tr("civ.expansionCost"),expand.food,expand.wood,expand.fiber,s.food_reserve);
    snprintf(text[5],sizeof(text[5]),tr("civ.upgrade"),qlcg_civ_economy_block_name(s.upgrade_block));
    if(s.level>=5) snprintf(text[6],sizeof(text[6]),"%s",tr("civ.maximumLevel"));
    else snprintf(text[6],sizeof(text[6]),tr("civ.upgradeCost"),upgrade.population,upgrade.food,upgrade.wood,upgrade.fiber);
    char leader[160];
    if (f->is_player)
        snprintf(leader,sizeof(leader),"%s · %s",g_app.world->opt.ren_ming[0]?g_app.world->opt.ren_ming:tr("character.player"),f->leader_name);
    else snprintf(leader,sizeof(leader),"%s",f->leader_name);
    snprintf(text[7],sizeof(text[7]),tr("civ.leader"),leader);
    ui_draw_rect(ui,r.x,r.y,r.w,r.h,ui_rgba(36,28,19,225));
    ui_draw_border(ui,r.x,r.y,r.w,r.h,1,ui_rgba(251,244,227,64));
    ui_draw_rect(ui,r.x,r.y,3*z,r.h,ui_rgb_hex(f->main_color,255));
    for(int i=0;i<8;i++)
        ui_draw_text_ellipsis(ui,UI_FACE_BODY,font,i==0,r.x+12*z,r.y+10*z+line*i,r.w-24*z,0,text[i],
            i==0?ui_rgba(255,245,211,255):ui_rgba(234,226,211,255));
}
