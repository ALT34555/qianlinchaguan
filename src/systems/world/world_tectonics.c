/* 入口：实现见 gen/gen_tectonics.c */
#include "planet_macro.h"
#include "world_internal.h"
#include "gen/gen_tectonics.h"

void build_tectonics(MacroWorld *w) {
    qlcg_gen_tectonics(w);
}
