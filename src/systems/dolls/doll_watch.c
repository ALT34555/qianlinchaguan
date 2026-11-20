#include "doll_watch.h"
#include "core/math_util.h"
#include <math.h>

typedef struct { float x,z; } WatchPoint;

static bool clip_axis(float start,float delta,float center,float half,float *enter,float *leave) {
    if(fabsf(delta)<.00001f)return fabsf(start-center)<half;
    float a=(center-half-start)/delta,b=(center+half-start)/delta;
    *enter=fmaxf(*enter,fminf(a,b));*leave=fminf(*leave,fmaxf(a,b));
    return *enter<*leave;
}

static bool clear_path(const QlcgDollWatchContext *c,WatchPoint a,WatchPoint b) {
    for(int i=0;i<c->obstacle_count;i++) {
        QlcgWatchObstacle r=c->obstacles[i];
        float enter=0,leave=1;
        if(clip_axis(a.x,b.x-a.x,r.x,r.half_x,&enter,&leave) &&
           clip_axis(a.z,b.z-a.z,r.z,r.half_z,&enter,&leave))return false;
    }
    return true;
}

static WatchPoint outside_target(const QlcgDollWatchContext *c,WatchPoint goal) {
    if(clear_path(c,goal,goal))return goal;
    float best=INFINITY;
    WatchPoint result=goal;
    for(int i=0;i<c->obstacle_count;i++) {
        QlcgWatchObstacle r=c->obstacles[i];
        float rx=r.half_x+.2f,rz=r.half_z+.2f;
        WatchPoint sides[]={{r.x-rx,goal.z},{r.x+rx,goal.z},{goal.x,r.z-rz},{goal.x,r.z+rz}};
        for(int k=0;k<4;k++) {
            float d=hypotf(sides[k].x-goal.x,sides[k].z-goal.z);
            if(d<best && clear_path(c,sides[k],sides[k])) {result=sides[k];best=d;}
        }
    }
    return result;
}

static WatchPoint corner_detour(const QlcgDollWatchContext *c,WatchPoint start,WatchPoint goal) {
    enum { CORNER_POINTS=2+QLCG_WATCH_OBSTACLES*4 };
    WatchPoint points[CORNER_POINTS]={start,goal};
    int count=2;
    for (int i=0;i<c->obstacle_count;i++) {
        QlcgWatchObstacle r=c->obstacles[i];
        for (int corner=0;corner<4;corner++) {
            WatchPoint p={r.x+(corner&1?1.f:-1.f)*(r.half_x+.2f),
                r.z+(corner&2?1.f:-1.f)*(r.half_z+.2f)};
            if (clear_path(c,p,p)) points[count++]=p;
        }
    }
    float distance[CORNER_POINTS]={0};
    int previous[CORNER_POINTS]={0};
    bool visited[CORNER_POINTS]={false};
    for (int i=1;i<count;i++) {distance[i]=INFINITY;previous[i]=-1;}
    for (int step=0;step<count;step++) {
        int best=-1;
        for (int i=0;i<count;i++) if (!visited[i] && isfinite(distance[i]) &&
            (best<0 || distance[i]<distance[best])) best=i;
        if (best<0) return start;
        if (best==1) {
            while (previous[best]>0) best=previous[best];
            return points[best];
        }
        visited[best]=true;
        for (int i=1;i<count;i++) if (!visited[i] && clear_path(c,points[best],points[i])) {
            float next=distance[best]+hypotf(points[i].x-points[best].x,points[i].z-points[best].z);
            if (next<distance[i]) {distance[i]=next;previous[i]=best;}
        }
    }
    return start;
}

static WatchPoint next_waypoint(const QlcgDollWatchContext *c,WatchPoint start,WatchPoint goal,bool *direct) {
    goal=outside_target(c,goal);
    if(clear_path(c,start,goal)) {
        if(direct)*direct=true;
        return goal;
    }
    static const WatchPoint rim[]={{-8.7f,-8.7f},{8.7f,-8.7f},{8.7f,8.7f},{-8.7f,8.7f}};
    float approach[4],exit_distance[4];
    for(int i=0;i<4;i++) {
        approach[i]=clear_path(c,start,rim[i])?hypotf(start.x-rim[i].x,start.z-rim[i].z):INFINITY;
        exit_distance[i]=clear_path(c,rim[i],goal)?hypotf(rim[i].x-goal.x,rim[i].z-goal.z):INFINITY;
    }
    float best=INFINITY;
    WatchPoint next=start;
    for(int entry=0;entry<4;entry++)for(int exit=0;exit<4;exit++) {
        int clockwise=(exit-entry+4)%4,steps=clockwise>2?4-clockwise:clockwise;
        float distance=approach[entry]+17.4f*steps+exit_distance[exit];
        if(distance>=best)continue;
        best=distance;next=rim[entry];
        if(approach[entry]<.12f) {
            next=steps?rim[(entry+(clockwise>2?3:1))%4]:goal;
            if(direct)*direct=steps==0;
        } else if(direct) *direct=false;
    }
    return direct && !isfinite(best)?corner_detour(c,start,goal):next;
}

bool qlcg_doll_detour(const QlcgDollWatchContext *c,float x,float z,float goal_x,float goal_z,
                      float *next_x,float *next_z) {
    bool direct=false;
    WatchPoint start={x,z},open=outside_target(c,start);
    WatchPoint next=open.x==x && open.z==z?next_waypoint(c,start,(WatchPoint){goal_x,goal_z},&direct):open;
    *next_x=next.x;*next_z=next.z;
    return direct;
}

void qlcg_doll_watch_step(QlcgDollWatch *watch,const QlcgDollWatchContext *c,float dt,QlcgDollSample *sample) {
    if(!watch->active) {
        *watch=(QlcgDollWatch){.x=sample->x,.z=sample->z,.yaw=sample->yaw,.mode=DOLL_STATE_WATCH,.active=true};
    }
    bool home=hypotf(watch->x-sample->x,watch->z-sample->z)<.12f;
    int previous=watch->mode;
    int mode=home?DOLL_STATE_WATCH:DOLL_STATE_RETURN;
    float x=sample->x,z=sample->z,max_speed=1.4f;
    if(c->inside&&c->field) {
        mode=DOLL_STATE_PROTECT;x=c->field_x;z=c->field_z;max_speed=3.6f;
    } else if(!c->inside&&c->nearby) {
        mode=DOLL_STATE_APPROACH;max_speed=3.85f;
        float dx=c->player_x-sample->x,dz=c->player_z-sample->z;
        float distance=hypotf(dx,dz);
        float fraction=distance>3.4f?1-3.4f/distance:0;
        x+=dx*fraction;z+=dz*fraction;
    } else if(c->inside&&c->roaming&&
              (home||(previous!=DOLL_STATE_APPROACH&&previous!=DOLL_STATE_RETURN&&previous!=DOLL_STATE_PROTECT))) {
        static const float patrol[][2]={{0,0},{0,5},{0,0},{5,0},{0,0},{0,-5},{0,0},{-5,0}};
        if(previous!=DOLL_STATE_PATROL) watch->waypoint=0;
        x=sample->x+patrol[watch->waypoint][0];z=sample->z+patrol[watch->waypoint][1];
        if(hypotf(x-watch->x,z-watch->z)<.13f) {
            watch->waypoint=(watch->waypoint+1)%8;
            x=sample->x+patrol[watch->waypoint][0];z=sample->z+patrol[watch->waypoint][1];
        }
        mode=DOLL_STATE_PATROL;max_speed=.85f;
    }
    WatchPoint next=next_waypoint(c,(WatchPoint){watch->x,watch->z},(WatchPoint){x,z},NULL);
    x=next.x;z=next.z;
    float dx=x-watch->x,dz=z-watch->z,distance=hypotf(dx,dz);
    float desired_yaw=sample->yaw;
    if(distance>.07f) desired_yaw=atan2f(dx,dz);
    else if(mode==DOLL_STATE_APPROACH||mode==DOLL_STATE_PROTECT)
        desired_yaw=atan2f(c->player_x-watch->x,c->player_z-watch->z);
    float turn=remainderf(desired_yaw-watch->yaw,2*QLCG_PI);
    watch->yaw+=qlcg_clampf(turn,-8*dt,8*dt);
    float desired_speed=distance>.07f?fminf(max_speed,sqrtf(distance*10)):0;
    watch->speed+=qlcg_clampf(desired_speed-watch->speed,-7*dt,5.5f*dt);
    float move=fminf(distance,watch->speed*dt)*fmaxf(0,cosf(desired_yaw-watch->yaw));
    if(distance>.07f) {
        watch->x+=dx*move/distance;
        watch->z+=dz*move/distance;
    } else move=0;
    watch->mode=mode;
    sample->x=watch->x;sample->z=watch->z;sample->yaw=watch->yaw;
    sample->state=mode;sample->moving=move>0;
}
