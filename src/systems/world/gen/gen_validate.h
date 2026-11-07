/* 世界一致性校验（对齐 validateWorld） */
#ifndef QLCG_GEN_VALIDATE_H
#define QLCG_GEN_VALIDATE_H

#include "../planet_macro.h"

typedef struct {
    int   nan_count;      /* 非有限值 */
    int   bad_rank;       /* 下游拓扑序错误 */
    int   uphill;         /* spill 逆升 */
    int   bad_plate;      /* 板块 id 越界 */
    int   bad_lake;       /* 湖面低于地面 */
    int   bad_balance;
    float land_ratio;     /* 实际陆地面积占比 */
    float flow_residual;  /* 出口流量 / 产流总量 */
    double conservation;
    double area_error;
} WorldAudit;

void qlcg_validate_world(const MacroWorld *w, WorldAudit *out);

/* 返回 1 表示通过 */
int  qlcg_audit_ok(const WorldAudit *a);

#endif
