#ifndef QLCG_PANEL_INVENTORY_H
#define QLCG_PANEL_INVENTORY_H

#include "systems/inventory/inventory.h"

struct UiContext;

typedef struct {
    bool open, catalog, selected, dragging, discard_confirm, press_pending;
    bool pack_page, show_pack, magnified, from_menu;
    int grid_page;
    InventoryCategory category;
    InventoryPosition source;
    int start_x, start_y;
    int error;
} InventoryUi;

void game_inventory_toggle(void);
void game_inventory_pointer_down(struct UiContext *ui);
void game_inventory_pointer_cancel(void);
void game_inventory_panel(struct UiContext *ui);
bool game_inventory_atlas_ready(struct UiContext *ui);
void game_inventory_tile(InventoryItem item, bool landscape, bool ghost, float uv[4]);

#endif
