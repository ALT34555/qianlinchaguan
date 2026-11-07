/* 开发工具的编译期边界 */
#ifndef QLCG_DEV_TOOLS_H
#define QLCG_DEV_TOOLS_H
#include <stdbool.h>

enum {
    QLCG_DEV_SKIP_SKY = 1u,
    QLCG_DEV_SKIP_TERRAIN = 2u,
    QLCG_DEV_SKIP_PROP = 4u,
    QLCG_DEV_SKIP_WATER = 8u,
    QLCG_DEV_SKIP_ICE = 16u
};

#ifdef QLCG_DEV_TOOLS
#define QLCG_DEV_ONLY(...) __VA_ARGS__
#include "../../.fordev_tools/runtime/dev_hooks.h"
#define QLCG_DEV_INPUT_LOG(...) qlcg_dev_input_log(__VA_ARGS__)
#define QLCG_DEV_TRACE(...)     qlcg_dev_trace(__VA_ARGS__)
#else
#define QLCG_DEV_ONLY(...)
#define QLCG_DEV_INPUT_LOG(...) ((void)0)
#define QLCG_DEV_TRACE(...)     ((void)0)
#define qlcg_dev_world_clock(...) false
#define qlcg_dev_forced_season() (-1)
#define qlcg_dev_weather(...) ((void)0)
#define qlcg_dev_hour(hour) (hour)
#define qlcg_dev_atlas_rain() false
#define qlcg_dev_prop_radius(radius) (radius)
#define qlcg_dev_sprite_dir(dir) (dir)
#define qlcg_dev_far_all() false
#define qlcg_dev_skip_draw(...) false
#define qlcg_dev_ice_pass(...) ((void)0)
#define qlcg_dev_ice_walk(...) ((void)0)
#define qlcg_dev_ice_sample(...) ((void)0)
#define qlcg_dev_select_open(ui, id, open) (open)
#define qlcg_dev_settings_fixture(...) ((void)0)
#define qlcg_dev_render_sprites(...) ((void)0)
#define qlcg_dev_autosave_every(fallback) (fallback)
#endif
#endif
