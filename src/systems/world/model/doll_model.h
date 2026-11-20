#ifndef QLCG_DOLL_MODEL_H
#define QLCG_DOLL_MODEL_H

#include <stdint.h>

enum { QLCG_DOLL_BONE_COUNT = 23, QLCG_DOLL_FRAME_COUNT = 24 };
#define QLCG_DOLL_CHAIR_HEIGHT .43f
typedef enum {
    QLCG_DOLL_IDLE, QLCG_DOLL_WALK, QLCG_DOLL_WORK, QLCG_DOLL_REST, QLCG_DOLL_SLEEP, QLCG_DOLL_RUN,
    QLCG_DOLL_ACTION_COUNT
} QlcgDollAction;

typedef struct {
    float position[3];
    float normal[3];
    float color[3];
    float skin_weights[4];
    float cloth_shade;
    uint8_t skin_indices[4];
    uint8_t color_kind;
} QlcgDollVertex;

typedef struct {
    const char *name;
    int parent;
    float position[3];
    float inverse_bind[16];
} QlcgDollBone;

typedef struct {
    const QlcgDollVertex *vertices;
    const uint32_t *indices;
    int vertex_count;
    int index_count;
    const QlcgDollBone *bones;
    int bone_count;
    float height;
    float bounds_min[3];
    float bounds_max[3];
} QlcgDollModel;

const QlcgDollModel *qlcg_doll_model(void);
void qlcg_doll_pose(QlcgDollAction action, float phase, float out[QLCG_DOLL_BONE_COUNT * 16]);
void qlcg_doll_chair_pose(float phase,float seat_height,float out[QLCG_DOLL_BONE_COUNT * 16]);
const float (*qlcg_doll_chair_bounds(void))[3];
void qlcg_doll_gait_pose(float phase, float idle_phase, float gait, float out[QLCG_DOLL_BONE_COUNT * 16]);
float qlcg_doll_gait_cycle(float gait);
float qlcg_doll_action_duration(QlcgDollAction action);
const float (*qlcg_doll_action_bounds(QlcgDollAction action))[3];

#endif
