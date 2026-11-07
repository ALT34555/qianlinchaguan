#ifndef QLCG_DECORATION_COLLISION_H
#define QLCG_DECORATION_COLLISION_H

#include <stdbool.h>
#include <stdint.h>

enum { COLLISION_NONE = -1, HARD_TRUNK, SOFT_SHRUB, HARD_ROCK, PLANT_TRIGGER };
typedef struct {
    float x, z, bottom, top, rx, rz;
    int kind;
} DecorationShape;

typedef struct {
    float max_slope_degrees, shrub_speed_factor;
    float fade_seconds, pulse_seconds, pulse_depth;
    float feedback_height, feedback_opacity;
    uint32_t feedback_rgb;
} MovementRules;

typedef struct {
    float from, to, elapsed, phase;
} BlockFeedback;

struct RegionData;
const MovementRules *qlcg_movement_rules(void);
float qlcg_block_feedback(BlockFeedback *fade, bool blocked, float dt);
void qlcg_collision_bounds(DecorationShape *out, const float *positions, int count);
bool qlcg_collision_stable(const char *id, int season, DecorationShape *out);
float qlcg_plant_instance_scale(float height, double model_height);
float qlcg_rock_instance_scale(float width, double model_radius);
void qlcg_region_build_colliders(struct RegionData *reg, int season, int form);
bool qlcg_region_blocked(const struct RegionData *reg, float x, float y, float z, float height, float half);
float qlcg_region_landing(const struct RegionData *reg, float x, float z, float from, float to, float half);
float qlcg_region_step_surface(const struct RegionData *reg, float x, float z, float feet, float step, float half);
float qlcg_region_brush_speed(const struct RegionData *reg, float x, float y, float z, float height, float half);
int qlcg_region_plant_contacts(const struct RegionData *reg, float x, float y, float z, float height, float half);

#endif
