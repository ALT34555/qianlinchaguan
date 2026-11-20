#include "app/app_state.h"
#include "hud_layout.h"

static const char *const category_keys[INV_CATEGORY_COUNT] = {
    "inv.category.packs", "inv.category.clothing", "inv.category.accessories", "inv.category.tools", "inv.category.materials"
};

static bool inventory_available(void) {
    return g_app.world && !g_app.esc_paused && !g_app.chat_open &&
           !g_app.settings_in_drawer && !g_app.touch_layout_edit;
}

static bool pointer_in(UiContext *ui, UiRect r) {
    return ui_pointer_in_rect(ui, r.x, r.y, r.w, r.h);
}

static bool point_in(float x, float y, UiRect r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void game_inventory_toggle(void) {
    if (!g_app.inventory_ui.open && !inventory_available()) return;
    bool open = !g_app.inventory_ui.open;
    bool return_to_menu = !open && g_app.inventory_ui.from_menu;
    g_app.inventory_ui = (InventoryUi){.open = open};
    if (open) {
        g_app.atlas.dragging = false;
        g_app.river_craft.drawing = false;
    }
    if (return_to_menu && inventory_available()) g_app.esc_paused = true;
    app_key_track_clear();
}

void game_inventory_pointer_down(UiContext *ui) {
    InventoryUi *state = &g_app.inventory_ui;
    if (!state->open || state->catalog || state->press_pending) return;
    state->press_pending = true;
    state->start_x = ui->mouse_x;
    state->start_y = ui->mouse_y;
}

void game_inventory_pointer_cancel(void) {
    g_app.inventory_ui.press_pending = false;
    g_app.inventory_ui.dragging = false;
}

static bool press_moved(const UiContext *ui, const InventoryUi *state) {
    return abs(ui->mouse_x - state->start_x) + abs(ui->mouse_y - state->start_y)
           > 5 * ui->met.size;
}
#define KIT_TEXT   ui->pal.text_main
#define KIT_MUTED  ui->pal.text_sub
#define KIT_ACCENT tk_of(ui,QL_PRIMARY_TEXT)
#define KIT_LINE   tk_of(ui,QL_LINE)

static void caption(UiContext *ui, UiRect r, float px, bool bold, const char *s, uint32_t color) {
    if(r.w>1 && px>=6) ui_draw_text_ellipsis(ui,UI_FACE_BODY,px,bold,r.x,r.y,r.w,0,s,color);
}

static float fit_label_px(UiContext *ui,const char *text,float px,bool bold,float width) {
    float measured=ui_text_width_px(ui,UI_FACE_BODY,px,bold,text);
    if(measured>width && measured>0) px*=width/measured;
    return fmaxf(6,px);
}

static void section(UiContext *ui, float x,float y,float width,float u,const char *name) {
    float available=width-14*u;
    float px=fit_label_px(ui,name,16*u,false,available);
    ui_draw_rect(ui,x,y+3*u,3*u,15*u,tk_of(ui,QL_ACCENT_UI));
    caption(ui,(UiRect){x+11*u,y+(16*u-px)*.55f,available,20*u},px,false,name,KIT_TEXT);
}

static bool action(UiContext *ui,UiRect r,const char *name,float px,bool active,bool danger) {
    bool hover=ui_hit(ui,r.x,r.y,r.w,r.h);
    uint32_t edge=danger ? tk_of(ui,QL_HIGHLIGHT_UI) : tk_of(ui,QL_PRIMARY_TEXT);
    uint32_t fill=active?tk_of(ui,QL_PRIMARY_UI):tk_of(ui,QL_SURFACE);
    if(hover) fill=danger?tk_of(ui,QL_HIGHLIGHT_SOFT):tk_of(ui,QL_SURFACE_HI);
    ui_draw_rect(ui,r.x,r.y,r.w,r.h,fill);
    ui_draw_border(ui,r.x,r.y,r.w,r.h,1,active||hover?edge:KIT_LINE);
    float width=r.w-6;
    px=fit_label_px(ui,name,px,active,width);
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,px,active,r.x+3,r.y+(r.h-px*1.35f)*.5f,
                          width,1,name,active && !hover ? tk_of(ui,QL_ON_PRIMARY) : KIT_TEXT);
    return hover && ui->mouse_clicked;
}

bool game_inventory_atlas_ready(UiContext *ui) {
    if(!ui->inventory_tex_tried) {
        ui_flush(ui);
        ui->inventory_tex_tried=true;
        ui_load_png_texture(L"content/assets/inventory/field-kit.png",&ui->inventory_tex,NULL,NULL);
    }
    return ui->inventory_tex != 0;
}

void game_inventory_tile(InventoryItem item,bool landscape,bool ghost,float uv[4]) {
    unsigned index=(unsigned)item-1;
    if(landscape) index=item==INV_ITEM_LONGSWORD ? 30u : 26u+(unsigned)(item-INV_ITEM_HOE);
    uv[0]=(float)(index%8)/8;
    uv[1]=(float)(index/8+(ghost?4:0))/8;
    uv[2]=uv[0]+.125f;
    uv[3]=uv[1]+.125f;
}

static void fabric(UiContext *ui,UiRect r,float u) {
    if(!game_inventory_atlas_ready(ui)) return;
    ui_use_sprite_tex(ui,ui->inventory_tex);
    float tile=135*u;
    for(float y=r.y; y<r.y+r.h; y+=tile) for(float x=r.x; x<r.x+r.w; x+=tile) {
        float w=fminf(tile,r.x+r.w-x), h=fminf(tile,r.y+r.h-y);
        ui_draw_sprite_rect(ui,x,y,w,h,.875f,.375f,.875f+.125f*w/tile,.375f+.125f*h/tile,
                            ui_rgba(255,255,255,190));
    }
}

static void item_art(UiContext *ui,InventoryItem item,UiRect r,bool ghost) {
    if(item<=INV_ITEM_NONE || item>=INV_ITEM_COUNT || r.w<3 || r.h<3) return;
    bool loaded=game_inventory_atlas_ready(ui);
    float size=fminf(r.w,r.h),x=r.x+(r.w-size)*.5f,y=r.y+(r.h-size)*.5f;
    if(!loaded) {
        const InventoryItemInfo *info=inventory_item_info(item);
        const char *name=tr(info->name_key);
        float px=fit_label_px(ui,name,size*.42f,false,size);
        ui_draw_text_ellipsis(ui,UI_FACE_BODY,px,false,x,y+(size-px*1.35f)*.5f,
                              size,1,name,ghost?KIT_MUTED:KIT_TEXT);
        return;
    }
    const InventoryItemInfo *info=inventory_item_info(item);
    bool landscape=!ghost && info->width>1 && r.w>1.55f*r.h;
    float uv[4];
    game_inventory_tile(item,landscape,ghost,uv);
    ui_use_sprite_tex(ui,ui->inventory_tex);
    float iw=landscape ? fminf(r.w,r.h*info->width) : size;
    float ih=landscape ? iw/info->width : size;
    ui_draw_sprite_rect(ui,r.x+(r.w-iw)*.5f,r.y+(r.h-ih)*.5f,iw,ih,uv[0],uv[1],uv[2],uv[3],
                        ghost ? with_alpha(tk_of(ui,QL_ACCENT_TEXT),.46f) : ui_rgba(255,255,255,255));
}

typedef struct {
    InventoryPosition hit,origin;
    bool valid, pack_drop;
} InventoryTargets;

static void register_target(UiContext *ui,const InventoryUi *state,InventoryTargets *targets,
                            UiRect r,InventoryPosition position) {
    if(pointer_in(ui,r)) {
        targets->hit=position;
        targets->valid=true;
    }
    if(point_in((float)state->start_x,(float)state->start_y,r)) targets->origin=position;
}
static const InventoryItem slot_art[INV_SLOT_COUNT] = {
    [INV_SLOT_PACK] = INV_ITEM_SMALL_PACK, [INV_SLOT_HEAD] = INV_ITEM_HEADLAMP,
    [INV_SLOT_BODY] = INV_ITEM_LIFE_VEST, [INV_SLOT_HANDS] = INV_ITEM_WITHERING_GLOVES,
    [INV_SLOT_LEGS] = INV_ITEM_SPEED_PANTS, [INV_SLOT_FEET] = INV_ITEM_SPRING_SHOES,
    [INV_SLOT_ACCESSORY_1] = INV_ITEM_WINGS, [INV_SLOT_ACCESSORY_2] = INV_ITEM_AQUATIC_RING,
    [INV_SLOT_ACCESSORY_3] = INV_ITEM_STARGAZING_GLASSES
};

static void hook_mark(UiContext *ui, float cx, float cy, float s, bool locked) {
    uint32_t ink = locked ? ui->pal.text_muted : tk_of(ui,QL_ACCENT_TEXT);
    if (locked) {
        ui_draw_border(ui, cx-5*s, cy-1*s, 10*s, 8*s, s, ink);
        ui_draw_line(ui, cx-3*s, cy-s, cx-3*s, cy-4*s, s, ink);
        ui_draw_line(ui, cx-3*s, cy-4*s, cx+3*s, cy-4*s, s, ink);
        ui_draw_line(ui, cx+3*s, cy-4*s, cx+3*s, cy-s, s, ink);
    } else {
        ui_draw_line(ui, cx-5*s, cy, cx+5*s, cy, s, ink);
        ui_draw_line(ui, cx, cy-5*s, cx, cy+5*s, s, ink);
    }
}

static void slot_card(UiContext *ui, const InventoryUi *state, const Inventory *inv,
                      InventorySlot slot, UiRect r, float u, InventoryTargets *targets) {
    bool hook = slot >= INV_SLOT_TOOL_1;
    bool enabled = inventory_slot_enabled(inv,slot);
    bool hover = enabled && pointer_in(ui,r);
    bool selected = state->selected && state->source.slot == (int)slot;
    InventoryItem item = inventory_equipped(inv,slot);
    const InventoryItemInfo *info = inventory_item_info(item);
    uint32_t edge = selected || hover ? KIT_ACCENT :
        tk_of(ui,info ? QL_ACCENT_UI : enabled ? QL_LINE : QL_LINE_SOFT);
    ui_draw_rect_grad(ui,r.x,r.y,r.w,r.h,tk_of(ui,info ? QL_SURFACE_HI : QL_SURFACE),
                      tk_of(ui,QL_SURFACE_LO));
    ui_draw_border(ui,r.x,r.y,r.w,r.h,selected ? 2*u : 1,edge);
    float footer = info && r.h >= 41*u ? 18*u : 0;
    float inset=fminf(7*u,fminf(r.w,r.h)*.09f);
    UiRect picture = {r.x+inset,r.y+inset,r.w-2*inset,r.h-footer-2*inset};
    if (info || slot_art[slot]) {
        item_art(ui,info ? item : slot_art[slot],picture,!info);
    } else if (hook) {
        hook_mark(ui,r.x+r.w*.5f,r.y+r.h*.5f,u,!enabled);
    } else {
        float cx = r.x+r.w*.5f, cy = r.y+r.h*.5f, s = fminf(r.w,r.h)*.12f;
        ui_draw_line(ui,cx-s,cy-s,cx+s,cy+s,u,tk_of(ui,QL_LINE));
        ui_draw_line(ui,cx-s,cy+s,cx+s,cy-s,u,tk_of(ui,QL_LINE));
    }
    if (footer) {
        ui_draw_rect_grad(ui,r.x+1,r.y+r.h-footer-1,r.w-2,footer,with_alpha(tk_of(ui,QL_BG_DEEP),.70f),tk_of(ui,QL_BG_DEEP));
        caption(ui,(UiRect){r.x+4*u,r.y+r.h-footer-2*u,r.w-8*u,footer},11*u,false,tr(info->name_key),KIT_TEXT);
    }
    if (hook && enabled && !info)
        ui_draw_rect(ui,r.x+1,r.y+1,10*u,2*u,tk_of(ui,QL_ACCENT_UI));
    if (enabled || info) register_target(ui,state,targets,r,(InventoryPosition){0,0,slot});
}

static void equipment_draw(UiContext *ui, const InventoryUi *state,const Inventory *inv,
                           UiRect a,float u,InventoryTargets *targets) {
    const InventorySlot armor[] = {INV_SLOT_HEAD,INV_SLOT_BODY,INV_SLOT_LEGS};
    const char *names[] = {tr("inv.heading.head"),tr("inv.heading.body"),tr("inv.heading.legs")};
    const InventorySlot rail[3][3] = {
        {INV_SLOT_ACCESSORY_1,INV_SLOT_ACCESSORY_2,INV_SLOT_ACCESSORY_3},
        {INV_SLOT_BODY_HOOK_1,INV_SLOT_BODY_HOOK_2,INV_SLOT_BODY_HOOK_3},
        {INV_SLOT_TOOL_1,INV_SLOT_LEG_HOOK_2,INV_SLOT_LEG_HOOK_3}
    };
    float gap = 5*u, gutter = 13*u, title = 23*u;
    float hand_w = a.w*.224f, hand_x = a.x+a.w-hand_w;
    float left_w = a.w-hand_w-gutter;
    float armor_w = (left_w-gutter)*.345f, rail_x = a.x+armor_w+gutter;
    float rail_w = left_w-armor_w-gutter, rail_card = (rail_w-2*gap)/3;
    float row = (a.h-2*gap)/3;
    ui_draw_rect(ui,hand_x-gutter*.53f,a.y,1,a.h,tk_of(ui,QL_LINE_SOFT));
    for (int i=0; i<3; ++i) {
        float y = a.y+i*(row+gap);
        section(ui,a.x,y,armor_w,u,names[i]);
        slot_card(ui,state,inv,armor[i],(UiRect){a.x,y+title,armor_w,row-title},u,targets);
        const char *group = i ? (i==1 ? tr("inv.heading.body_hooks") : tr("inv.heading.leg_hooks")) : tr("inv.heading.accessories");
        int active = 0;
        for (int j=0; j<3; ++j) active += inventory_slot_enabled(inv,rail[i][j]);
        char count[16];
        snprintf(count,sizeof(count),"%d / 3",active);
        section(ui,rail_x,y,rail_w-(i ? 32*u : 0),u,group);
        if (i) ui_draw_text_px_right(ui,UI_FACE_MONO,10*u,false,rail_x+rail_w,y+5*u,count,KIT_MUTED);
        float rail_h = fminf(row-title, rail_card*1.2f);
        for (int j=0; j<3; ++j) {
            UiRect box = {rail_x+j*(rail_card+gap),y+title,rail_card,rail_h};
            slot_card(ui,state,inv,rail[i][j],box,u,targets);
        }
    }
    float main_h=(a.h-2*title-gutter)*.59f, off_y=a.y+title+main_h+gutter;
    section(ui,hand_x,a.y,hand_w,u,tr("inv.heading.main_hand"));
    slot_card(ui,state,inv,INV_SLOT_MAIN_HAND,(UiRect){hand_x,a.y+title,hand_w,main_h},u,targets);
    section(ui,hand_x,off_y,hand_w,u,tr("inv.heading.off_hand"));
    slot_card(ui,state,inv,INV_SLOT_OFF_HAND,
              (UiRect){hand_x,off_y+title,hand_w,a.y+a.h-off_y-title},u,targets);
}

static int grid_usage(const Inventory *inv,int grid,int cols,int rows) {
    int used=0;
    for (int y=0; y<rows; ++y) for (int x=0; x<cols; ++x) {
        const InventoryItemInfo *info=inventory_item_info(inventory_grid_item(inv,grid,x,y));
        if (info) used+=info->width*info->height;
    }
    return used;
}

static void set_compartment(InventoryUi *state, bool pack) {
    if (state->show_pack!=pack) {
        state->show_pack=pack;
        state->grid_page=0;
        state->magnified=false;
        state->error=0;
    }
}

static bool pack_destination(const Inventory *inv,InventoryPosition source,InventoryPosition *out) {
    InventoryItem item=inventory_at(inv,&source);
    const InventoryItemInfo *info=inventory_item_info(item);
    int cols,rows;
    inventory_grid_dimensions(inv,INV_GRID_PACK,&cols,&rows);
    if (!info || source.slot==INV_SLOT_PACK) return false;
    for (int y=0; y+info->height<=rows; ++y) {
        int run=0;
        for (int x=0; x<cols; ++x) {
            InventoryPosition cell={x,y,INV_GRID_PACK};
            if (inventory_at(inv,&cell)) run=0;
            else if (++run==info->width) {
                *out=(InventoryPosition){x-run+1,y,INV_GRID_PACK};
                return true;
            }
        }
    }
    return false;
}

static void pack_summary(UiContext *ui, InventoryUi *state, const Inventory *inv,
                         UiRect a,float u,bool narrow,InventoryTargets *targets) {
    float gap=8*u, title=24*u;
    float tile_w = (a.w-3*gap)*.163f;
    const InventorySlot slots[] = {INV_SLOT_HANDS,INV_SLOT_FEET,INV_SLOT_PACK};
    const char *names[] = {tr("inv.heading.hands"),tr("inv.heading.feet"),tr("inv.category.packs")};
    for (int i=0; i<3; ++i) {
        float x=a.x+i*(tile_w+gap);
        section(ui,x,a.y,tile_w,u,names[i]);
        slot_card(ui,state,inv,slots[i],(UiRect){x,a.y+title,tile_w,a.h-title},u,targets);
    }
    float x=a.x+3*(tile_w+gap);
    UiRect r={x,a.y+title,a.x+a.w-x,a.h-title};
    section(ui,x,a.y,r.w,u,tr("inv.heading.pack"));
    InventoryItem item=inventory_equipped(inv,INV_SLOT_PACK);
    const InventoryItemInfo *info=inventory_item_info(item);
    int cols,rows;
    inventory_grid_dimensions(inv,INV_GRID_PACK,&cols,&rows);
    uint32_t edge=tk_of(ui,QL_LINE);
    ui_draw_rect_grad(ui,r.x,r.y,r.w,r.h,tk_of(ui,QL_SURFACE),tk_of(ui,QL_SURFACE_LO));
    ui_draw_border(ui,r.x,r.y,r.w,r.h,1,edge);
    float cell=fmaxf(16*u,r.h/3);
    for (float cx=r.x+cell; cx<r.x+r.w-1; cx+=cell)
        ui_draw_rect(ui,cx,r.y+1,1,r.h-2,tk_of(ui,QL_LINE_FAINT));
    for (float cy=r.y+cell; cy<r.y+r.h-1; cy+=cell)
        ui_draw_rect(ui,r.x+1,cy,r.w-2,1,tk_of(ui,QL_LINE_FAINT));
    char status[72];
    if (info) {
        int used=grid_usage(inv,INV_GRID_PACK,cols,rows);
        snprintf(status,sizeof(status),"%d × %d   ·   %d / %d",cols,rows,used,cols*rows);
    } else {
        snprintf(status,sizeof(status),"%s",tr("inv.pack.unlock"));
    }
    bool hovering=info && ui_hit(ui,r.x,r.y,r.w,r.h);
    bool moving=state->selected || (state->press_pending && press_moved(ui,state));
    InventoryPosition source=state->selected ? state->source : targets->origin;
    targets->pack_drop=hovering && moving;
    bool deposit=targets->pack_drop && state->selected && pack_destination(inv,source,&targets->hit);
    if (deposit) targets->valid=true;
    float cy=r.y+r.h*.5f, label_w=r.w-8*u;
    const char *prompt=deposit ? tr("inv.pack.deposit") : info ? tr("inv.pack.open") : tr("inv.pack.empty");
    float px=fit_label_px(ui,prompt,15*u,false,label_w);
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,px,false,r.x+4*u,cy-17*u,label_w,1,prompt,info ? KIT_ACCENT : KIT_MUTED);
    ui_draw_text_ellipsis(ui,info ? UI_FACE_MONO : UI_FACE_BODY,10*u,false,r.x+4*u,cy+6*u,label_w,1,status,KIT_MUTED);
    if (hovering) {
        ui_draw_border(ui,r.x,r.y,r.w,r.h,1,KIT_ACCENT);
        if (ui->mouse_clicked && !moving) {
            set_compartment(state,true);
            if (narrow) state->pack_page=true;
        }
    }
}

static void grid_draw(UiContext *ui, InventoryUi *state, const Inventory *inv, UiRect a,
                      float u, InventoryTargets *targets) {
    bool packed = inventory_equipped(inv,INV_SLOT_PACK)!=INV_ITEM_NONE;
    if (!packed) set_compartment(state,false);
    int grid = state->show_pack ? INV_GRID_PACK : INV_GRID_POCKET;
    int cols, rows;
    inventory_grid_dimensions(inv,grid,&cols,&rows);
    char dims[32];
    snprintf(dims,sizeof(dims),"%d × %d",cols,rows);
    section(ui,a.x,a.y,a.w-65*u,u,state->show_pack ? tr("inv.pack.extra") : tr("inv.pack.base"));
    ui_draw_text_px_right(ui,UI_FACE_MONO,15*u,true,a.x+a.w,a.y+1*u,dims,KIT_ACCENT);

    float tab_y=a.y+26*u, tab_h=25*u, gap=4*u;
    float tw=(a.w-gap)*.5f;
    UiRect pocket_tab={a.x,tab_y,tw,tab_h}, pack_tab={a.x+tw+gap,tab_y,tw,tab_h};
    bool pocket_press=action(ui,pocket_tab,tr("inv.pack.base"),12*u,!state->show_pack,false);
    bool pack_press=false;
    if (packed) pack_press=action(ui,pack_tab,tr("inv.heading.pack"),12*u,state->show_pack,false);
    else {
        ui_draw_rect(ui,pack_tab.x,tab_y,tw,tab_h,tk_of(ui,QL_SURFACE_LO));
        ui_draw_border(ui,pack_tab.x,tab_y,tw,tab_h,1,tk_of(ui,QL_LINE_SOFT));
        ui_draw_text_ellipsis(ui,UI_FACE_BODY,11*u,false,pack_tab.x+3*u,tab_y+4*u,tw-6*u,1,
                              tr("inv.pack.disabled"),KIT_MUTED);
    }
    bool carry=state->dragging && press_moved(ui,state);
    if (pocket_press || (carry && pointer_in(ui,pocket_tab))) set_compartment(state,false);
    if (pack_press || (packed && carry && pointer_in(ui,pack_tab))) set_compartment(state,true);
    grid=state->show_pack ? INV_GRID_PACK : INV_GRID_POCKET;
    inventory_grid_dimensions(inv,grid,&cols,&rows);
    if (!cols || !rows) return;
    float top=tab_y+tab_h+7*u, bottom=a.y+a.h-29*u;
    float inset=6*u;
    UiRect well={a.x,top,a.w,bottom-top};
    ui_draw_rect_grad(ui,well.x,well.y,well.w,well.h,tk_of(ui,QL_BG_DEEP),tk_of(ui,QL_SURFACE_LO));
    ui_draw_border(ui,well.x,well.y,well.w,well.h,1,KIT_LINE);
    int visible_w=cols, visible_h=rows;
    if (state->magnified && state->show_pack) {
        visible_w=(int)fminf(cols,fmaxf(4,floorf((a.w-inset*2)/(40*u))));
        visible_h=(int)fminf(rows,fmaxf(4,floorf((well.h-inset*2)/(40*u))));
    }
    int pages_x=(cols+visible_w-1)/visible_w, pages_y=(rows+visible_h-1)/visible_h;
    int pages=pages_x*pages_y;
    if (state->grid_page>=pages) state->grid_page=0;
    int first_x=state->grid_page%pages_x*visible_w, first_y=state->grid_page/pages_x*visible_h;
    float cell=fminf((well.w-2*inset)/visible_w,(well.h-2*inset)/visible_h);
    if (cell<2) return;
    UiRect cells={well.x+(well.w-visible_w*cell)*.5f,well.y+(well.h-visible_h*cell)*.5f,
                  (float)fminf(visible_w,cols-first_x)*cell,(float)fminf(visible_h,rows-first_y)*cell};
    float seam=cell>22*u ? 2*u : 1;
    ui_clip_push(ui,cells.x,cells.y,cells.w,cells.h);
    for (int y=first_y; y<first_y+visible_h && y<rows; ++y) {
        float py=cells.y+(y-first_y)*cell;
        for (int x=first_x; x<first_x+visible_w && x<cols; ++x) {
            float px=cells.x+(x-first_x)*cell;
            ui_draw_rect_grad(ui,px,py,cell-seam,cell-seam,
                              tk_of(ui,(x+y)%2 ? QL_SURFACE : QL_SURFACE_HI),tk_of(ui,QL_SURFACE));
            ui_draw_border(ui,px,py,cell-seam,cell-seam,1,tk_of(ui,QL_LINE_SOFT));
        }
        for (int x=0; x<first_x+visible_w && x<cols; ++x) {
            InventoryItem item=inventory_grid_item(inv,grid,x,y);
            const InventoryItemInfo *info=inventory_item_info(item);
            if (!info || x+info->width<=first_x) continue;
            UiRect r={cells.x+(x-first_x)*cell,py,cell*info->width-seam,cell*info->height-seam};
            bool selected=state->selected && state->source.slot==grid && state->source.x==x && state->source.y==y;
            ui_draw_rect_grad(ui,r.x,r.y,r.w,r.h,tk_of(ui,QL_SURFACE_HI),tk_of(ui,QL_SURFACE_LO));
            ui_draw_border(ui,r.x,r.y,r.w,r.h,selected ? 2*u : 1,selected ? KIT_ACCENT : tk_of(ui,QL_ACCENT_UI));
            bool named=cell>=48*u && r.w>=ui_text_width_px(ui,UI_FACE_BODY,10*u,false,tr(info->name_key))+4*u;
            float tag=named ? 14*u : 0;
            item_art(ui,item,(UiRect){r.x+1,r.y+1,r.w-2,r.h-2-tag},false);
            if (named) ui_draw_text_px_right(ui,UI_FACE_BODY,10*u,false,r.x+r.w-4*u,r.y+r.h-tag,tr(info->name_key),KIT_TEXT);
        }
    }
    if (point_in((float)state->start_x,(float)state->start_y,cells))
        targets->origin=(InventoryPosition){first_x+(int)((state->start_x-cells.x)/cell),
                                            first_y+(int)((state->start_y-cells.y)/cell),grid};
    if (pointer_in(ui,cells)) {
        int x=first_x+(int)((ui->mouse_x-cells.x)/cell), y=first_y+(int)((ui->mouse_y-cells.y)/cell);
        if (x<cols && y<rows) {
            targets->hit=(InventoryPosition){x,y,grid};
            targets->valid=true;
            InventoryPosition anchor=targets->hit;
            const InventoryItemInfo *at=inventory_item_info(inventory_at(inv,&anchor));
            ui_draw_border(ui,cells.x+(anchor.x-first_x)*cell,cells.y+(anchor.y-first_y)*cell,
                           (at ? at->width : 1)*cell-seam,(at ? at->height : 1)*cell-seam,1.5f*u,KIT_ACCENT);
        }
    }
    ui_clip_pop(ui);
    int used=grid_usage(inv,grid,cols,rows);
    char usage[40];
    snprintf(usage,sizeof(usage),"%d / %d",used,cols*rows);
    float fy=bottom+4*u;
    ui_draw_text_px(ui,UI_FACE_MONO,12*u,true,a.x,fy+1*u,usage,KIT_TEXT);
    if (state->show_pack) {
        UiRect toggle={a.x+a.w-58*u,fy,58*u,19*u};
        if (action(ui,toggle,state->magnified ? tr("inv.grid.overview") : tr("inv.grid.zoom"),10*u,false,false)) {
            state->magnified=!state->magnified;
            state->grid_page=0;
        }
        if (pages>1) {
            char index[24];
            snprintf(index,sizeof(index),"%d/%d",state->grid_page+1,pages);
            float px=toggle.x-88*u;
            if (action(ui,(UiRect){px,fy,21*u,19*u},"‹",11*u,false,false))
                state->grid_page=(state->grid_page+pages-1)%pages;
            ui_draw_text_px_center(ui,UI_FACE_MONO,10*u,false,px+42*u,fy+2*u,index,KIT_TEXT);
            if (action(ui,(UiRect){px+63*u,fy,21*u,19*u},"›",11*u,false,false))
                state->grid_page=(state->grid_page+1)%pages;
        }
    } else {
        ui_draw_text_px_right(ui,UI_FACE_BODY,11*u,false,a.x+a.w,fy,tr("inv.grid.used"),KIT_MUTED);
    }
    ui_draw_rect(ui,a.x,a.y+a.h-2*u,a.w,2*u,tk_of(ui,QL_LINE));
    if (used) ui_draw_rect(ui,a.x,a.y+a.h-2*u,a.w*(float)used/(cols*rows),2*u,tk_of(ui,QL_PRIMARY_UI));
}

static void inventory_click(UiContext *ui, InventoryUi *state, Inventory *inv,
                            InventoryPosition hit, bool valid, InventoryPosition origin) {
    bool pressed = state->press_pending;
    state->press_pending = false;
    if (ui_pointer_blocked(ui) || state->discard_confirm) return;
    if (ui->right_clicked) {
        state->selected = state->dragging = false;
        state->error = 0;
        return;
    }
    if (pressed && !state->selected && inventory_at(inv, &origin)) {
        state->source = origin;
        state->selected = state->dragging = true;
        state->error = 0;
    }
    if (state->dragging && !ui->mouse_down) {
        state->dragging = false;
        if (!press_moved(ui, state)) return;
        if (!valid || !inventory_move(inv, state->source, hit)) state->error = 1;
        else state->selected = false;
    } else if (ui->mouse_clicked && valid && state->selected && !state->dragging) {
        if (inventory_move(inv, state->source, hit)) state->selected = false;
        state->error = state->selected ? 1 : 0;
    } else if (ui->mouse_clicked && valid && !state->selected && inventory_at(inv, &hit)) {
        state->source = hit;
        state->selected = true;
        state->error = 0;
    }
}
static const char *item_help(InventoryItem item) {
    const InventoryItemInfo *info=inventory_item_info(item);
    return info ? tr(info->help_key) : "";
}

static void catalog_draw(UiContext *ui, InventoryUi *state, Inventory *inv, UiRect area, float u) {
    const bool side = area.w >= 590*u && area.h >= 234*u;
    const float gap = 9*u, nav = 125*u, button_h = 34*u;
    UiRect cards = area;
    if (side) {
        cards.x += nav + gap;
        cards.w -= nav + gap;
        ui_draw_rect_grad(ui,area.x,area.y,nav,area.h,
                          tk_of(ui,QL_SURFACE),tk_of(ui,QL_SURFACE_LO));
        ui_draw_border(ui,area.x,area.y,nav,area.h,1,KIT_LINE);
        section(ui,area.x+9*u,area.y+11*u,nav-18*u,u,tr("inv.heading.categories"));
    } else {
        cards.y += button_h + gap;
        cards.h -= button_h + gap;
    }

    for (int i=0;i<INV_CATEGORY_COUNT;i++) {
        float w=(area.w-(INV_CATEGORY_COUNT-1)*gap)/INV_CATEGORY_COUNT;
        UiRect r=side ? (UiRect){area.x+8*u,area.y+39*u+i*(button_h+5*u),nav-16*u,button_h}
                      : (UiRect){area.x+i*(w+gap),area.y,w,button_h};
        float font=fminf(13*u,r.w/4);
        if (action(ui,r,tr(category_keys[i]),font,(int)state->category==i,false)) {
            state->category=(InventoryCategory)i;
            state->error=0;
            ui_pointer_consume(ui);
        }
    }

    InventoryItem items[INV_ITEM_COUNT];
    int count=0;
    for (int id=INV_ITEM_NONE+1;id<INV_ITEM_COUNT;id++) {
        const InventoryItemInfo *info=inventory_item_info((InventoryItem)id);
        if(info && info->category==state->category) items[count++]=(InventoryItem)id;
    }
    if (!count || cards.h<15*u || cards.w<24*u) return;
    bool packs = state->category==INV_CATEGORY_PACKS;
    float summary=(side||packs) ? 28*u : 0;
    if (packs)
        caption(ui,(UiRect){cards.x,cards.y,cards.w,summary},12*u,false,
                tr("inv.catalog.capacity"),KIT_MUTED);
    else if (side)
        caption(ui,(UiRect){cards.x,cards.y,cards.w,summary},12*u,false,
                tr("inv.catalog.pick"),KIT_MUTED);
    cards.y+=summary;
    cards.h-=summary;

    int columns=cards.w>=560*u ? 4 : cards.w>=440*u ? 3 : 2;
    if (columns>count) columns=count;
    int rows=(count+columns-1)/columns;
    float w=(cards.w-(columns-1)*gap)/columns;
    float h=fminf(218*u,(cards.h-(rows-1)*gap)/rows);
    if (h<10*u || w<12*u) return;
    float inset=7*u, name=fminf(14*u,(h-2*inset)*.31f);
    float line_h=16*u;
    int line_budget=(int)fmaxf(1,floorf((h*.47f-3*u)/line_h));
    int help_lines[INV_ITEM_COUNT]={0}, max_lines=0;
    if (h>=78*u) {
        for (int i=0; i<count; ++i) {
            help_lines[i]=ui_measure_text_wrapped(ui,UI_FACE_BODY,11*u,false,w-2*inset,item_help(items[i]));
            int shown=help_lines[i]>line_budget ? 1 : help_lines[i];
            if (shown>max_lines) max_lines=shown;
        }
    }
    float details=max_lines ? max_lines*line_h+3*u : 0;
    float art_h=fmaxf(0,h-name*1.55f-details-2*inset);
    for (int i=0;i<count;i++) {
        InventoryItem item=items[i];
        const InventoryItemInfo *info=inventory_item_info(item);
        UiRect r={cards.x+(i%columns)*(w+gap),cards.y+(i/columns)*(h+gap),w,h};
        bool hover=ui_hit(ui,r.x,r.y,r.w,r.h);
        ui_draw_rect_grad(ui,r.x,r.y,r.w,r.h,
                          tk_of(ui,hover ? QL_SURFACE_HI : QL_SURFACE),tk_of(ui,QL_SURFACE_LO));
        ui_draw_border(ui,r.x,r.y,r.w,r.h,1,hover?KIT_ACCENT:KIT_LINE);
        if (hover) ui_draw_rect(ui,r.x+1,r.y+1,r.w-2,2*u,KIT_ACCENT);

        item_art(ui,item,(UiRect){r.x+inset,r.y+inset,r.w-2*inset,art_h},false);
        float text_y=r.y+inset+art_h;
        float label_w=r.w-2*inset;
        char size[20];
        snprintf(size,sizeof(size),"%u × %u",(unsigned)info->width,(unsigned)info->height);
        float tag=ui_text_width_px(ui,UI_FACE_MONO,9*u,false,size);
        if (label_w>=tag+5*u+ui_text_width_px(ui,UI_FACE_BODY,name,true,tr(info->name_key))) {
            ui_draw_text_px_right(ui,UI_FACE_MONO,9*u,false,r.x+r.w-inset,text_y+2*u,size,KIT_ACCENT);
            label_w-=tag+5*u;
        } else if (art_h>=30*u) {
            ui_draw_text_px_right(ui,UI_FACE_MONO,9*u,false,r.x+r.w-inset,r.y+2*u,size,KIT_ACCENT);
        }
        caption(ui,(UiRect){r.x+inset,text_y,label_w,20*u},name,true,tr(info->name_key),KIT_TEXT);
        float bottom=text_y+name*1.55f;
        if (help_lines[i]>line_budget) {
            caption(ui,(UiRect){r.x+inset,bottom,r.w-2*inset,details},11*u,false,item_help(item),KIT_MUTED);
        } else if (help_lines[i]>0) {
            ui_draw_text_wrapped(ui,UI_FACE_BODY,11*u,false,r.x+inset,bottom,r.w-2*inset,line_h,
                                 item_help(item),KIT_MUTED);
        }
        if (hover && ui->mouse_clicked) {
            state->error=inventory_add(inv,item)?3:2;
            ui_pointer_consume(ui);
        }
    }
}

void game_inventory_panel(UiContext *ui) {
    InventoryUi *state = &g_app.inventory_ui;
    if (!inventory_available()) {
        if (state->open) game_inventory_toggle();
        return;
    }
    if (!state->open) return;
    Inventory *inv=&g_app.world->inventory;
    if ((state->press_pending || state->dragging) && press_moved(ui,state))
        ui->mouse_clicked=false;
    bool leisure=g_app.world->opt.play_mode==PLAY_MODE_LEISURE;
    if (!leisure) state->catalog=false;
    float sw=ui->win_w-ui->safe_edges[0]-ui->safe_edges[2];
    float sh=ui->win_h-ui->safe_edges[1]-ui->safe_edges[3];
    if (sw<100 || sh<100) return;
    bool narrow=sw<sh*.93f || sw<500;
    float u=narrow ? fminf(sw/400,sh/605) : fminf(sw/1010,sh/650);
    u=qlcg_clampf(u,.48f,1.7f);
    float w=fminf(sw-(narrow ? 14 : 32)*u,(narrow ? 550 : 1230)*u);
    float h=fminf(sh-(narrow ? 14 : 24)*u,(narrow ? 890 : 780)*u);
    float x=ui->safe_edges[0]+(sw-w)*.5f, y=ui->safe_edges[1]+(sh-h)*.5f;
    float pad=(narrow ? 12 : 20)*u, head=58*u, foot=44*u;
    ui_draw_rect(ui,0,0,(float)ui->win_w,(float)ui->win_h,tk_of(ui,QL_SHADOW_LG));
    ui_draw_rect_grad(ui,x,y,w,h,with_alpha(tk_of(ui,QL_BG),.93f),with_alpha(tk_of(ui,QL_BG_DEEP),.95f));
    fabric(ui,(UiRect){x,y,w,h},u);
    ui_draw_border(ui,x,y,w,h,1,tk_of(ui,QL_LINE));
    ui_draw_rect(ui,x+pad,y+15*u,5*u,24*u,tk_of(ui,QL_PRIMARY_UI));
    float button_h=30*u, close_w=(narrow ? 46 : 60)*u, switch_w=(narrow ? 75 : 98)*u;
    UiRect close={x+w-pad-close_w,y+12*u,close_w,button_h};
    UiRect catalog={close.x-switch_w-6*u,close.y,switch_w,button_h};
    const char *title=state->catalog ? tr("inv.catalog.title") : tr("inv.title");
    float tx=x+pad+14*u, title_w=(leisure ? catalog.x : close.x)-tx-8*u;
    float font=fit_label_px(ui,title,(narrow ? 21 : 23)*u,true,title_w);
    caption(ui,(UiRect){tx,close.y+(button_h-font*1.35f)*.5f,title_w,button_h},font,true,title,KIT_TEXT);
    if (action(ui,close,tr("inv.close"),13*u,false,false)) {
        game_inventory_toggle();
        return;
    }
    if (leisure && action(ui,catalog,
                          state->catalog ? tr("inv.catalog.back") : tr("inv.catalog.title"),13*u,state->catalog,false)) {
        state->catalog=!state->catalog;
        state->error=0;
        state->selected=state->dragging=state->press_pending=state->discard_confirm=false;
        ui_pointer_consume(ui);
    }
    ui_draw_rect(ui,x+pad,y+head-5*u,w-2*pad,1,KIT_LINE);
    UiRect body={x+pad,y+head+6*u,w-2*pad,h-head-foot-12*u};
    InventoryTargets targets={.hit={-1,-1,INV_GRID_POCKET},.origin={-1,-1,INV_GRID_POCKET}};
    if (state->catalog) {
        catalog_draw(ui,state,inv,body,u);
    } else {
        if (narrow) {
            float tw=(body.w-5*u)*.5f;
            UiRect tabs[2]={{body.x,body.y,tw,28*u},{body.x+tw+5*u,body.y,tw,28*u}};
            for (int i=0; i<2; ++i) {
                bool active=state->pack_page==(i==1);
                bool drag=state->dragging && press_moved(ui,state) && pointer_in(ui,tabs[i]);
                if (action(ui,tabs[i],i ? tr("inv.tab.storage") : tr("inv.tab.equipment"),12*u,active,false) || drag)
                    state->pack_page=i==1;
            }
            body.y+=36*u;
            body.h-=36*u;
        }
        float gutter=22*u;
        float left_w=narrow ? body.w : (body.w-gutter)*.595f;
        if (!narrow || !state->pack_page) {
            float lower_h=fminf(body.h*.245f,157*u);
            UiRect wear={body.x,body.y,left_w,body.h-lower_h-15*u};
            equipment_draw(ui,state,inv,wear,u,&targets);
            float low_y=body.y+body.h-lower_h;
            ui_draw_rect(ui,body.x,low_y-7*u,left_w,1,KIT_LINE);
            pack_summary(ui,state,inv,(UiRect){body.x,low_y,left_w,lower_h},u,narrow,&targets);
            if (!narrow) ui_draw_rect(ui,body.x+left_w+gutter*.5f,body.y,1,body.h,KIT_LINE);
        }
        if (!narrow || state->pack_page) {
            UiRect storage=narrow ? body : (UiRect){body.x+left_w+gutter,body.y,body.w-left_w-gutter,body.h};
            grid_draw(ui,state,inv,storage,u,&targets);
        }
        if (targets.pack_drop && !state->selected)
            targets.valid=pack_destination(inv,targets.origin,&targets.hit);
        inventory_click(ui,state,inv,targets.hit,targets.valid,targets.origin);
    }
    float fy=y+h-foot;
    ui_draw_rect(ui,x+pad,fy,w-2*pad,1,KIT_LINE);
    UiRect discard={x+w-pad-77*u,fy+7*u,77*u,27*u};
    float label_end=x+w-pad;
    if (!state->catalog && state->selected && !state->dragging) {
        bool confirm=state->discard_confirm;
        if (action(ui,discard,confirm ? tr("inv.discard.confirm") : tr("inv.discard"),12*u,false,confirm)) {
            if (confirm) {
                state->selected=!inventory_discard(inv,state->source);
                state->error=state->selected ? 1 : 0;
            }
            state->discard_confirm=!confirm;
        }
        label_end=discard.x-8*u;
        if (confirm) {
            UiRect cancel={discard.x-58*u,discard.y,52*u,discard.h};
            if (action(ui,cancel,tr("inv.cancel"),12*u,false,false)) state->discard_confirm=false;
            label_end=cancel.x-6*u;
        }
    }
    if (!state->catalog && !state->selected && !state->dragging && (!narrow || state->pack_page)) {
        ui_draw_rect(ui,discard.x,discard.y,discard.w,discard.h,tk_of(ui,QL_SURFACE_LO));
        ui_draw_border(ui,discard.x,discard.y,discard.w,discard.h,1,tk_of(ui,QL_LINE_SOFT));
        ui_draw_text_px_center(ui,UI_FACE_BODY,12*u,false,discard.x+discard.w*.5f,discard.y+4*u,
                               tr("inv.discard"),ui->pal.text_muted);
        UiRect sort={discard.x-82*u,discard.y,76*u,discard.h};
        if (action(ui,sort,tr("inv.sort"),12*u,false,false)) {
            int grid=state->show_pack ? INV_GRID_PACK : INV_GRID_POCKET;
            state->error=inventory_sort_grid(inv,grid) ? 4 : 5;
        }
        label_end=sort.x-8*u;
    }
    const char *msg=state->catalog ? tr("inv.catalog.hint") : tr("inv.move.hint");
    InventoryPosition focus=state->selected ? state->source : targets.hit;
    InventoryItem item=(state->selected || targets.valid) ? inventory_at(inv,&focus) : INV_ITEM_NONE;
    const InventoryItemInfo *info=inventory_item_info(item);
    char detail[240];
    if (state->error==1) {
        msg=tr("inv.move.invalid");
        if (state->selected && (state->source.slot==INV_SLOT_PACK || item==INV_ITEM_SMALL_PACK ||
            item==INV_ITEM_MEDIUM_PACK || item==INV_ITEM_LARGE_PACK))
            msg=tr("inv.move.pack");
        else if (state->selected && (state->source.slot==INV_SLOT_BODY || state->source.slot==INV_SLOT_LEGS ||
                 item==INV_ITEM_LIFE_VEST || item==INV_ITEM_TOOL_BELT || item==INV_ITEM_SPEED_PANTS))
            msg=tr("inv.move.hooks");
        if (state->selected && state->source.slot==INV_SLOT_BODY_HOOK_2 && !inventory_slot_enabled(inv,INV_SLOT_BODY_HOOK_2))
            msg=tr("inv.hook.takeOnly");
    } else if (state->error==2) msg=tr("inv.storage.full");
    else if (state->error==3) msg=tr("inv.storage.added");
    else if (state->error==4) msg=state->show_pack ? tr("inv.sort.pack") : tr("inv.sort.pockets");
    else if (state->error==5) msg=tr("inv.sort.failed");
    else if (info && focus.slot==INV_SLOT_BODY_HOOK_2 && !inventory_slot_enabled(inv,INV_SLOT_BODY_HOOK_2))
        msg=tr("inv.hook.takeOnly");
    else if (info) {
        snprintf(detail,sizeof(detail),"%s   %u×%u    %s",tr(info->name_key),(unsigned)info->width,
                 (unsigned)info->height,item_help(item));
        msg=detail;
    }
    caption(ui,(UiRect){x+pad,fy+11*u,label_end-x-pad,28*u},12*u,false,msg,
            state->error==1 || state->error==2 ? tk_of(ui,QL_PRIMARY_TEXT) : KIT_MUTED);
    if (state->dragging && press_moved(ui,state) && info) {
        UiRect ghost={ui->mouse_x+9*u,ui->mouse_y+9*u,43*u,43*u};
        ui_draw_rect(ui,ghost.x,ghost.y,ghost.w,ghost.h,tk_of(ui,QL_SURFACE));
        ui_draw_border(ui,ghost.x,ghost.y,ghost.w,ghost.h,1,KIT_ACCENT);
        item_art(ui,item,ghost,false);
    }
}
