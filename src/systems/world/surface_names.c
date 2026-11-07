/* 地表命名：自然事件与工程工具 */
#include "planet_macro.h"
#include "surface_internal.h"
#include "../../core/math_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

const char *qlcg_nat_event_name(NaturalEventType t) {
    switch (t) {
        case NAT_EVENT_TYPHOON:        return "台风过境";
        case NAT_EVENT_EARTHQUAKE:     return "地壳强震";
        case NAT_EVENT_DESERTIFY:      return "土地沙漠化";
        case NAT_EVENT_FLOOD:          return "山洪暴发";
        case NAT_EVENT_COLD_WAVE:      return "极地寒潮";
        case NAT_EVENT_WILDFIRE:       return "林莽山火";
        default:                       return "自然事件";
    }
}

/* 自然工程工具名称 */
const char *qlcg_eng_tool_name(EngToolType t) {
    switch (t) {
        case ENG_TOOL_NONE:         return "检视 / 漫游";
        case ENG_TOOL_DEMOLISH:     return "地形破坏";
        case ENG_TOOL_AFFOREST:     return "植树造林";
        case ENG_TOOL_RIVER_DIVERT: return "河流改道";
        case ENG_TOOL_RECLAIM_SEA:  return "填海造陆";
        case ENG_TOOL_RAISE_MOUNT:  return "造山筑峰";
        case ENG_TOOL_DIG_LAKE:     return "开挖湖泊";
        case ENG_TOOL_FLATTEN:      return "土地平整";
        default:                    return "检视";
    }
}

/* 自然工程工具说明 */
const char *qlcg_eng_tool_desc(EngToolType t) {
    switch (t) {
        case ENG_TOOL_NONE:         return "漫游检视，右键空降或点选";
        case ENG_TOOL_DEMOLISH:     return "轰击爆破地表并震碎岩林";
        case ENG_TOOL_AFFOREST:     return "播撒林木锁水固沙退漠化";
        case ENG_TOOL_RIVER_DIVERT: return "拖拽开凿河床引水改道";
        case ENG_TOOL_RECLAIM_SEA:  return "倾倒土石抬升浅海成平陆";
        case ENG_TOOL_RAISE_MOUNT:  return "拔地隆起层峦叠嶂与山脊";
        case ENG_TOOL_DIG_LAKE:     return "深挖盆地汇水成澄澈湖泊";
        case ENG_TOOL_FLATTEN:      return "削峰填谷平整地基与台地";
        default:                    return "";
    }
}

/* 点到线段距离与投影系数 */
