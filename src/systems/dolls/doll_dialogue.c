#include "doll_dialogue.h"
#include "doll_sim.h"

static bool matches_state(QlcgDollTrigger trigger,int state,bool field_contact) {
    switch(trigger) {
        case DOLL_TALK_NEAR: return true;
        case DOLL_TALK_WALK: return state==DOLL_STATE_WALK;
        case DOLL_TALK_WORK: return state==DOLL_STATE_WORK;
        case DOLL_TALK_CAMPFIRE: return state==DOLL_STATE_CAMPFIRE;
        case DOLL_TALK_WATCH: return state==DOLL_STATE_WATCH;
        case DOLL_TALK_APPROACH: return state==DOLL_STATE_APPROACH;
        case DOLL_TALK_RETURN: return state==DOLL_STATE_RETURN;
        case DOLL_TALK_FIELD: return state==DOLL_STATE_PROTECT&&field_contact;
        case DOLL_TALK_PATROL: return state==DOLL_STATE_PATROL;
    }
    return false;
}

bool qlcg_doll_dialogue_step(QlcgDollConversation *c,const QlcgDollType *type,QlcgDollDayKind day,
                             int state,bool field_contact,float distance_sq,float elapsed,QlcgDollSpeech *speech) {
    const QlcgDollDialogues *set=qlcg_doll_dialogues(type);
    unsigned matches=0;
    int first=-1;
    for(int i=0;i<set->count;i++) {
        const QlcgDollDialogueRule *rule=set->rules+i;
        unsigned bit=1u<<i;
        float radius=rule->radius+((c->matches&bit)?1.0f:0.0f);
        if(distance_sq>radius*radius||(rule->day!=DOLL_DAY_COUNT&&rule->day!=(int)day)||
           !matches_state(rule->trigger,state,field_contact)) continue;
        matches|=bit;
        if(first<0) first=i;
    }
    unsigned entered=matches&~c->matches;
    c->matches=matches;
    if(first<0) { c->next_line=0;return false; }
    if(entered&(1u<<first)) {
        c->rule=first;c->next_line=0;c->remaining=0;
    } else {
        if(c->rule!=first) c->next_line=0;
        if(!c->next_line) return false;
        c->remaining-=elapsed;
        if(c->remaining>0) return false;
    }
    const QlcgDollDialogueRule *rule=set->rules+first;
    speech->speaker=rule->speaker[0]?rule->speaker:type->name;
    speech->text=rule->lines[c->next_line++];
    if(c->next_line==rule->line_count) c->next_line=0;
    c->remaining=rule->interval;
    return true;
}
