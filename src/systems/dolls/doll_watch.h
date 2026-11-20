#ifndef QLCG_DOLL_WATCH_H
#define QLCG_DOLL_WATCH_H

#include "doll_sim.h"

typedef struct {
    float x,z,yaw,speed;
    int mode,waypoint;
    bool active;
} QlcgDollWatch;

enum { QLCG_WATCH_OBSTACLES=4 };
typedef struct { float x,z,half_x,half_z; } QlcgWatchObstacle;
typedef struct {
    QlcgWatchObstacle obstacles[QLCG_WATCH_OBSTACLES];
    int obstacle_count;
    float player_x,player_z,field_x,field_z;
    bool inside,nearby,roaming,field;
} QlcgDollWatchContext;

void qlcg_doll_watch_step(QlcgDollWatch *watch,const QlcgDollWatchContext *context,
                          float dt,QlcgDollSample *sample);
bool qlcg_doll_detour(const QlcgDollWatchContext *context,float x,float z,float goal_x,float goal_z,
                      float *next_x,float *next_z);
#endif
