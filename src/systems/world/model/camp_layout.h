#ifndef QLCG_CAMP_LAYOUT_H
#define QLCG_CAMP_LAYOUT_H
#include <stdint.h>

enum { QLCG_PART_FLAT=1, QLCG_PART_SLOPE=2 };
enum { QLCG_CAMP_SLOTS=4, QLCG_SETTLEMENT_PARTS=7 };
typedef struct {
    int model, slot, parent;
    float x,y,z,yaw,scale;
    unsigned flags;
} QlcgSettlementPart;
typedef struct {
    int units[QLCG_CAMP_SLOTS], mask, count;
    QlcgSettlementPart parts[QLCG_SETTLEMENT_PARTS];
} QlcgCampLayout;

int qlcg_camp_pack_count(void);
const char *qlcg_camp_pack_id(int index);
int qlcg_camp_generate(int pack,uint32_t seed,QlcgCampLayout *out);
#endif
