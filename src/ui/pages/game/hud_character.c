#include "hud_layout.h"
#include "view_atlas_layers.h"
#include "app/app_doll_control.h"

static float status_line(UiContext *ui) {
    return fmaxf(18*ui->met.size,15*ui->met.scale);
}

UiRect game_character_status_bounds(UiContext *ui) {
    UiRect empty={0};
    if (!g_app.world || g_app.settings.character_status==CHAR_STATUS_HIDDEN ||
        g_app.settings_in_drawer || g_app.inventory_ui.open || g_app.touch_layout_edit) return empty;

    float z=ui->met.size, right=ui->win_w-ui->safe_edges[2]-12*z;
    float w=fminf(254*z,(right-ui->safe_edges[0])*.85f);
    bool atlas=g_app.view_mode==VIEW_MODE_ATLAS;
    float y=ui->safe_y_px+game_top_bar_height(ui);
    if (atlas && !g_app.esc_paused) y+=game_atlas_tools_height(ui);
    UiRect menu=game_menu_bounds(ui);
    y=fmaxf(y,menu.y+menu.h)+6*z;
    float h=12*z+status_line(ui)*(g_app.settings.character_status==CHAR_STATUS_COMPACT?1:3);
    if (w<=0 || y+h>ui->win_h-ui->safe_edges[3]) return empty;
    return (UiRect){right-w,y,w,h};
}

float game_character_chat_top(UiContext *ui) {
    UiRect r=game_character_status_bounds(ui);
    return r.h>0?r.y+r.h+6*ui->met.size:ui->safe_y_px+game_top_bar_height(ui);
}

static const char *movement(void) {
    const TerrainRenderer *t=&g_app.terrain;
    if (g_app.view_mode==VIEW_MODE_ATLAS) return tr("character.map");
    if (t->flying) return tr("debug.mode.flying");
    if (t->in_water) return tr("character.swim");
    if (!t->on_ground) return tr("character.air");
    if (t->crouch_offset>.05f) return tr("character.crouch");
    if (hypotf(t->vx,t->vz)>.08f) return tr("character.walk");
    return tr("character.stand");
}

static const char *hand_name(InventorySlot slot) {
    const InventoryItemInfo *info=inventory_item_info(inventory_equipped(&g_app.world->inventory,slot));
    return info?tr(info->name_key):tr("character.empty");
}

void game_character_status_draw(UiContext *ui) {
    UiRect r=game_character_status_bounds(ui);
    if (!r.h) return;
    const char *current=app_doll_controlled_name();
    const char *name=current?current:g_app.world->opt.ren_ming;
    if (!name || !*name) name=tr("character.player");
    float z=ui->met.size,line=status_line(ui),px=fminf(12.5f*ui->met.scale,line*.72f);
    float x=r.x+10*z,y=r.y+5*z,w=r.w-20*z;
    ui_draw_rect(ui,r.x,r.y,r.w,r.h,with_alpha(ui->pal.bg_card,.94f));
    ui_draw_border(ui,r.x,r.y,r.w,r.h,1,ui->pal.border_soft);
    ui_draw_rect(ui,r.x,r.y,3*z,r.h,tk_of(ui,QL_ACCENT_UI));
    const char *state=movement();
    float state_w=fminf(w*.46f,ui_text_width_px(ui,UI_FACE_BODY,px,false,state)+2*z);
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,px,true,x,y,w-state_w-6*z,0,name,ui->pal.text_main);
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,px,false,x+w-state_w,y,state_w,2,state,tk_of(ui,QL_PRIMARY_TEXT));
    if (g_app.settings.character_status==CHAR_STATUS_COMPACT) return;
    char text[384];
    snprintf(text,sizeof(text),tr("character.hands"),hand_name(INV_SLOT_MAIN_HAND),hand_name(INV_SLOT_OFF_HAND));
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,px,false,x,y+line,w,0,text,ui->pal.text_sub);
    const QlcgDollLifeResident *host=app_doll_controlled_resident();
    if (host) snprintf(text,sizeof(text),tr("character.needs"),
                       (host->satiety+5)/10,(host->energy+5)/10,(host->social+5)/10);
    else if (g_app.view_mode==VIEW_MODE_TERRAIN && g_app.region)
        snprintf(text,sizeof(text),tr("doll.position"),(double)g_app.region->center_x+g_app.terrain.x,
                 (double)g_app.terrain.y,(double)g_app.region->center_z+g_app.terrain.z);
    else snprintf(text,sizeof(text),tr("character.chunk"),g_app.player_cx,g_app.player_cz);
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,px,false,x,y+2*line,w,0,text,ui->pal.text_sub);
}
