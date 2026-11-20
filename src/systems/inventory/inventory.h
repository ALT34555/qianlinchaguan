#ifndef QLCG_INVENTORY_H
#define QLCG_INVENTORY_H

#include <stdbool.h>
#include <stdint.h>

enum {
    INV_GRID_PACK = -2, INV_GRID_POCKET = -1,
    INV_POCKET_W = 5, INV_POCKET_H = 8, INV_MAX_W = 18, INV_MAX_H = 28
};

typedef enum {
    INV_ITEM_NONE,
    INV_ITEM_SMALL_PACK, INV_ITEM_MEDIUM_PACK, INV_ITEM_LARGE_PACK,
    INV_ITEM_HEADLAMP, INV_ITEM_LIFE_VEST, INV_ITEM_WITHERING_GLOVES,
    INV_ITEM_SPEED_PANTS, INV_ITEM_TOOL_BELT, INV_ITEM_SPRING_SHOES,
    INV_ITEM_WINGS, INV_ITEM_STARGAZING_GLASSES, INV_ITEM_NIGHT_VISION_GLASSES,
    INV_ITEM_AQUATIC_RING,
    INV_ITEM_HOE, INV_ITEM_SHOVEL, INV_ITEM_AXE, INV_ITEM_PICKAXE,
    INV_ITEM_SICKLE, INV_ITEM_LONGSWORD, INV_ITEM_SHORTSWORD, INV_ITEM_KNIFE,
    INV_ITEM_SOIL, INV_ITEM_WOOD, INV_ITEM_STONE, INV_ITEM_FIBER, INV_ITEM_SAPLING,
    INV_ITEM_COUNT
} InventoryItem;

typedef enum {
    INV_SLOT_PACK, INV_SLOT_HEAD, INV_SLOT_BODY, INV_SLOT_HANDS,
    INV_SLOT_LEGS, INV_SLOT_FEET, INV_SLOT_ACCESSORY_1, INV_SLOT_ACCESSORY_2,
    INV_SLOT_ACCESSORY_3, INV_SLOT_MAIN_HAND, INV_SLOT_OFF_HAND,
    INV_SLOT_TOOL_1, INV_SLOT_BODY_HOOK_1, INV_SLOT_BODY_HOOK_2, INV_SLOT_BODY_HOOK_3,
    INV_SLOT_LEG_HOOK_2, INV_SLOT_LEG_HOOK_3, INV_SLOT_COUNT
} InventorySlot;

typedef enum {
    INV_CATEGORY_PACKS, INV_CATEGORY_CLOTHING, INV_CATEGORY_ACCESSORIES,
    INV_CATEGORY_TOOLS, INV_CATEGORY_MATERIALS, INV_CATEGORY_COUNT
} InventoryCategory;

typedef struct {
    const char *name_key, *help_key;
    uint8_t width, height;
    InventoryCategory category;
    uint32_t slots;
} InventoryItemInfo;

typedef struct {
    uint8_t equipped[INV_SLOT_COUNT];
    uint8_t pocket[INV_POCKET_H][INV_POCKET_W];
    uint8_t pack[INV_MAX_H][INV_MAX_W];
} Inventory;

typedef struct { int x, y, slot; } InventoryPosition;

const InventoryItemInfo *inventory_item_info(InventoryItem item);
void inventory_reset(Inventory *inv);
void inventory_seed_survival(Inventory *inv);
void inventory_grid_dimensions(const Inventory *inv, int grid, int *width, int *height);
InventoryItem inventory_grid_item(const Inventory *inv, int grid, int x, int y);
bool inventory_slot_enabled(const Inventory *inv, InventorySlot slot);
InventoryItem inventory_equipped(const Inventory *inv, InventorySlot slot);
bool inventory_has_equipped(const Inventory *inv, InventoryItem item);
InventoryItem inventory_at(const Inventory *inv, InventoryPosition *position);
bool inventory_validate(const Inventory *inv);
void inventory_recover_legacy_hook(Inventory *inv);
bool inventory_add(Inventory *inv, InventoryItem item);
unsigned inventory_add_count(Inventory *inv, InventoryItem item, unsigned count);
bool inventory_move(Inventory *inv, InventoryPosition from, InventoryPosition to);
bool inventory_discard(Inventory *inv, InventoryPosition position);
bool inventory_sort_grid(Inventory *inv, int grid);

#endif
