#ifndef QLCG_BUILDING_MODEL_H
#define QLCG_BUILDING_MODEL_H

#include "lp_builder.h"
#include <stdbool.h>

typedef struct {
    const char *name;
    double position[3];
    double quaternion[4];
    const char *plant_model;
    float plant_height;
} QlcgBuildingSocket;

typedef struct {
    int start;
    int count;
    int side;
} QlcgBuildingDraw;

typedef struct {
    float x,z,half_x,half_z,bottom,top;
    bool rounded;
} BuildingCollider;

typedef struct {
    const char *id;
    const char *name;
    const LpGeometry *geometry;
    double plot_size[3];
    double bounds_min[3];
    double bounds_max[3];
    const QlcgBuildingSocket *sockets;
    int socket_count;
    const QlcgBuildingDraw *draws;
    int draw_count;
    const char *pack;
    const char *unit_id;
    int kind;
    const BuildingCollider *colliders;
    int collider_count;
} QlcgBuildingModel;

enum { QLCG_BUILDING_ROOM, QLCG_BUILDING_YARD, QLCG_BUILDING_DOOR };

int qlcg_building_model_count(void);
const QlcgBuildingModel *qlcg_building_model_at(int index);
const QlcgBuildingModel *qlcg_building_model_find(const char *id);

#endif
