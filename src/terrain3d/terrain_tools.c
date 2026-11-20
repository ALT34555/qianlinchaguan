#include "terrain_tools.h"
#include "../core/math_util.h"
#include "../core/i18n.h"
#include "../systems/inventory/inventory.h"
#include "../systems/world/world_delta.h"
#include "../systems/world/world_prop_edit.h"
#include "../systems/world/world_settlement.h"
#include "../systems/world/decoration_collision.h"
#include "../systems/world/model/plant_material.h"
#include <math.h>
#include <stddef.h>

static float tool_reach(InventoryItem item) {
    switch (item) {
    case INV_ITEM_LONGSWORD: return 3.8f;
    case INV_ITEM_SHORTSWORD: return 2.8f;
    case INV_ITEM_KNIFE: return 1.9f;
    case INV_ITEM_NONE: return 2.0f;
    case INV_ITEM_SAPLING: return 2.8f;
    default: return 3.4f;
    }
}

static int tool_cooldown(InventoryItem item) {
    switch (item) {
    case INV_ITEM_KNIFE: return 290;
    case INV_ITEM_SHORTSWORD: return 370;
    case INV_ITEM_LONGSWORD: return 520;
    case INV_ITEM_AXE: case INV_ITEM_PICKAXE: return 650;
    default: return 470;
    }
}

static bool tool_changes_world(InventoryItem item) {
    return (item>=INV_ITEM_HOE && item<=INV_ITEM_KNIFE) || item==INV_ITEM_SAPLING ||
           item==INV_ITEM_SOIL || item==INV_ITEM_WOOD;
}

static bool workbench_site(const RegionData *reg,float wx,float wz) {
    float x=wx-reg->center_x,z=wz-reg->center_z;
    float ground=qlcg_region_height_at(reg,x,z);
    if (!isfinite(ground) || qlcg_settlement_blocked(reg,x,ground,z,1,.8f) ||
        qlcg_region_blocked(reg,x,ground,z,1,.8f)) return false;
    const float offsets[5][2]={{0,0},{-.75f,-.65f},{.75f,-.65f},{-.75f,.65f},{.75f,.65f}};
    for (int i=0;i<5;i++) {
        float lx=x+offsets[i][0],lz=z+offsets[i][1];
        float h=qlcg_region_height_at(reg,lx,lz);
        if (!isfinite(h) || fabsf(h-ground)>.22f || qlcg_region_water_at(reg,lx,lz)>h+.02f)
            return false;
    }
    return true;
}

const char *qlcg_terrain_tool_use(TerrainRenderer *renderer, RegionData *reg, MacroWorld *w,
                                  const GameSettings *settings, bool off_hand, bool *worked) {
    *worked = false;
    if (!reg || !w) return NULL;
    Inventory *inv = &w->inventory;
    InventorySlot slot = off_hand ? INV_SLOT_OFF_HAND : INV_SLOT_MAIN_HAND;
    InventoryItem item = inventory_equipped(inv, slot);
    bool glove = item == INV_ITEM_NONE && inventory_has_equipped(inv, INV_ITEM_WITHERING_GLOVES);
    double now = qlcg_time_ms();
    if (renderer->tool_swing_ms[off_hand] > 0 &&
        now - renderer->tool_swing_ms[off_hand] < tool_cooldown(renderer->tool_swing_item[off_hand]))
        return NULL;
    renderer->tool_swing_ms[off_hand] = now;
    renderer->tool_swing_item[off_hand] = item;
    if (!glove && !tool_changes_world(item)) return NULL;

    TerrainToolHit hit;
    if (!qlcg_terrain_tool_target(renderer, reg, w, tool_reach(item), &hit)) return tr("inv.use.unreachable");
    bool plant = hit.kind == AIM_HIT_PLANT;
    bool woody = plant && (hit.category == PLANT_CAT_SHU || hit.category == PLANT_CAT_GUAN);
    bool planted = plant && (hit.state == PLANT_STATE_PLANTED || hit.state == PLANT_STATE_PLANTED_WITHERED);
    int state = 0;
    if (planted) {
        state = qlcg_chd_planted_state(w, hit.wx, hit.wz, hit.prop_kind);
        if (!state) return tr("inv.use.gone");
    } else if (hit.prop_kind) {
        state = qlcg_chd_prop_state(w, hit.wx, hit.wz, hit.prop_kind);
        if (state == CHD_PROP_REMOVED) return tr("inv.use.gone");
    }
    bool withered = plant && (state == CHD_PROP_WITHERED || state == CHD_PROP_PLANTED_WITHERED ||
                              hit.state == PLANT_STATE_WITHERED);
    InventoryItem yield = INV_ITEM_NONE;
    unsigned count = 1;
    int prop = 0, depth = 0, substrate = 0, forest = 0;
    float radius = 2.6f;
    const char *message;
    switch (item) {
    case INV_ITEM_SOIL:
        if (hit.kind!=AIM_HIT_GROUND && hit.kind!=AIM_HIT_WATER) return tr("inv.use.fill.target");
        depth=26; message=tr("inv.use.fill.done");
        break;
    case INV_ITEM_WOOD:
        hit.wx=floorf(hit.wx/2)*2+1; hit.wz=floorf(hit.wz/2)*2+1;
        if (hit.kind!=AIM_HIT_GROUND || !workbench_site(reg,hit.wx,hit.wz)) return tr("inv.use.workbench.target");
        message=tr("inv.use.workbench.done");
        break;
    case INV_ITEM_SAPLING: {
        if (hit.kind != AIM_HIT_GROUND) return tr("inv.use.sapling.target");
        int species = qlcg_plant_species_index("shu.huai");
        if (species < 0) return tr("inv.use.sapling.failed");
        hit.prop_kind = CHD_PROP_PLANT_BASE + species;
        message = tr("inv.use.sapling.done");
        break;
    }
    case INV_ITEM_NONE:
        if (!plant || withered) return tr("inv.use.glove.target");
        prop = CHD_PROP_WITHERED; message = tr("inv.use.glove.done");
        break;
    case INV_ITEM_HOE:
        if (hit.kind != AIM_HIT_GROUND) return tr("inv.use.hoe.target");
        depth = -6; substrate = 2; forest = -8; message = tr("inv.use.hoe.done");
        break;
    case INV_ITEM_SHOVEL:
        if (hit.kind != AIM_HIT_GROUND) return tr("inv.use.shovel.target");
        depth = -26; yield = INV_ITEM_SOIL; message = tr("inv.use.shovel.done");
        break;
    case INV_ITEM_AXE:
        if (!woody) return tr("inv.use.axe.target");
        prop = CHD_PROP_REMOVED; yield = INV_ITEM_WOOD;
        count = hit.category == PLANT_CAT_SHU && !planted ? 2 : 1;
        message = tr("inv.use.axe.done");
        break;
    case INV_ITEM_PICKAXE:
        if (hit.kind != AIM_HIT_ROCK) return tr("inv.use.pickaxe.target");
        prop = CHD_PROP_REMOVED; yield = INV_ITEM_STONE; count = 2; message = tr("inv.use.pickaxe.done");
        break;
    case INV_ITEM_SICKLE:
        if (!plant || (hit.category != PLANT_CAT_CAO && hit.category != PLANT_CAT_NONG))
            return tr("inv.use.sickle.target");
        prop = CHD_PROP_REMOVED; yield = INV_ITEM_FIBER; count = 2; message = tr("inv.use.sickle.done");
        break;
    case INV_ITEM_LONGSWORD:
        if (!plant || (hit.category != PLANT_CAT_GUAN && hit.category != PLANT_CAT_TENG))
            return tr("inv.use.longsword.target");
        prop = CHD_PROP_REMOVED; message = tr("inv.use.longsword.done");
        break;
    case INV_ITEM_SHORTSWORD:
        if (!plant || hit.category == PLANT_CAT_SHU || hit.category == PLANT_CAT_SHUI)
            return tr("inv.use.shortsword.target");
        prop = CHD_PROP_REMOVED; message = tr("inv.use.shortsword.done");
        break;
    default:
        if (!plant || woody || withered) return tr("inv.use.knife.target");
        prop = CHD_PROP_WITHERED; yield = INV_ITEM_FIBER; message = tr("inv.use.knife.done");
        break;
    }

    Inventory next = *inv;
    if (yield != INV_ITEM_NONE && inventory_add_count(&next, yield, count) != count)
        return tr("inv.use.inventory.full");
    bool changed;
    if (item==INV_ITEM_SAPLING || item==INV_ITEM_SOIL || item==INV_ITEM_WOOD) {
        if (!inventory_discard(&next,(InventoryPosition){.slot=slot})) return tr("inv.use.blocked");
        if (item==INV_ITEM_WOOD) changed=qlcg_chd_place_workbench(w,hit.wx,hit.wz);
        else if (item==INV_ITEM_SAPLING) changed=qlcg_chd_plant(w,hit.wx,hit.wz,hit.prop_kind);
        else changed=qlcg_chd_edit(w,hit.wx,hit.wz,radius,depth,substrate,forest);
    } else {
        changed = prop ? qlcg_chd_set_prop(w, hit.wx, hit.wz, hit.prop_kind, prop)
                       : qlcg_chd_edit(w, hit.wx, hit.wz, radius, depth, substrate, forest);
    }
    if (!changed) return tr("inv.use.blocked");
    *inv = next;
    qlcg_region_stream_sync_changed(reg,reg->climate_season,settings);
    renderer->probe_cache.valid = false;
    *worked = true;
    return message;
}

typedef struct { float x, y; } ToolPoint;
typedef struct { float position[3], color[3]; } ToolVertex;
typedef struct {
    ToolVertex vertices[4096];
    int count;
    float x, y, z, scale, c, s, tilt;
} ToolMesh;

static const float oak[3] = {.44f, .24f, .13f};
static const float leather[3] = {.26f, .13f, .082f};
static const float brass[3] = {.76f, .55f, .26f};
static const float iron[3] = {.48f, .53f, .54f};
static const float edge[3] = {.74f, .79f, .81f};

static void tool_pose(ToolMesh *mesh, float x, float y, float z, float scale, float roll, float tilt) {
    mesh->x = x; mesh->y = y; mesh->z = z; mesh->scale = scale;
    mesh->c = cosf(roll); mesh->s = sinf(roll); mesh->tilt = tilt;
}

static void tool_vertex(ToolMesh *mesh, ToolPoint p, float depth, const float color[3], float shade) {
    if (mesh->count == (int)(sizeof(mesh->vertices)/sizeof(mesh->vertices[0]))) return;
    float x = mesh->c*p.x - mesh->s*p.y;
    float y = mesh->s*p.x + mesh->c*p.y;
    ToolVertex *v = mesh->vertices + mesh->count++;
    v->position[0] = mesh->x + mesh->scale*x;
    v->position[1] = mesh->y + mesh->scale*(y - depth*.25f);
    v->position[2] = mesh->z + mesh->scale*(depth + mesh->tilt*y + x*.19f);
    for (int i = 0; i < 3; ++i) v->color[i] = color[i]*shade;
}

static void tool_prism(ToolMesh *mesh, const ToolPoint *points, int count, float z, float width,
                       const float color[3]) {
    float front = z + width*.5f, back = z - width*.5f;
    for (int i = 1; i+1 < count; ++i) {
        tool_vertex(mesh, points[0], front, color, 1.0f);
        tool_vertex(mesh, points[i], front, color, 1.0f);
        tool_vertex(mesh, points[i+1], front, color, 1.0f);
        tool_vertex(mesh, points[0], back, color, .67f);
        tool_vertex(mesh, points[i+1], back, color, .67f);
        tool_vertex(mesh, points[i], back, color, .67f);
    }
    for (int i = 0; i < count; ++i) {
        ToolPoint a = points[i], b = points[(i+1)%count];
        float shade = b.y > a.y ? .77f : .63f;
        tool_vertex(mesh, a, front, color, shade); tool_vertex(mesh, b, front, color, shade);
        tool_vertex(mesh, a, back, color, shade); tool_vertex(mesh, a, back, color, shade);
        tool_vertex(mesh, b, front, color, shade); tool_vertex(mesh, b, back, color, shade);
    }
}

static void tool_box(ToolMesh *mesh, float x, float y, float z, float width, float height, float depth,
                     const float color[3]) {
    float a = width*.5f, b = height*.5f;
    const ToolPoint points[] = {{x-a,y-b},{x+a,y-b},{x+a,y+b},{x-a,y+b}};
    tool_prism(mesh, points, 4, z, depth, color);
}

static void tool_blade(ToolMesh *mesh, InventoryItem item) {
    float tip = item == INV_ITEM_LONGSWORD ? .96f : item == INV_ITEM_SHORTSWORD ? .62f : .35f;
    float half = item == INV_ITEM_KNIFE ? .043f : .052f;
    float guard = item == INV_ITEM_KNIFE ? .13f : .24f;
    const ToolPoint blade[] = {{-half,.11f},{half,.11f},{half*.78f,tip-.10f},{0,tip},{-half*.78f,tip-.10f}};
    const ToolPoint bevel[] = {{-half,.11f},{0,.11f},{0,tip},{-half*.78f,tip-.10f}};
    tool_prism(mesh, blade, 5, 0, .023f, iron);
    tool_prism(mesh, bevel, 4, .0125f, .002f, edge);
    tool_box(mesh, 0, .105f, 0, guard, .045f, .060f, brass);
    tool_box(mesh, 0,-.027f, 0, .070f, .228f, .062f, leather);
    tool_box(mesh, 0,-.149f, 0, .086f, .045f, .079f, brass);
    for (int i = 0; i < 4; ++i)
        tool_box(mesh, 0, -.105f + i*.045f, .032f, .066f, .012f, .004f, oak);
}

static void tool_sickle(ToolMesh *mesh) {
    const ToolPoint arc[][2] = {
        {{-.047f,.365f},{.044f,.355f}}, {{-.022f,.49f},{.064f,.456f}},
        {{.078f,.585f},{.125f,.518f}}, {{.209f,.60f},{.205f,.532f}},
        {{.326f,.544f},{.295f,.496f}}, {{.38f,.438f},{.346f,.426f}},
        {{.366f,.34f},{.366f,.34f}}
    };
    for (int i = 0; i < 6; ++i) {
        ToolPoint in0 = arc[i][1], in1 = arc[i+1][1];
        ToolPoint out0 = arc[i][0], out1 = arc[i+1][0];
        const ToolPoint strip[] = {out0, out1, in1, in0};
        const ToolPoint bevel[] = {{out0.x*.27f+in0.x*.73f, out0.y*.27f+in0.y*.73f},
                                    {out1.x*.27f+in1.x*.73f, out1.y*.27f+in1.y*.73f}, in1, in0};
        tool_prism(mesh, strip, 4, 0, .027f, iron);
        tool_prism(mesh, bevel, 4, .014f, .003f, edge);
    }
}

static void tool_model(ToolMesh *mesh, InventoryItem item) {
    if (item == INV_ITEM_NONE) {
        static const float skin[3]={.65f,.43f,.30f};
        const ToolPoint fist[]={{-.098f,-.12f},{.092f,-.12f},{.115f,.078f},
                                {.069f,.15f},{-.059f,.155f},{-.102f,.10f}};
        tool_box(mesh,0,-.18f,0,.145f,.21f,.135f,skin);
        tool_prism(mesh,fist,6,.025f,.16f,skin);
        return;
    }
    if (item==INV_ITEM_SOIL || item==INV_ITEM_WOOD) {
        tool_box(mesh,0,.10f,0,.26f,.21f,.23f,item==INV_ITEM_SOIL?leather:oak);
        return;
    }
    if (item == INV_ITEM_STONE) {
        const ToolPoint stone[]={{-.132f,.013f},{.093f,.006f},{.164f,.121f},
                                 {.105f,.237f},{-.045f,.263f},{-.159f,.158f}};
        tool_prism(mesh,stone,6,0,.15f,iron);
        return;
    }
    if (item == INV_ITEM_FIBER) {
        static const float fiber[3]={.71f,.59f,.34f};
        for (int i=0;i<5;i++) {
            float x=(i-2)*.045f, y=.40f+(i%3)*.034f;
            const ToolPoint strand[]={{x*.46f-.014f,-.061f},{x*.46f+.012f,-.061f},
                                      {x+.018f,y},{x-.008f,y}};
            tool_prism(mesh,strand,4,.005f*(i%2),.021f,fiber);
        }
        tool_box(mesh,0,.124f,.025f,.137f,.047f,.054f,leather);
        return;
    }
    if (item == INV_ITEM_SAPLING) {
        static const float leaf[3] = {.35f, .53f, .20f};
        const ToolPoint left[] = {{0,.23f},{-.12f,.25f},{-.18f,.39f},{-.064f,.38f}};
        const ToolPoint right[] = {{0,.34f},{.065f,.46f},{.16f,.49f},{.111f,.365f}};
        const ToolPoint tip[] = {{-.038f,.43f},{-.055f,.536f},{.017f,.61f},{.055f,.517f}};
        tool_box(mesh, 0, .22f, 0, .038f, .56f, .035f, oak);
        tool_box(mesh, 0,-.078f, 0, .12f, .12f, .11f, leather);
        tool_prism(mesh, left, 4, 0, .019f, leaf);
        tool_prism(mesh, right, 4, .022f, .021f, leaf);
        tool_prism(mesh, tip, 4, -.009f, .018f, leaf);
        return;
    }
    if (item >= INV_ITEM_LONGSWORD && item <= INV_ITEM_KNIFE) {
        tool_blade(mesh, item);
        return;
    }
    if (item < INV_ITEM_HOE || item > INV_ITEM_SICKLE) {
        const InventoryItemInfo *info=inventory_item_info(item);
        if (!info) return;
        float width=info->category==INV_CATEGORY_PACKS ? .34f : .245f;
        float height=info->category==INV_CATEGORY_PACKS ? .44f : .29f;
        float depth=info->category==INV_CATEGORY_PACKS ? .19f : .13f;
        const float *material=info->category==INV_CATEGORY_ACCESSORIES ? iron : leather;
        const ToolPoint parcel[]={{-width*.5f,0},{width*.5f,0},{width*.54f,height*.65f},
                                  {width*.34f,height},{-width*.34f,height},{-width*.54f,height*.65f}};
        tool_prism(mesh,parcel,6,0,depth,material);
        tool_box(mesh,0,height*.54f,depth*.51f,width,.043f,.014f,oak);
        tool_box(mesh,0,height*.54f,depth*.56f,.062f,.055f,.021f,brass);
        return;
    }
    float shaft = item == INV_ITEM_SICKLE ? .39f : item == INV_ITEM_AXE ? .60f : .79f;
    tool_box(mesh, 0, (shaft-.21f)*.5f, 0, .043f, shaft+.21f, .043f, oak);
    tool_box(mesh, 0,-.12f, 0, .051f, .16f, .051f, leather);
    tool_box(mesh, 0, shaft-.015f, 0, .058f, .081f, .054f, iron);
    switch (item) {
    case INV_ITEM_HOE: {
        const ToolPoint hoe[] = {{-.027f,.83f},{.088f,.82f},{.286f,.665f},{.236f,.618f},{.052f,.721f}};
        tool_prism(mesh, hoe, 5, 0, .055f, iron);
        const ToolPoint lip[] = {{.236f,.618f},{.286f,.665f},{.252f,.68f},{.208f,.64f}};
        tool_prism(mesh, lip, 4, .024f, .009f, edge);
        break;
    }
    case INV_ITEM_SHOVEL: {
        const ToolPoint spade[] = {{-.135f,.765f},{.135f,.765f},{.13f,.925f},
                                    {.074f,1.024f},{0,1.071f},{-.074f,1.024f},{-.13f,.925f}};
        tool_prism(mesh, spade, 7, 0, .041f, iron);
        const ToolPoint rib[] = {{-.027f,.767f},{.027f,.767f},{0,1.026f}};
        tool_prism(mesh, rib, 3, .026f, .011f, edge);
        break;
    }
    case INV_ITEM_AXE: {
        const ToolPoint axe[] = {{-.043f,.536f},{.105f,.512f},{.235f,.469f},
                                  {.294f,.577f},{.268f,.714f},{.095f,.665f},{-.043f,.637f}};
        tool_prism(mesh, axe, 7, 0, .066f, iron);
        const ToolPoint bevel[] = {{.235f,.469f},{.294f,.577f},{.268f,.714f},
                                   {.232f,.703f},{.253f,.579f},{.201f,.48f}};
        tool_prism(mesh, bevel, 6, .033f, .006f, edge);
        break;
    }
    case INV_ITEM_PICKAXE: {
        const ToolPoint pick[] = {{-.311f,.671f},{-.137f,.798f},{0,.837f},
                                   {.159f,.79f},{.324f,.636f},{.142f,.711f},{0,.765f},{-.136f,.728f}};
        tool_prism(mesh, pick, 8, 0, .054f, iron);
        break;
    }
    case INV_ITEM_SICKLE: tool_sickle(mesh); break;
    default: break;
    }
}

static void tool_belt(ToolMesh *mesh, const Inventory *inv, float x_limit, float y_limit) {
    float x = fminf(.48f, x_limit*.66f), y = -y_limit*.71f;
    tool_pose(mesh, 0, y, -1.10f, 1, 0, 0);
    tool_box(mesh, 0, 0, 0, x*2, .077f, .037f, leather);
    tool_box(mesh, 0, .032f, .022f, x*2, .008f, .012f, oak);
    tool_box(mesh, 0,-.032f, .022f, x*2, .008f, .012f, oak);
    tool_box(mesh, 0, 0, .037f, .128f, .092f, .035f, brass);
    tool_box(mesh, 0, 0, .059f, .081f, .049f, .014f, leather);
    tool_box(mesh, 0, 0, .069f, .069f, .010f, .010f, brass);
    float hook = -x*.71f;
    tool_pose(mesh, hook, y-.053f, -1.04f, 1, 0, 0);
    tool_box(mesh, -.029f, 0, 0, .015f, .087f, .024f, brass);
    tool_box(mesh, .029f, 0, 0, .015f, .087f, .024f, brass);
    tool_box(mesh, 0, -.038f, 0, .066f, .015f, .024f, brass);
    tool_pose(mesh, hook, y-.083f, -1.02f, .39f, -1.87f, -.18f);
    InventoryItem suspended=inventory_equipped(inv,INV_SLOT_TOOL_1);
    if (suspended) tool_model(mesh,suspended);
}

static bool setup_tool_render(TerrainRenderer *tr) {
    if (tr->tool_program) return true;
    static const char *vertex =
        "#version 330 core\n"
        "layout(location=0) in vec3 inPosition;layout(location=1) in vec3 inColor;\n"
        "uniform mat4 proj;uniform bool foreground;out vec3 color;\n"
        "void main(){gl_Position=proj*vec4(inPosition,1.0);\n"
        "if(foreground)gl_Position.z=(gl_Position.z+gl_Position.w)*.025-gl_Position.w;color=inColor;}\n";
    static const char *fragment =
        "#version 330 core\n"
        "in vec3 color;uniform float light;layout(location=0) out vec4 frag;\n"
        "layout(location=1) out vec4 ground;\n"
        "void main(){frag=vec4(color*light,1.0);ground=vec4(0.0);}\n";
    tr->tool_program = qlcg_gl_compile_program(vertex, fragment);
    if (!tr->tool_program) return false;
    glGenVertexArrays(1, &tr->tool_vao);
    glGenBuffers(1, &tr->tool_vbo);
    glBindVertexArray(tr->tool_vao);
    glBindBuffer(GL_ARRAY_BUFFER, tr->tool_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ToolVertex), (const void *)offsetof(ToolVertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ToolVertex), (const void *)offsetof(ToolVertex, color));
    return true;
}

typedef struct {
    InventoryItem item;
    float x,y,z,scale,roll,tilt;
} HandPose;

float qlcg_terrain_hand_swing(const TerrainRenderer *tr,int hand,double now,InventoryItem *active) {
    float elapsed=(float)(now-tr->tool_swing_ms[hand]);
    int duration=tool_cooldown(tr->tool_swing_item[hand]);
    if (tr->tool_swing_ms[hand]<=0 || elapsed<0 || elapsed>=duration) return -1;
    if (active) *active=tr->tool_swing_item[hand];
    return sinf(QLCG_PI*elapsed/duration);
}

static bool hand_pose(const TerrainRenderer *tr,const Inventory *inv,int hand,double now,
                      float x_limit,float y_limit,HandPose *pose) {
    InventoryItem item=inventory_equipped(inv,hand ? INV_SLOT_OFF_HAND : INV_SLOT_MAIN_HAND);
    float swing=qlcg_terrain_hand_swing(tr,hand,now,&item);
    if (!item && swing<0) return false;
    float side=hand ? -1.0f : 1.0f;
    swing=fmaxf(0,swing);
    *pose=(HandPose){item,side*x_limit*(.61f-.31f*swing),y_limit*(-.62f+.16f*swing),
                     -.92f-.45f*swing,fminf(.74f,y_limit*.78f),
                     side*(.39f+1.48f*swing),-.25f-.29f*swing};
    return true;
}

unsigned qlcg_terrain_hand_sprites(const TerrainRenderer *tr,const MacroWorld *w,
                                   float fov,float width,float height,TerrainHandSprite out[2]) {
    if (!w || width<=0 || height<=0) return 0;
    float half_tan=tanf(fov*QLCG_PI/360.0f);
    float y_limit=1.10f*half_tan,x_limit=y_limit*width/height;
    double now=qlcg_time_ms();
    unsigned mask=0;
    for (int i=0;i<2;++i) {
        HandPose pose;
        if (!hand_pose(tr,&w->inventory,i,now,x_limit,y_limit,&pose)) continue;
        const InventoryItemInfo *info=inventory_item_info(pose.item);
        if (!info || info->category>INV_CATEGORY_ACCESSORIES) continue;
        float up=.18f*pose.scale;
        float x=pose.x-sinf(pose.roll)*up,y=pose.y+cosf(pose.roll)*up;
        float focal=.5f*height/(half_tan*-pose.z);
        out[i]=(TerrainHandSprite){pose.item,width*.5f+x*focal,height*.5f-y*focal,
                                   .50f*pose.scale*focal,-pose.roll};
        mask|=1u<<i;
    }
    return mask;
}

static void draw_tool_mesh(TerrainRenderer *tr,const ToolMesh *mesh,const float proj[16],float daylight,bool foreground) {
    if (!mesh->count || !setup_tool_render(tr)) return;
    glUseProgram(tr->tool_program);
    glUniformMatrix4fv(qlcg_gl_uniform(tr->tool_program, "proj"), 1, GL_FALSE, proj);
    glUniform1i(qlcg_gl_uniform(tr->tool_program, "foreground"), foreground);
    glUniform1f(qlcg_gl_uniform(tr->tool_program, "light"), qlcg_clampf(daylight, .45f, 1.15f));
    glBindVertexArray(tr->tool_vao);
    glBindBuffer(GL_ARRAY_BUFFER, tr->tool_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(mesh->count*sizeof(ToolVertex)), mesh->vertices, GL_STREAM_DRAW);
    GLboolean culled = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLES, 0, mesh->count);
    if (culled) glEnable(GL_CULL_FACE);
}

void qlcg_terrain_tools_draw(TerrainRenderer *tr, const MacroWorld *w, const float proj[16], float daylight) {
    const Inventory *inv = &w->inventory;
    bool belt = inventory_equipped(inv, INV_SLOT_LEGS) == INV_ITEM_TOOL_BELT;
    ToolMesh mesh;
    mesh.count = 0;
    float x_limit = 1.10f / proj[0], y_limit = 1.10f / proj[5];
    if (belt) tool_belt(&mesh, inv, x_limit, y_limit);
    double now = qlcg_time_ms();
    for (int i = 0; i < 2; ++i) {
        if (tr->tool_sprite_mask & (1u<<i)) continue;
        HandPose pose;
        if (!hand_pose(tr,inv,i,now,x_limit,y_limit,&pose)) continue;
        tool_pose(&mesh,pose.x,pose.y,pose.z,pose.scale,pose.roll,pose.tilt);
        tool_model(&mesh,pose.item);
    }
    draw_tool_mesh(tr,&mesh,proj,daylight,true);
}

void qlcg_terrain_tools_world_draw(TerrainRenderer *tr,const MacroWorld *w,const float vp[16],
                                   const float wrists[2][3],float body_scale,float daylight) {
    ToolMesh mesh;
    mesh.count=0;
    float sy=sinf(tr->yaw),cy=cosf(tr->yaw);
    double now=qlcg_time_ms();
    for (int hand=0; hand<2; ++hand) {
        InventoryItem item=inventory_equipped(&w->inventory,hand ? INV_SLOT_OFF_HAND : INV_SLOT_MAIN_HAND);
        float swing=fmaxf(0,qlcg_terrain_hand_swing(tr,hand,now,&item));
        if (!item) continue;
        int start=mesh.count;
        tool_pose(&mesh,0,0,0,body_scale*.70f,hand ? -.20f : .20f,0);
        tool_model(&mesh,item);
        float pitch=.76f-swing*1.3f, vertical=sinf(pitch),forward=cosf(pitch);
        for (int i=start;i<mesh.count;++i) {
            float *p=mesh.vertices[i].position;
            float x=p[0], y=p[1], z=p[2], along=forward*y-vertical*z;
            p[0]=wrists[hand][0]+cy*x+sy*along;
            p[1]=wrists[hand][1]+vertical*y+forward*z;
            p[2]=wrists[hand][2]+sy*x-cy*along;
        }
    }
    draw_tool_mesh(tr,&mesh,vp,daylight,false);
}

void qlcg_terrain_tools_free(TerrainRenderer *tr) {
    qlcg_gl_delete_vertex_arrays(1, &tr->tool_vao);
    qlcg_gl_delete_buffers(1, &tr->tool_vbo);
    qlcg_gl_delete_program(tr->tool_program);
    tr->tool_vao = tr->tool_vbo = tr->tool_program = 0;
}
