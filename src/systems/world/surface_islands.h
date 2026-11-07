#ifndef QLCG_SURFACE_ISLANDS_H
#define QLCG_SURFACE_ISLANDS_H
#include "planet_macro.h"

/* 岛形摘要：调试跳岛与检视用 */
typedef struct {
    int   cell, variant;
    float wx, wz;
    float peak;
    float span;
} IslandSummary;

/* 岛岸模板高度：命中返回 true 并写出替换高度 */
bool qlcg_island_coast(const MacroWorld *w, double wx, double wz,
                       double natural, double *out_height);
/* 该点是否落在某岛的模板作用域内（舆图取色用） */
bool qlcg_island_at(const MacroWorld *w, double wx, double wz);
/* 已筛选岛数；out 可为空，至多 capacity 条 */
int  qlcg_island_list(const MacroWorld *w, IslandSummary *out, int capacity);
void qlcg_islands_free(MacroWorld *w);

#endif
