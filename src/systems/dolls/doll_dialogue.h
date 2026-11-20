#ifndef QLCG_DOLL_DIALOGUE_H
#define QLCG_DOLL_DIALOGUE_H
#include "doll_ai.h"

enum { DOLL_DIALOGUE_RULES=8, DOLL_DIALOGUE_LINES=4, DOLL_DIALOGUE_TEXT=256 };
typedef enum {
    DOLL_TALK_NEAR, DOLL_TALK_WALK, DOLL_TALK_WORK, DOLL_TALK_CAMPFIRE,
    DOLL_TALK_WATCH, DOLL_TALK_APPROACH, DOLL_TALK_RETURN, DOLL_TALK_FIELD, DOLL_TALK_PATROL
} QlcgDollTrigger;
typedef struct {
    QlcgDollTrigger trigger;
    int day,line_count;
    float radius,interval;
    char speaker[96],lines[DOLL_DIALOGUE_LINES][DOLL_DIALOGUE_TEXT];
} QlcgDollDialogueRule;
typedef struct {
    int count;
    QlcgDollDialogueRule rules[DOLL_DIALOGUE_RULES];
} QlcgDollDialogues;
typedef struct {
    unsigned matches;
    int rule,next_line;
    float remaining;
} QlcgDollConversation;
typedef struct { const char *speaker,*text; } QlcgDollSpeech;

const QlcgDollDialogues *qlcg_doll_dialogues(const QlcgDollType *type);
bool qlcg_doll_dialogue_step(QlcgDollConversation *conversation,const QlcgDollType *type,
    QlcgDollDayKind day,int state,bool field_contact,float distance_sq,float elapsed,QlcgDollSpeech *speech);
#endif
