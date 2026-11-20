#ifndef QLCG_DOLL_AI_H
#define QLCG_DOLL_AI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { QLCG_DOLL_TYPES_MAX=64, QLCG_DOLL_LOGIC_MAX=32, QLCG_DOLL_SCHEDULES_MAX=32, QLCG_DOLL_ID_MAX=64 };
enum { DOLL_AI_WALK,DOLL_AI_WORK,DOLL_AI_CAMPFIRE,DOLL_AI_SLEEP,DOLL_AI_WATCH };
typedef enum { DOLL_DAY_NORMAL,DOLL_DAY_REST,DOLL_DAY_FESTIVAL,DOLL_DAY_COUNT } QlcgDollDayKind;
typedef struct {
    char id[QLCG_DOLL_ID_MAX],name[96],behavior[QLCG_DOLL_ID_MAX],schedule[QLCG_DOLL_ID_MAX];
    int role,priority,logic_index,schedule_index,dialogue_index;
    float scale,home[2],work[2],via[2],facing[2],meeting[2];
} QlcgDollType;
typedef struct {
    uint32_t resident_id;
    double local_hour;
    bool work_available;
    float home[2],work[2],via[2],facing[2];
    QlcgDollDayKind day_kind;
    float meeting[2],campfire[2];
} QlcgDollAIContext;
typedef struct {
    float x,z,yaw;
    int state;
    bool moving;
} QlcgDollAIResult;
typedef bool (*QlcgDollLogicFn)(const QlcgDollAIContext *,QlcgDollAIResult *);

void qlcg_doll_types_reset(void);
void qlcg_doll_types_init(void);
bool qlcg_doll_types_load(const char *path,char *error,size_t cap);
bool qlcg_doll_types_validate(const char *path,char *error,size_t cap);
int qlcg_doll_types_load_dir(const char *path);
int qlcg_doll_type_count(void);
const QlcgDollType *qlcg_doll_type(const char *id);
const QlcgDollType *qlcg_doll_type_for(int role,uint32_t resident_id);
bool qlcg_doll_logic_register(const char *id,QlcgDollLogicFn sample);
bool qlcg_doll_ai_sample(const QlcgDollType *type,const QlcgDollAIContext *context,QlcgDollAIResult *out);
#endif
