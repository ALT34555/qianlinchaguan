#include "inventory.h"
#include <string.h>

#define SLOT(s) (1u << (INV_SLOT_##s))
#define ACCESSORIES (SLOT(ACCESSORY_1) | SLOT(ACCESSORY_2) | SLOT(ACCESSORY_3))
#define HOOKS (SLOT(TOOL_1) | SLOT(BODY_HOOK_1) | SLOT(BODY_HOOK_2) | \
               SLOT(BODY_HOOK_3) | SLOT(LEG_HOOK_2) | SLOT(LEG_HOOK_3))
#define HANDS (SLOT(MAIN_HAND) | SLOT(OFF_HAND))
#define TOOLS (HANDS | HOOKS)

enum { INV_ITEM_MAX_WIDTH = 3 };

static const InventoryItemInfo items[INV_ITEM_COUNT] = {
    [INV_ITEM_SMALL_PACK] = {"inv.item.small_pack.name", "inv.item.small_pack.help", 1, 1, INV_CATEGORY_PACKS, SLOT(PACK) | HANDS},
    [INV_ITEM_MEDIUM_PACK] = {"inv.item.medium_pack.name", "inv.item.medium_pack.help", 1, 1, INV_CATEGORY_PACKS, SLOT(PACK) | HANDS},
    [INV_ITEM_LARGE_PACK] = {"inv.item.large_pack.name", "inv.item.large_pack.help", 1, 1, INV_CATEGORY_PACKS, SLOT(PACK) | HANDS},
    [INV_ITEM_HEADLAMP] = {"inv.item.headlamp.name", "inv.item.headlamp.help", 1, 1, INV_CATEGORY_CLOTHING, SLOT(HEAD) | HANDS},
    [INV_ITEM_LIFE_VEST] = {"inv.item.life_vest.name", "inv.item.life_vest.help", 1, 1, INV_CATEGORY_CLOTHING, SLOT(BODY) | HANDS},
    [INV_ITEM_WITHERING_GLOVES] = {"inv.item.withering_gloves.name", "inv.item.withering_gloves.help", 1, 1, INV_CATEGORY_CLOTHING, SLOT(HANDS) | HANDS},
    [INV_ITEM_SPEED_PANTS] = {"inv.item.speed_pants.name", "inv.item.speed_pants.help", 1, 1, INV_CATEGORY_CLOTHING, SLOT(LEGS) | HANDS},
    [INV_ITEM_TOOL_BELT] = {"inv.item.tool_belt.name", "inv.item.tool_belt.help", 1, 1, INV_CATEGORY_CLOTHING, SLOT(LEGS) | HANDS},
    [INV_ITEM_SPRING_SHOES] = {"inv.item.spring_shoes.name", "inv.item.spring_shoes.help", 1, 1, INV_CATEGORY_CLOTHING, SLOT(FEET) | HANDS},
    [INV_ITEM_WINGS] = {"inv.item.wings.name", "inv.item.wings.help", 1, 1, INV_CATEGORY_ACCESSORIES, ACCESSORIES | HANDS},
    [INV_ITEM_STARGAZING_GLASSES] = {"inv.item.stargazing_glasses.name", "inv.item.stargazing_glasses.help", 1, 1, INV_CATEGORY_ACCESSORIES, ACCESSORIES | HANDS},
    [INV_ITEM_NIGHT_VISION_GLASSES] = {"inv.item.night_vision_glasses.name", "inv.item.night_vision_glasses.help", 1, 1, INV_CATEGORY_ACCESSORIES, ACCESSORIES | HANDS},
    [INV_ITEM_AQUATIC_RING] = {"inv.item.aquatic_ring.name", "inv.item.aquatic_ring.help", 1, 1, INV_CATEGORY_ACCESSORIES, ACCESSORIES | HANDS},
    [INV_ITEM_HOE] = {"inv.item.hoe.name", "inv.item.hoe.help", 2, 1, INV_CATEGORY_TOOLS, TOOLS},
    [INV_ITEM_SHOVEL] = {"inv.item.shovel.name", "inv.item.shovel.help", 2, 1, INV_CATEGORY_TOOLS, TOOLS},
    [INV_ITEM_AXE] = {"inv.item.axe.name", "inv.item.axe.help", 2, 1, INV_CATEGORY_TOOLS, TOOLS},
    [INV_ITEM_PICKAXE] = {"inv.item.pickaxe.name", "inv.item.pickaxe.help", 2, 1, INV_CATEGORY_TOOLS, TOOLS},
    [INV_ITEM_SICKLE] = {"inv.item.sickle.name", "inv.item.sickle.help", 1, 1, INV_CATEGORY_TOOLS, TOOLS},
    [INV_ITEM_LONGSWORD] = {"inv.item.longsword.name", "inv.item.longsword.help", 3, 1, INV_CATEGORY_TOOLS, TOOLS},
    [INV_ITEM_SHORTSWORD] = {"inv.item.shortsword.name", "inv.item.shortsword.help", 1, 1, INV_CATEGORY_TOOLS, TOOLS},
    [INV_ITEM_KNIFE] = {"inv.item.knife.name", "inv.item.knife.help", 1, 1, INV_CATEGORY_TOOLS, TOOLS},
    [INV_ITEM_SOIL] = {"inv.item.soil.name", "inv.item.soil.help", 1, 1, INV_CATEGORY_MATERIALS, HANDS},
    [INV_ITEM_WOOD] = {"inv.item.wood.name", "inv.item.wood.help", 1, 1, INV_CATEGORY_MATERIALS, HANDS},
    [INV_ITEM_STONE] = {"inv.item.stone.name", "inv.item.stone.help", 1, 1, INV_CATEGORY_MATERIALS, HANDS},
    [INV_ITEM_FIBER] = {"inv.item.fiber.name", "inv.item.fiber.help", 1, 1, INV_CATEGORY_MATERIALS, HANDS},
    [INV_ITEM_SAPLING] = {"inv.item.sapling.name", "inv.item.sapling.help", 1, 1, INV_CATEGORY_MATERIALS, HANDS},
};

const InventoryItemInfo *inventory_item_info(InventoryItem item) {
    return item > INV_ITEM_NONE && item < INV_ITEM_COUNT ? &items[item] : NULL;
}

void inventory_reset(Inventory *inv) {
    memset(inv, 0, sizeof(*inv));
}

void inventory_seed_survival(Inventory *inv) {
    inventory_reset(inv);
    for (int item = INV_ITEM_SMALL_PACK; item <= INV_ITEM_KNIFE; item++)
        inventory_add(inv, (InventoryItem)item);
    inventory_add(inv, INV_ITEM_SAPLING);
}

void inventory_grid_dimensions(const Inventory *inv, int grid, int *width, int *height) {
    *width = *height = 0;
    if (grid == INV_GRID_POCKET) {
        *width = INV_POCKET_W;
        *height = INV_POCKET_H;
        return;
    }
    if (grid != INV_GRID_PACK) return;
    switch (inv->equipped[INV_SLOT_PACK]) {
        case INV_ITEM_SMALL_PACK:  *width = 10; *height = 16; break;
        case INV_ITEM_MEDIUM_PACK: *width = 15; *height = 20; break;
        case INV_ITEM_LARGE_PACK:  *width = 18; *height = 28; break;
    }
}

static const uint8_t *grid_row(const Inventory *inv, int grid, int y) {
    return grid == INV_GRID_POCKET ? inv->pocket[y] : inv->pack[y];
}

InventoryItem inventory_grid_item(const Inventory *inv, int grid, int x, int y) {
    int width, height;
    inventory_grid_dimensions(inv, grid, &width, &height);
    if (x < 0 || y < 0 || x >= width || y >= height) return INV_ITEM_NONE;
    return (InventoryItem)grid_row(inv, grid, y)[x];
}

static int hook_count(InventoryItem garment) {
    switch (garment) {
        case INV_ITEM_LIFE_VEST:
        case INV_ITEM_SPEED_PANTS:
        case INV_ITEM_TOOL_BELT: return 1;
        default: return 0;
    }
}

bool inventory_slot_enabled(const Inventory *inv, InventorySlot slot) {
    if (slot < 0 || slot >= INV_SLOT_COUNT) return false;
    if (slot >= INV_SLOT_BODY_HOOK_1 && slot <= INV_SLOT_BODY_HOOK_3)
        return slot - INV_SLOT_BODY_HOOK_1 < hook_count(inv->equipped[INV_SLOT_BODY]);
    if (slot == INV_SLOT_TOOL_1)
        return hook_count(inv->equipped[INV_SLOT_LEGS]) >= 1;
    if (slot >= INV_SLOT_LEG_HOOK_2)
        return slot - INV_SLOT_LEG_HOOK_2 + 2 <= hook_count(inv->equipped[INV_SLOT_LEGS]);
    return true;
}

InventoryItem inventory_equipped(const Inventory *inv, InventorySlot slot) {
    bool legacy_hook = slot == INV_SLOT_BODY_HOOK_2 &&
                       inv->equipped[INV_SLOT_BODY] == INV_ITEM_LIFE_VEST;
    return legacy_hook || inventory_slot_enabled(inv, slot)
               ? (InventoryItem)inv->equipped[slot] : INV_ITEM_NONE;
}

bool inventory_has_equipped(const Inventory *inv, InventoryItem item) {
    const InventoryItemInfo *info = inventory_item_info(item);
    if (!info) return false;
    bool wearable = info->category == INV_CATEGORY_PACKS || info->category == INV_CATEGORY_CLOTHING ||
                    info->category == INV_CATEGORY_ACCESSORIES;
    int limit = wearable ? INV_SLOT_MAIN_HAND : INV_SLOT_TOOL_1;
    for (int i = 0; i < limit; i++) {
        if (inventory_equipped(inv, (InventorySlot)i) == item) return true;
    }
    return false;
}

InventoryItem inventory_at(const Inventory *inv, InventoryPosition *position) {
    if (position->slot >= 0) return inventory_equipped(inv, (InventorySlot)position->slot);
    int width, height;
    inventory_grid_dimensions(inv, position->slot, &width, &height);
    int x = position->x, y = position->y;
    if (x < 0 || y < 0 || x >= width || y >= height) return INV_ITEM_NONE;

    const uint8_t *row = grid_row(inv, position->slot, y);
    for (int ax = x; ax >= 0 && x - ax < INV_ITEM_MAX_WIDTH; ax--) {
        InventoryItem item = (InventoryItem)row[ax];
        const InventoryItemInfo *info = inventory_item_info(item);
        if (info && ax + info->width > x) {
            position->x = ax;
            return item;
        }
    }
    return INV_ITEM_NONE;
}

static bool grid_validate(const Inventory *inv, int grid, int max_w, int max_h) {
    int width, height;
    inventory_grid_dimensions(inv, grid, &width, &height);
    for (int y = 0; y < max_h; y++) {
        const uint8_t *row = grid_row(inv, grid, y);
        int edge = 0;
        for (int x = 0; x < max_w; x++) {
            if (!row[x]) continue;
            const InventoryItemInfo *info = inventory_item_info((InventoryItem)row[x]);
            if (!info || x < edge || x + info->width > width || y + info->height > height)
                return false;
            edge = x + info->width;
        }
    }
    return true;
}

bool inventory_validate(const Inventory *inv) {
    for (int slot = 0; slot < INV_SLOT_COUNT; slot++) {
        if (!inv->equipped[slot]) continue;
        const InventoryItemInfo *info = inventory_item_info((InventoryItem)inv->equipped[slot]);
        if (!info || inventory_equipped(inv, (InventorySlot)slot) == INV_ITEM_NONE ||
            !(info->slots & (1u << slot))) return false;
    }
    return grid_validate(inv, INV_GRID_POCKET, INV_POCKET_W, INV_POCKET_H) &&
           grid_validate(inv, INV_GRID_PACK, INV_MAX_W, INV_MAX_H);
}

static unsigned grid_add(Inventory *inv, int grid, InventoryItem item,
                         const InventoryItemInfo *info, unsigned count) {
    int width, height;
    unsigned added = 0;
    inventory_grid_dimensions(inv, grid, &width, &height);
    for (int y = 0; y + info->height <= height; y++) {
        uint8_t *row = grid == INV_GRID_POCKET ? inv->pocket[y] : inv->pack[y];
        int run = 0;
        int edge = 0;
        for (int x = 0; x < width; x++) {
            if (row[x]) {
                const InventoryItemInfo *existing = inventory_item_info((InventoryItem)row[x]);
                if (!existing) return added;
                edge = x + existing->width;
                run = 0;
            } else if (x >= edge && ++run == info->width) {
                row[x - run + 1] = (uint8_t)item;
                run = 0;
                if (++added == count) return added;
            }
        }
    }
    return added;
}

unsigned inventory_add_count(Inventory *inv, InventoryItem item, unsigned count) {
    const InventoryItemInfo *info = inventory_item_info(item);
    if (!info || !count) return 0;
    unsigned added = grid_add(inv, INV_GRID_POCKET, item, info, count);
    if (added < count) added += grid_add(inv, INV_GRID_PACK, item, info, count - added);
    return added;
}

bool inventory_add(Inventory *inv, InventoryItem item) {
    return inventory_add_count(inv, item, 1) == 1;
}

void inventory_recover_legacy_hook(Inventory *inv) {
    InventoryItem item = inventory_equipped(inv, INV_SLOT_BODY_HOOK_2);
    if (item && inventory_add(inv, item)) inv->equipped[INV_SLOT_BODY_HOOK_2] = INV_ITEM_NONE;
}

bool inventory_sort_grid(Inventory *inv, int grid) {
    int width, height;
    inventory_grid_dimensions(inv, grid, &width, &height);
    if (!width || !inventory_validate(inv)) return false;

    unsigned counts[INV_ITEM_COUNT] = {0};
    for (int y = 0; y < height; y++) {
        const uint8_t *row = grid_row(inv, grid, y);
        for (int x = 0; x < width; x++)
            if (row[x]) counts[row[x]]++;
    }

    Inventory next = *inv;
    if (grid == INV_GRID_POCKET) memset(next.pocket, 0, sizeof(next.pocket));
    else memset(next.pack, 0, sizeof(next.pack));
    for (int size = INV_ITEM_MAX_WIDTH; size >= 1; size--) {
        for (int item = 1; item < INV_ITEM_COUNT; item++) {
            if (counts[item] && items[item].width == size &&
                grid_add(&next, grid, (InventoryItem)item, &items[item], counts[item]) != counts[item])
                return false;
        }
    }
    *inv = next;
    return true;
}

static uint8_t *position_cell(Inventory *inv, InventoryPosition p) {
    if (p.slot >= 0) return p.slot < INV_SLOT_COUNT ? &inv->equipped[p.slot] : NULL;
    if (p.x < 0 || p.y < 0) return NULL;
    if (p.slot == INV_GRID_POCKET && p.x < INV_POCKET_W && p.y < INV_POCKET_H)
        return &inv->pocket[p.y][p.x];
    if (p.slot == INV_GRID_PACK && p.x < INV_MAX_W && p.y < INV_MAX_H)
        return &inv->pack[p.y][p.x];
    return NULL;
}

bool inventory_move(Inventory *inv, InventoryPosition from, InventoryPosition to) {
    InventoryItem item = inventory_at(inv, &from);
    if (!item) return false;

    InventoryPosition destination = to;
    InventoryItem previous = inventory_at(inv, &destination);
    if (destination.slot == INV_SLOT_BODY_HOOK_2 ||
        (from.slot == INV_SLOT_BODY_HOOK_2 && previous)) return false;
    if (from.slot == destination.slot &&
        (from.slot >= 0 || (from.x == destination.x && from.y == destination.y))) {
        previous = INV_ITEM_NONE;
        destination = to;
    }
    Inventory next = *inv;
    uint8_t *source = position_cell(&next, from);
    uint8_t *target = position_cell(&next, destination);
    if (!target) return false;
    *source = (uint8_t)previous;
    *target = (uint8_t)item;
    if (!inventory_validate(&next)) return false;
    *inv = next;
    return true;
}

bool inventory_discard(Inventory *inv, InventoryPosition position) {
    if (!inventory_at(inv, &position)) return false;
    Inventory next = *inv;
    *position_cell(&next, position) = INV_ITEM_NONE;
    if (!inventory_validate(&next)) return false;
    *inv = next;
    return true;
}
