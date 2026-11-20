#include "doll_model.h"
#include "core/math_util.h"
#include <math.h>

#include "doll_model_data.inc"

static void interpolate(const float *a, const float *b, float t, float q[4]) {
    float dot = 0, sign = 1, x = 1 - t, y = t;
    for (int i = 0; i < 4; i++) dot += a[i] * b[i];
    if (dot < 0) { dot = -dot; sign = -1; }
    if (dot < 0.9995f) {
        float theta = acosf(fminf(dot, 1)), den = sinf(theta);
        x = sinf((1-t)*theta) / den;
        y = sinf(t*theta) / den;
    }
    float len = 0;
    for (int i = 0; i < 4; i++) { q[i] = a[i]*x + b[i]*y*sign; len += q[i]*q[i]; }
    len = sqrtf(len);
    for (int i = 0; i < 4; i++) q[i] /= len;
}

static void compose(const float p[3], const float q[4], float m[16]) {
    float x=q[0], y=q[1], z=q[2], w=q[3];
    qlcg_mat4_identity(m);
    m[0]=1-2*(y*y+z*z); m[1]=2*(x*y+z*w); m[2]=2*(x*z-y*w);
    m[4]=2*(x*y-z*w); m[5]=1-2*(x*x+z*z); m[6]=2*(y*z+x*w);
    m[8]=2*(x*z+y*w); m[9]=2*(y*z-x*w); m[10]=1-2*(x*x+y*y);
    m[12]=p[0]; m[13]=p[1]; m[14]=p[2];
}

const QlcgDollModel *qlcg_doll_model(void) { return &doll_model; }

float qlcg_doll_action_duration(QlcgDollAction action) {
    return action >= 0 && action < QLCG_DOLL_ACTION_COUNT ? doll_durations[action] : doll_durations[0];
}

const float (*qlcg_doll_action_bounds(QlcgDollAction action))[3] {
    if (action < 0 || action >= QLCG_DOLL_ACTION_COUNT) action = QLCG_DOLL_IDLE;
    return doll_action_bounds[action];
}

static float pose_frame(float phase) {
    if (!isfinite(phase)) phase = 0;
    phase -= floorf(phase);
    if (phase >= 1) phase = 0;
    return phase * QLCG_DOLL_FRAME_COUNT;
}

static void sample_bone(QlcgDollAction action, float frame, int bone, float p[3], float q[4]) {
    int a = (int)frame, b = (a + 1) % QLCG_DOLL_FRAME_COUNT;
    float t = frame - a;
    const float *pa = doll_frames[action][a][bone], *pb = doll_frames[action][b][bone];
    for (int k = 0; k < 3; k++) p[k] = pa[k] + (pb[k]-pa[k])*t;
    interpolate(pa+3, pb+3, t, q);
}

typedef struct { float hips,thigh,shin; } ChairPose;
enum { BONE_ROOT=0,BONE_HIPS=1,BONE_SPINE=2,BONE_LEFT_THIGH=15,BONE_RIGHT_THIGH=19 };

static ChairPose chair_pose(float seat_height) {
    float hips=seat_height+.09f,down=hips-.04f-.06f,forward=.38f;
    const float upper=.41f,lower=.39f;
    float knee=acosf(qlcg_clampf((forward*forward+down*down-upper*upper-lower*lower)/(2*upper*lower),-1,1));
    float thigh=-atan2f(forward,down)-atan2f(lower*sinf(knee),upper+lower*cosf(knee));
    return (ChairPose){hips,thigh,knee};
}

static void chair_bone(const ChairPose *chair,int bone,float p[3],float q[4]) {
    float angle=0;
    if(bone==BONE_ROOT) p[0]=p[1]=p[2]=0;
    else if(bone==BONE_HIPS) p[1]=chair->hips;
    else if(bone==BONE_SPINE) angle=.08f;
    else if(bone==BONE_LEFT_THIGH||bone==BONE_RIGHT_THIGH) angle=chair->thigh;
    else if(bone==BONE_LEFT_THIGH+1||bone==BONE_RIGHT_THIGH+1) angle=chair->shin;
    else if(bone==BONE_LEFT_THIGH+2||bone==BONE_RIGHT_THIGH+2) angle=-chair->thigh-chair->shin;
    else return;
    q[0]=sinf(angle*.5f);q[1]=q[2]=0;q[3]=cosf(angle*.5f);
}

static void pose(QlcgDollAction action, float frame, QlcgDollAction other, float other_frame,
                 float mix,const ChairPose *chair,float out[QLCG_DOLL_BONE_COUNT * 16]) {
    float world[QLCG_DOLL_BONE_COUNT][16];
    for (int i = 0; i < QLCG_DOLL_BONE_COUNT; i++) {
        float p[3], q[4], local[16];
        sample_bone(action,frame,i,p,q);
        if(chair) chair_bone(chair,i,p,q);
        if (mix>0) {
            float next_p[3],next_q[4];
            sample_bone(other,other_frame,i,next_p,next_q);
            for(int k=0;k<3;k++) p[k]+=(next_p[k]-p[k])*mix;
            interpolate(q,next_q,mix,q);
        }
        compose(p, q, local);
        if (doll_bones[i].parent < 0) {
            for (int k = 0; k < 16; k++) world[i][k] = local[k];
        } else qlcg_mat4_mul(world[doll_bones[i].parent], local, world[i]);
        qlcg_mat4_mul(world[i], doll_bones[i].inverse_bind, out+i*16);
    }
}

void qlcg_doll_pose(QlcgDollAction action, float phase, float out[QLCG_DOLL_BONE_COUNT * 16]) {
    if (!out) return;
    if (action < 0 || action >= QLCG_DOLL_ACTION_COUNT) action = QLCG_DOLL_IDLE;
    pose(action,pose_frame(phase),action,0,0,NULL,out);
}

void qlcg_doll_chair_pose(float phase,float seat_height,float out[QLCG_DOLL_BONE_COUNT * 16]) {
    if(!out) return;
    ChairPose chair=chair_pose(seat_height);
    pose(QLCG_DOLL_REST,pose_frame(phase),QLCG_DOLL_REST,0,0,&chair,out);
}

const float (*qlcg_doll_chair_bounds(void))[3] {
    static const float bounds[2][3]={{-.34f,-.03f,-.23f},{.34f,1.7f,.70f}};
    return bounds;
}

float qlcg_doll_gait_cycle(float gait) {
    if (gait<=1) return doll_gait_cycles[0]*fmaxf(.2f,gait);
    return doll_gait_cycles[0]+(doll_gait_cycles[1]-doll_gait_cycles[0])*fminf(1,gait-1);
}

void qlcg_doll_gait_pose(float phase, float idle_phase, float gait, float out[QLCG_DOLL_BONE_COUNT * 16]) {
    if (!out) return;
    if (gait<=0) qlcg_doll_pose(QLCG_DOLL_IDLE,idle_phase,out);
    else if (gait<1) pose(QLCG_DOLL_IDLE,pose_frame(idle_phase),QLCG_DOLL_WALK,pose_frame(phase),gait,NULL,out);
    else if (gait<2) pose(QLCG_DOLL_WALK,pose_frame(phase),QLCG_DOLL_RUN,pose_frame(phase),gait-1,NULL,out);
    else qlcg_doll_pose(QLCG_DOLL_RUN,phase,out);
}
