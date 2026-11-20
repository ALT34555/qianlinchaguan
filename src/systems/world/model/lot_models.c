#include "../world_lot.h"
#include "editor_buildings.h"
#include "../../dolls/doll_life.h"

static LpGeometry geometry[LOT_MODEL_END-LOT_TEA];
#define ITEM(key,label,x,y,z,z0) { .id=key, .name=label, .unit_id=key, .pack="lot", .kind=3, .bounds_min={-x,0,z0}, .bounds_max={x,y,z} }
static QlcgBuildingModel catalog[]={
    ITEM("tea","双杯茶席",.30,.85,1.10,-.23),
    ITEM("fence","竹篱笆",1,1.05,.10,-.10),
    ITEM("wall","矮石墙",1,.90,.20,-.20),
    ITEM("steps","石阶",1,.60,1,-1),
    ITEM("deck","木栈台",1,.60,1,-1),
    ITEM("lamp","庭院石灯",.32,1.60,.32,-.32),
    ITEM("palisade","粗木栅栏",1,1.2,.12,-.12),
    ITEM("garden_wall","粉墙黛瓦院墙",1,1.75,.26,-.26),
    ITEM("gate","双柱柴门",1,1.8,.19,-.19),
    ITEM("wide_gate","四米院门",2,2.15,.24,-.24),
    ITEM("wood_chair","单人木椅",.27,.9,.27,-.27),
    ITEM("stump","原木坐桩",.29,.45,.29,-.29),
    ITEM("stone_tea","石桌石鼓凳",.54,.65,1.37,-.32),
    ITEM("tea_shelter","竹编茶棚",1.42,2.5,1.7,-.95),
    ITEM("pot","陶罐盆栽",.42,.92,.42,-.42)
};
#undef ITEM
const QlcgBuildingModel *qlcg_lot_model(int id) {
    if(id>=LOT_TEA && id<LOT_MODEL_END) return catalog+id-LOT_TEA;
    return id>=LOT_EXT_BASE?qlcg_extra_model(id):qlcg_building_model_at(id);
}
bool qlcg_lot_is_seat(int id) {return id==LOT_TEA || (id>=LOT_WOOD_CHAIR && id<=LOT_TEA_SHELTER);}
bool qlcg_lot_is_wall(int id) {return id==LOT_FENCE || id==LOT_STONE_WALL || id==LOT_TIMBER || id==LOT_PLASTER;}
static void box(LpBuilder *b,double x,double y,double z,double a,double h,double d,LpRgb color) {
    qlcg_lp_box(b,(double[]){x,y,z},(double[]){a,h,d},color,NULL);
}
void qlcg_lot_models_init(void) {
    if(catalog[0].geometry) return;
    LpRgb wood={129,83,49},bamboo={138,137,80},stone={124,130,122},light={255,199,96};
    for(int id=LOT_TEA;id<LOT_MODEL_END;id++) {
        LpBuilder b;qlcg_lp_init(&b);
        switch(id) {
        case LOT_TIMBER:
            for(int i=-2;i<=2;i++) qlcg_lp_prism(&b,(double[]){i*.45,0,0},.09,.11,0,1.1+.06*(i&1),6,wood,NULL);
            for(int i=0;i<2;i++) box(&b,0,.29+i*.47,.064,1,.064,.055,wood);
            break;
        case LOT_PLASTER:
            box(&b,0,.77,0,1,.77,.17,(LpRgb){213,207,177});
            box(&b,0,.13,0,1,.13,.20,stone);
            box(&b,0,1.59,0,1,.085,.26,(LpRgb){69,83,82});
            for(int i=-1;i<=1;i+=2) box(&b,i*.91,.87,0,.082,.87,.21,stone);
            break;
        case LOT_GATE: case LOT_WIDE_GATE: {
            float rx=id==LOT_GATE?1:2,h=id==LOT_GATE?1.8f:2.15f;
            for(int i=-1;i<=1;i+=2) box(&b,i*(rx-.13),h*.5,0,.12,h*.5,.14,wood);
            box(&b,0,h-.08,0,rx,.08,.19,bamboo);
            break;
        }
        case LOT_STUMP: case LOT_STONE_TEA:
            qlcg_lp_prism(&b,(double[]){0,0,0},.28,.28,0,.45,10,id==LOT_STUMP?wood:stone,NULL);
            if(id==LOT_STONE_TEA) {
                qlcg_lp_prism(&b,(double[]){0,0,.83},.19,.19,0,.52,8,stone,NULL);
                qlcg_lp_prism(&b,(double[]){0,0,.83},.54,.54,.5,.62,12,stone,NULL);
                for(int i=-1;i<=1;i+=2) qlcg_lp_prism(&b,(double[]){i*.16,0,.83},.05,.05,.62,.68,8,light,NULL);
            }
            break;
        case LOT_POT:
            qlcg_lp_prism(&b,(double[]){0,0,0},.29,.29,.04,.44,10,(LpRgb){161,90,61},NULL);
            qlcg_lp_prism(&b,(double[]){0,0,0},.33,.33,.37,.46,10,(LpRgb){184,106,68},NULL);
            for(int i=-1;i<=1;i++) qlcg_lp_cone(&b,(double[]){i*.17,.42,i*.10},.22,.22,.42+.08*(i==0),7,(LpRgb){83,127,74},NULL);
            break;
        case LOT_WOOD_CHAIR: case LOT_TEA_SHELTER: case LOT_TEA:
            if(id==LOT_TEA_SHELTER) {
                for(int i=-1;i<=1;i+=2) for(int j=-1;j<=1;j+=2) box(&b,i*1.20,1.0,.40+j*1.07,.061,1,.061,bamboo);
                qlcg_lp_cone(&b,(double[]){0,1.98,.40},1.42,1.30,.50,4,bamboo,NULL);
            }
            for(int i=-1;i<=1;i+=2) for(int j=-1;j<=1;j+=2) {
                box(&b,i*.19,.20,j*.18,.025,.20,.025,wood);
                if(id!=LOT_WOOD_CHAIR) box(&b,i*.235,.22,QLCG_LIFE_TEA_TABLE_M+j*.15,.025,.22,.025,wood);
            }
            box(&b,0,QLCG_DOLL_CHAIR_HEIGHT,0,.23,.025,.22,bamboo);
            box(&b,0,.62,-.18,.21,.20,.025,wood);
            if(id==LOT_WOOD_CHAIR) break;
            box(&b,0,.44,QLCG_LIFE_TEA_TABLE_M,.27,.025,.19,wood);
            for(int i=-1;i<=1;i+=2) qlcg_lp_prism(&b,(double[]){i*.14,0,QLCG_LIFE_TEA_TABLE_M},.045,.045,.465,.53,8,stone,NULL);
            break;
        case LOT_FENCE:
            for(int i=-2;i<=2;i++) box(&b,i*.47,.52,0,.045,.52,.05,bamboo);
            for(int i=1;i<=2;i++) box(&b,0,i*.36,.04,1,.055,.05,wood);
            break;
        case LOT_STONE_WALL:
            for(int i=0;i<3;i++) for(int j=0;j<4;j++)
                box(&b,-.75+j*.5,.15+i*.30,0,.247,.146,.20,stone);
            break;
        case LOT_STEPS:
            for(int i=0;i<3;i++) box(&b,0,.10*(i+1),.667-i*.667,1,.10*(i+1),.3335,stone);
            break;
        case LOT_DECK:
            for(int i=-1;i<=1;i+=2) for(int j=-1;j<=1;j+=2) box(&b,i*.82,.27,j*.82,.085,.27,.085,wood);
            for(int i=0;i<8;i++) box(&b,-.875+i*.25,.55,0,.122,.05,1,bamboo);
            break;
        case LOT_LAMP:
            box(&b,0,.10,0,.28,.10,.28,stone);box(&b,0,.61,0,.12,.42,.12,stone);
            box(&b,0,1.22,0,.20,.20,.20,light);box(&b,0,1.48,0,.32,.07,.32,stone);
            break;
        }
        int i=id-LOT_TEA;
        qlcg_lp_build(&b,geometry+i);qlcg_lp_free(&b);catalog[i].geometry=geometry+i;
    }
}
void qlcg_lot_models_free(void) {
    qlcg_extra_free();
    for(int i=0;i<LOT_MODEL_END-LOT_TEA;i++) {qlcg_lp_geometry_free(geometry+i);catalog[i].geometry=NULL;}
}
