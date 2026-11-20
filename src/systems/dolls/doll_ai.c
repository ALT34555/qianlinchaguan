#include "doll_ai.h"
#include "doll_dialogue.h"
#include "../world/model/model_json.h"
#include "platform/platform.h"
#include "core/binary.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { AI_WINDOWS=8,AI_PHASES=8,AI_NATIVE=16 };
enum { POINT_HOME,POINT_SQUARE,POINT_VIA,POINT_WORK,POINT_MEETING,POINT_FIXED };
typedef struct {int kind;float x,z;} AiPoint;
typedef struct {AiPoint from,to;float duration;int state;} AiPhase;
typedef struct {int count;AiPhase phases[AI_PHASES];} AiProgram;
typedef struct {
    double start,end,cycle;
    int condition;
    AiProgram program;
    char action[QLCG_DOLL_ID_MAX];
    int action_index;
} AiWindow;
typedef struct {int count;AiWindow windows[AI_WINDOWS];} AiDay;
typedef struct {char id[QLCG_DOLL_ID_MAX];AiDay day;} AiBehavior;
typedef struct {char id[QLCG_DOLL_ID_MAX];AiProgram program;} AiAction;
typedef struct {
    char id[QLCG_DOLL_ID_MAX];
    AiDay days[DOLL_DAY_COUNT];
} AiSchedule;
typedef struct {
    int types,behaviors,actions,schedules;
    int role_count[3],role_types[3][QLCG_DOLL_TYPES_MAX];
    QlcgDollType type[QLCG_DOLL_TYPES_MAX];
    AiBehavior behavior[QLCG_DOLL_LOGIC_MAX];
    AiAction action[QLCG_DOLL_LOGIC_MAX];
    AiSchedule schedule[QLCG_DOLL_SCHEDULES_MAX];
    QlcgDollDialogues default_dialogues,dialogues[QLCG_DOLL_TYPES_MAX];
} AiRegistry;
static AiRegistry registry;
static bool ready;
static struct {char id[64];QlcgDollLogicFn fn;} native[AI_NATIVE];
static int native_count;
static const float square[2]={.8f,.8f};

static bool identifier(const char *id) {
    if(!id||!*id||strlen(id)>=64)return false;
    for(const unsigned char *c=(const unsigned char*)id;*c;c++)
        if(!((*c>='a'&&*c<='z')||(*c>='A'&&*c<='Z')||(*c>='0'&&*c<='9')||*c=='_'||*c=='.'||*c==':'||*c=='-'))return false;
    return true;
}
static int behavior_index(const AiRegistry *r,const char *id) {
    for(int i=0;i<r->behaviors;i++)if(!strcmp(r->behavior[i].id,id))return i;
    return -1;
}
static int type_index(const AiRegistry *r,const char *id) {
    for(int i=0;i<r->types;i++)if(!strcmp(r->type[i].id,id))return i;
    return -1;
}
static int action_index(const AiRegistry *r,const char *id) {
    for(int i=0;i<r->actions;i++)if(!strcmp(r->action[i].id,id))return i;
    return -1;
}
static int schedule_index(const AiRegistry *r,const char *id) {
    for(int i=0;i<r->schedules;i++)if(!strcmp(r->schedule[i].id,id))return i;
    return -1;
}
static int native_index(const char *id) {
    for(int i=0;i<native_count;i++)if(!strcmp(native[i].id,id))return i;
    return -1;
}
static bool bind_logic(const AiRegistry *r,const char *id,int *out) {
    *out=behavior_index(r,id);
    if(*out>=0)return true;
    int external=native_index(id);
    if(external<0)return false;
    *out=-external-1;
    return true;
}
static bool bind_registry(AiRegistry *r) {
    for(int i=0;i<r->schedules;i++) {
        AiSchedule *s=r->schedule+i;
        for(int j=0;j<DOLL_DAY_COUNT;j++)for(int k=0;k<s->days[j].count;k++) {
            AiWindow *w=s->days[j].windows+k;
            w->action_index=action_index(r,w->action);
            if(w->action_index>=0)continue;
            int external=native_index(w->action);
            if(external<0)return false;
            w->action_index=-external-1;
        }
    }
    memset(r->role_count,0,sizeof(r->role_count));int priority[3]={-1001,-1001,-1001};
    for(int i=0;i<r->types;i++) {
        QlcgDollType *t=r->type+i;
        t->schedule_index=schedule_index(r,t->schedule);
        if(t->schedule[0]&&t->schedule_index<0)return false;
        if(t->behavior[0]&&!bind_logic(r,t->behavior,&t->logic_index))return false;
        if(t->priority>priority[t->role]){priority[t->role]=t->priority;r->role_count[t->role]=0;}
        if(t->priority==priority[t->role])r->role_types[t->role][r->role_count[t->role]++]=i;
    }
    return true;
}
static void reset_default_schedule(AiRegistry *r,const AiBehavior *legacy) {
    const char *action_ids[]={"village_work","village_campfire","village_sleep","village_square","village_watch"};
    r->actions=5;
    for(int i=0;i<r->actions;i++)strcpy(r->action[i].id,action_ids[i]);
    for(int i=0;i<legacy->day.count;i++)r->action[i].program=legacy->day.windows[i].program;
    r->action[3].program=(AiProgram){3,{
        {{POINT_HOME,0,0},{POINT_MEETING,0,0},.03f,DOLL_AI_WALK},
        {{POINT_MEETING,0,0},{POINT_MEETING,0,0},.94f,DOLL_AI_CAMPFIRE},
        {{POINT_MEETING,0,0},{POINT_HOME,0,0},.03f,DOLL_AI_WALK}
    }};
    r->action[4].program=(AiProgram){1,{{{POINT_HOME,0,0},{POINT_HOME,0,0},1,DOLL_AI_WATCH}}};
    r->schedules=2;
    AiSchedule *s=r->schedule;
    strcpy(s->id,"village_calendar");
    AiDay *normal=s->days+DOLL_DAY_NORMAL;
    *normal=legacy->day;
    for(int i=0;i<normal->count;i++) {
        normal->windows[i].program.count=0;
        strcpy(normal->windows[i].action,action_ids[i]);
    }
    AiWindow visit={.start=8,.end=18,.cycle=10,.action="village_square"};
    s->days[DOLL_DAY_REST]=(AiDay){3,{visit,normal->windows[1],normal->windows[2]}};
    visit.start=10;visit.end=20;visit.cycle=10;
    s->days[DOLL_DAY_FESTIVAL]=(AiDay){2,{visit,normal->windows[2]}};
    AiSchedule *watch=s+1;
    strcpy(watch->id,"village_watch_calendar");
    for(int i=0;i<DOLL_DAY_COUNT;i++) {
        const AiDay *base=s->days+i;
        AiDay *day=watch->days+i;
        day->count=base->count+1;
        day->windows[0]=(AiWindow){.start=21,.end=6,.cycle=1,.action="village_watch"};
        memcpy(day->windows+1,base->windows,(size_t)base->count*sizeof(*base->windows));
    }
}
static void set_role_defaults(QlcgDollType *t) {
    const float homes[3][2]={{.8f,-1},{1.8f,.8f},{.8f,1.8f}};
    const float stations[3][2]={{26.4f,.8f},{.8f,3.3f},{.8f,-6}};
    const float meetings[3][2]={{.7f,-.4f},{2.4f,0},{.3f,2.6f}};
    int role=t->role;
    memcpy(t->home,homes[role],sizeof(t->home));
    memcpy(t->work,stations[role],sizeof(t->work));
    memcpy(t->meeting,meetings[role],sizeof(t->meeting));
    t->via[0]=role==0?8:(square[0]+t->work[0])*.5f;
    t->via[1]=role==0?square[1]:(square[1]+t->work[1])*.5f;
    t->facing[0]=role==2?0:-1;t->facing[1]=role==2?-1:0;
}
void qlcg_doll_types_reset(void) {
    memset(&registry,0,sizeof(registry));native_count=0;ready=true;
    AiBehavior *b=registry.behavior;registry.behaviors=1;
    strcpy(b->id,"village_day");b->day.count=3;
    AiWindow *work=b->day.windows;work->start=6;work->end=18;work->cycle=2;work->condition=1;work->program.count=7;
    const int from[7]={POINT_HOME,POINT_SQUARE,POINT_VIA,POINT_WORK,POINT_WORK,POINT_VIA,POINT_SQUARE};
    const int to[7]={POINT_SQUARE,POINT_VIA,POINT_WORK,POINT_WORK,POINT_VIA,POINT_SQUARE,POINT_HOME};
    for(int i=0;i<7;i++)work->program.phases[i]=(AiPhase){{from[i],0,0},{to[i],0,0},i==3?2.f/3:1.f/18,i==3?DOLL_AI_WORK:DOLL_AI_WALK};
    b->day.windows[1]=(AiWindow){.start=18,.end=21,.cycle=1,.program={1,{{{POINT_HOME,0,0},{POINT_HOME,0,0},1,DOLL_AI_CAMPFIRE}}}};
    b->day.windows[2]=(AiWindow){.start=0,.end=24,.cycle=1,.program={1,{{{POINT_HOME,0,0},{POINT_HOME,0,0},1,DOLL_AI_SLEEP}}}};
    reset_default_schedule(&registry,b);
    registry.default_dialogues.count=1;
    registry.default_dialogues.rules[0]=(QlcgDollDialogueRule){
        .trigger=DOLL_TALK_FIELD,.day=DOLL_DAY_COUNT,.radius=16,.interval=3,.line_count=1,
        .speaker="守夜人",.lines={"田里种着庄稼，请沿田边走。"}
    };
    const char *ids[3]={"farmer_type","artisan_type","scout_type"};
    const char *names[3]={"农耕村民","加工村民","巡领村民"};
    registry.types=3;
    for(int i=0;i<3;i++) {
        QlcgDollType *t=registry.type+i;strcpy(t->id,ids[i]);strcpy(t->name,names[i]);
        strcpy(t->schedule,registry.schedule[i==2?1:0].id);
        t->role=i;t->scale=1;t->dialogue_index=-1;
        set_role_defaults(t);
    }
    bind_registry(&registry);
}
static void ensure(void) {if(!ready)qlcg_doll_types_reset();}
static bool number(const JsVal *obj,const char *key,double low,double high,double fallback,double *out) {
    const JsVal *v=qlcg_js_get(obj,key);double n=v?qlcg_js_num(v,NAN):fallback;
    if((v&&v->type!=JS_NUM)||!isfinite(n)||n<low||n>high)return false;
    *out=n;return true;
}
static bool vector(const JsVal *v,float out[2]) {
    if(!v||v->type!=JS_ARR||v->count!=2)return false;
    for(int i=0;i<2;i++){const JsVal *a=qlcg_js_at(v,i);double n=qlcg_js_num(a,NAN);if(!a||a->type!=JS_NUM||!isfinite(n)||fabs(n)>32)return false;out[i]=(float)n;}
    return true;
}
static int dialogue_choice(const JsVal *value,const char *const *choices,int count) {
    const char *key=qlcg_js_str(value,"");
    for(int i=0;i<count;i++) if(!strcmp(key,choices[i])) return i;
    return -1;
}
static bool dialogue_text(const JsVal *v,char *dest,size_t capacity) {
    const char *s=qlcg_js_str(v,"");
    size_t size=strlen(s);
    if(!size||size>=capacity||!s[strspn(s," ")]||!qlcg_utf8_valid(s,size)) return false;
    for(size_t i=0;i<size;i++) if((unsigned char)s[i]<32) return false;
    memcpy(dest,s,size+1);
    return true;
}
static bool read_dialogues(const JsVal *list,QlcgDollDialogues *set,char *error,size_t cap) {
    static const char *const triggers[]={"player_near","walk","work","campfire","watch","watch_approach",
                                          "watch_return","watch_field","watch_patrol"};
    static const char *const days[]={"normal","rest","festival","any"};
    if(!list||list->type!=JS_ARR||list->count>DOLL_DIALOGUE_RULES) {
        if(error&&cap) snprintf(error,cap,"自动对话应为列表，每个身份最多 %d 条规则。",DOLL_DIALOGUE_RULES);
        return false;
    }
    set->count=list->count;
    for(int i=0;i<list->count;i++) {
        const JsVal *item=list->items[i];
        QlcgDollDialogueRule *rule=set->rules+i;
        const JsVal *day=qlcg_js_get(item,"day"),*speaker=qlcg_js_get(item,"speaker");
        const JsVal *lines=qlcg_js_get(item,"lines");
        const char *problem=NULL;
        int trigger=dialogue_choice(qlcg_js_get(item,"trigger"),triggers,sizeof(triggers)/sizeof(triggers[0]));
        rule->day=day?dialogue_choice(day,days,sizeof(days)/sizeof(days[0])):DOLL_DAY_COUNT;
        double radius,interval;
        if(trigger<0||rule->day<0) problem="请选择有效的触发条件和日期。";
        else if(!number(item,"radius",1,64,8,&radius)) problem="触发距离须为 1 至 64 米。";
        else if(!number(item,"interval",0.5,30,3,&interval)) problem="台词间隔须为 0.5 至 30 秒。";
        else if(speaker&&(!dialogue_text(speaker,rule->speaker,sizeof(rule->speaker))||
                          strstr(rule->speaker,"】"))) problem="请填写有效署名，或留空以使用身份名称。";
        else if(!lines||lines->type!=JS_ARR||lines->count<1||lines->count>DOLL_DIALOGUE_LINES)
            problem="请填写 1 至 4 句台词。";
        else {
            rule->trigger=(QlcgDollTrigger)trigger;rule->radius=(float)radius;rule->interval=(float)interval;
            rule->line_count=lines->count;
            if(!speaker) rule->speaker[0]=0;
            for(int j=0;j<lines->count;j++)
                if(!dialogue_text(lines->items[j],rule->lines[j],sizeof(rule->lines[j]))) {
                    problem="台词须为非空单行文本，每句最多 255 个 UTF-8 字节。";break;
                }
        }
        if(problem) {
            if(error&&cap) snprintf(error,cap,"自动对话规则 %d：%s",i+1,problem);
            return false;
        }
    }
    return true;
}
static bool point(const JsVal *v,AiPoint *p) {
    if(v&&v->type==JS_ARR){float xy[2];if(!vector(v,xy))return false;*p=(AiPoint){POINT_FIXED,xy[0],xy[1]};return true;}
    const char *s=qlcg_js_str(v,"");const char *keys[]={"home","square","via","work","meeting"};
    for(int i=0;i<POINT_FIXED;i++)if(!strcmp(s,keys[i])){*p=(AiPoint){i,0,0};return true;}
    return false;
}
static int state(const char *s) {
    const char *keys[]={"walk","work","campfire","sleep","watch"};
    for(int i=0;i<=DOLL_AI_WATCH;i++)if(!strcmp(s,keys[i]))return i;
    return -1;
}
static bool read_program(const JsVal *phases,AiProgram *program) {
    if(!phases||phases->type!=JS_ARR||phases->count<1||phases->count>AI_PHASES)return false;
    program->count=phases->count;
    double total=0;
    for(int i=0;i<program->count;i++) {
        const JsVal *s=phases->items[i];AiPhase *p=program->phases+i;double duration;
        if(!s||s->type!=JS_OBJ||!point(qlcg_js_get(s,"from"),&p->from)||!point(qlcg_js_get(s,"to"),&p->to)||
           !number(s,"duration",.001,1,1,&duration))return false;
        p->duration=(float)duration;total+=duration;
        p->state=state(qlcg_js_str(qlcg_js_get(s,"state"),""));if(p->state<0)return false;
        if(p->state!=DOLL_AI_WALK&&(p->from.kind!=p->to.kind||p->from.x!=p->to.x||p->from.z!=p->to.z))return false;
    }
    return fabs(total-1)<=.00001;
}
static bool read_day(const JsVal *windows,AiDay *day,bool legacy) {
    if(!windows||windows->type!=JS_ARR||windows->count<(legacy?1:0)||windows->count>AI_WINDOWS)return false;
    day->count=windows->count;
    for(int i=0;i<day->count;i++) {
        const JsVal *a=windows->items[i];AiWindow *w=day->windows+i;
        if(!a||a->type!=JS_OBJ||!number(a,"start",0,24,0,&w->start)||!number(a,"end",0,24,24,&w->end)||
           w->start==w->end||!number(a,"cycle_hours",.05,24,1,&w->cycle))return false;
        const JsVal *condition=qlcg_js_get(a,"when");if(condition&&condition->type!=JS_STR)return false;
        const char *when=qlcg_js_str(condition,"always");
        w->condition=!strcmp(when,"always")?0:!strcmp(when,"work_available")?1:!strcmp(when,"no_work")?2:-1;
        if(w->condition<0)return false;
        if(legacy) {
            if(!read_program(qlcg_js_get(a,"phases"),&w->program))return false;
        } else {
            const char *action=qlcg_js_str(qlcg_js_get(a,"action"),"");
            if(!identifier(action)||qlcg_js_get(a,"phases"))return false;
            strcpy(w->action,action);
        }
    }
    return true;
}
static bool read_behavior(const JsVal *v,AiBehavior *b) {
    if(!v||v->type!=JS_OBJ)return false;
    const char *id=qlcg_js_str(qlcg_js_get(v,"id"),"");
    if(!identifier(id)||!read_day(qlcg_js_get(v,"windows"),&b->day,true))return false;
    strcpy(b->id,id);
    return true;
}
static bool read_action(const JsVal *v,AiAction *a) {
    if(!v||v->type!=JS_OBJ)return false;
    const char *id=qlcg_js_str(qlcg_js_get(v,"id"),"");
    if(!identifier(id)||!read_program(qlcg_js_get(v,"phases"),&a->program))return false;
    strcpy(a->id,id);
    return true;
}
static bool read_schedule(const JsVal *v,AiSchedule *s) {
    if(!v||v->type!=JS_OBJ)return false;
    const char *id=qlcg_js_str(qlcg_js_get(v,"id"),"");
    if(!identifier(id))return false;
    strcpy(s->id,id);
    const char *keys[DOLL_DAY_COUNT]={"normal","rest","festival"};
    for(int i=0;i<DOLL_DAY_COUNT;i++) {
        if(!read_day(qlcg_js_get(v,keys[i]),s->days+i,false))return false;
    }
    return true;
}
static bool read_type(const JsVal *v,QlcgDollType *t,int schema) {
    if(!v||v->type!=JS_OBJ)return false;
    const char *id=qlcg_js_str(qlcg_js_get(v,"id"),"");size_t n=strlen(id);
    const char *name=qlcg_js_str(qlcg_js_get(v,"name"),id);
    const JsVal *behavior=qlcg_js_get(v,"behavior"),*day_schedule=qlcg_js_get(v,"schedule");
    const char *logic=qlcg_js_str(behavior,""),*schedule=qlcg_js_str(day_schedule,"");
    const JsVal *label=qlcg_js_get(v,"name");if(label&&label->type!=JS_STR)return false;
    if(!identifier(id)||n<6||strcmp(id+n-5,"_type")||!*name||strlen(name)>=sizeof(t->name)||
       !qlcg_utf8_valid(name,strlen(name))||(behavior&&!identifier(logic))||
       (day_schedule&&(schema!=2||!identifier(schedule)))||(!behavior&&!day_schedule))return false;
    double role,priority,scale;
    if(!number(v,"role",0,2,0,&role)||floor(role)!=role||!number(v,"priority",-1000,1000,0,&priority)||floor(priority)!=priority||
       !number(v,"scale",.5,1.5,1,&scale))return false;
    strcpy(t->id,id);strcpy(t->name,name);strcpy(t->behavior,logic);t->role=(int)role;t->priority=(int)priority;t->scale=(float)scale;
    strcpy(t->schedule,schedule);
    set_role_defaults(t);
    const char *keys[]={"home","work","via","facing","meeting"};
    float *values[]={t->home,t->work,t->via,t->facing,t->meeting};
    for(int i=0;i<sizeof(values)/sizeof(*values);i++){const JsVal *a=qlcg_js_get(v,keys[i]);if(a&&!vector(a,values[i]))return false;}
    return fabsf(t->facing[0])+fabsf(t->facing[1])>0;
}
static JsVal *load_json(const char *path) {
    FILE *f=path?qlcg_platform_fopen(path,"rb"):NULL;if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long size=ftell(f);rewind(f);
    if(size<1||size>262144){fclose(f);return NULL;}
    char *text=malloc((size_t)size+1);if(!text){fclose(f);return NULL;}
    bool complete=fread(text,1,(size_t)size,f)==(size_t)size;fclose(f);text[size]=0;
    if(memchr(text,0,(size_t)size)||!qlcg_utf8_valid(text,(size_t)size))complete=false;
    const char *start=text,*error=NULL;
    if(size>=3&&(unsigned char)text[0]==239&&(unsigned char)text[1]==187&&(unsigned char)text[2]==191)start+=3;
    JsVal *json=complete?qlcg_js_parse(start,&error):NULL;free(text);return json;
}
static bool duplicate_id(const JsVal *items,int index,const char *id) {
    for(int i=0;i<index;i++)if(!strcmp(id,qlcg_js_str(qlcg_js_get(items->items[i],"id"),"")))return true;
    return false;
}
static void bind_legacy_defaults(AiRegistry *r) {
    for(int i=0;i<3;i++) {
        QlcgDollType *type=r->type+i;
        if(strcmp(type->schedule,"village_calendar")&&strcmp(type->schedule,"village_watch_calendar"))continue;
        type->schedule[0]=0;
        strcpy(type->behavior,"village_day");
    }
}
static bool load_definitions(const char *path,char *error,size_t cap,bool commit) {
    ensure();if(error&&cap)*error=0;
    JsVal *json=load_json(path);
    AiRegistry *next=malloc(sizeof(*next));bool ok=json&&next&&json->type==JS_OBJ&&qlcg_js_unique_keys(json);
    if(next)*next=registry;
    double schema=0;
    const char *section="schema";int index=-1;
    if(ok)ok=number(json,"schema",1,2,NAN,&schema)&&floor(schema)==schema;
    const JsVal *behaviors=qlcg_js_get(json,"behaviors"),*types=qlcg_js_get(json,"doll_types");
    const JsVal *actions=qlcg_js_get(json,"actions"),*schedules=qlcg_js_get(json,"schedules");
    const JsVal *defaults=qlcg_js_get(json,"default_dialogues");
    if(ok&&defaults) ok=read_dialogues(defaults,&next->default_dialogues,error,cap);
    if(ok&&schema==1&&(actions||schedules))ok=false;
    if(ok&&behaviors) {
        section="behaviors";
        ok=behaviors->type==JS_ARR&&behaviors->count<=QLCG_DOLL_LOGIC_MAX;
        for(int i=0;ok&&i<behaviors->count;i++) {
            index=i;AiBehavior b={0};
            if(!read_behavior(behaviors->items[i],&b)||native_index(b.id)>=0||duplicate_id(behaviors,i,b.id)){ok=false;break;}
            int slot=behavior_index(next,b.id);if(slot<0)slot=next->behaviors++;
            if(slot>=QLCG_DOLL_LOGIC_MAX)ok=false;else next->behavior[slot]=b;
            if(ok&&schema==1&&!strcmp(b.id,"village_day"))bind_legacy_defaults(next);
        }
    }
    if(ok&&actions) {
        section="actions";index=-1;
        ok=actions->type==JS_ARR&&actions->count<=QLCG_DOLL_LOGIC_MAX;
        for(int i=0;ok&&i<actions->count;i++) {
            index=i;AiAction a={0};
            if(!read_action(actions->items[i],&a)||native_index(a.id)>=0||duplicate_id(actions,i,a.id)){ok=false;break;}
            int slot=action_index(next,a.id);if(slot<0)slot=next->actions++;
            if(slot>=QLCG_DOLL_LOGIC_MAX)ok=false;else next->action[slot]=a;
        }
    }
    if(ok&&schedules) {
        section="schedules";index=-1;
        ok=schedules->type==JS_ARR&&schedules->count<=QLCG_DOLL_SCHEDULES_MAX;
        for(int i=0;ok&&i<schedules->count;i++) {
            index=i;AiSchedule s={0};
            if(!read_schedule(schedules->items[i],&s)||duplicate_id(schedules,i,s.id)){ok=false;break;}
            int slot=schedule_index(next,s.id);if(slot<0)slot=next->schedules++;
            if(slot>=QLCG_DOLL_SCHEDULES_MAX)ok=false;else next->schedule[slot]=s;
        }
    }
    if(ok&&types) {
        section="doll_types";index=-1;
        ok=types->type==JS_ARR&&types->count<=QLCG_DOLL_TYPES_MAX;
        for(int i=0;ok&&i<types->count;i++) {
            index=i;QlcgDollType t={0};
            if(!read_type(types->items[i],&t,(int)schema)||duplicate_id(types,i,t.id)){ok=false;break;}
            int slot=type_index(next,t.id);if(slot<0)slot=next->types++;
            if(slot>=QLCG_DOLL_TYPES_MAX){ok=false;break;}
            const JsVal *dialogues=qlcg_js_get(types->items[i],"dialogues");
            t.dialogue_index=dialogues?slot:-1;
            if(dialogues&&!read_dialogues(dialogues,next->dialogues+slot,error,cap)){ok=false;break;}
            next->type[slot]=t;
        }
    }
    if(ok) {
        section="references";index=-1;
        ok=(types||behaviors||actions||schedules||defaults)&&bind_registry(next);
    }
    if(ok&&commit)registry=*next;
    if(!ok&&error&&cap&&!*error) {
        if(index>=0)snprintf(error,cap,"invalid %s[%d] in %s",section,index,path?path:"");
        else snprintf(error,cap,"invalid %s in %s",section,path?path:"");
    }
    free(next);qlcg_js_free(json);return ok;
}
bool qlcg_doll_types_load(const char *path,char *error,size_t cap) {
    return load_definitions(path,error,cap,true);
}
bool qlcg_doll_types_validate(const char *path,char *error,size_t cap) {
    return load_definitions(path,error,cap,false);
}
static int compare_path(const void *a,const void *b) {return strcmp((const char*)a,(const char*)b);}
int qlcg_doll_types_load_dir(const char *path) {
    ensure();QlDirectory *dir=qlcg_platform_dir_open(path);if(!dir)return 0;
    char (*files)[1024]=calloc(64,sizeof(*files));if(!files){qlcg_platform_dir_close(dir);return 0;}
    int count=0,loaded=0;bool overflow=false;QlDirectoryEntry entry;
    while(qlcg_platform_dir_next(dir,&entry)) {
        size_t n=strlen(entry.name);
        if(entry.directory||n<5||strcmp(entry.name+n-5,".json")||entry.size>262144)continue;
        if(count>=64){fprintf(stderr,"[doll] file limit: %s\n",path);overflow=true;break;}
        snprintf(files[count++],1024,"%s",entry.name);
    }
    qlcg_platform_dir_close(dir);if(overflow){free(files);return 0;}
    qsort(files,(size_t)count,sizeof(*files),compare_path);
    for(int i=0;i<count;i++) {
        char file[2048],error[256];int n=snprintf(file,sizeof(file),"%s/%s",path,files[i]);
        if(n>0&&n<(int)sizeof(file)&&qlcg_doll_types_load(file,error,sizeof(error)))loaded++;
        else fprintf(stderr,"[doll] rejected %s/%s\n",path,files[i]);
    }
    free(files);return loaded;
}
void qlcg_doll_types_init(void) {
    qlcg_doll_types_reset();qlcg_doll_types_load_dir("content/data/dolls");qlcg_doll_types_load_dir("userdata/mods/dolls");
}
int qlcg_doll_type_count(void) {ensure();return registry.types;}
const QlcgDollDialogues *qlcg_doll_dialogues(const QlcgDollType *type) {
    return type->dialogue_index<0?&registry.default_dialogues:registry.dialogues+type->dialogue_index;
}
const QlcgDollType *qlcg_doll_type(const char *id) {
    ensure();int i=id?type_index(&registry,id):-1;return i<0?NULL:registry.type+i;
}
const QlcgDollType *qlcg_doll_type_for(int role,uint32_t resident_id) {
    ensure();if(role<0||role>2||!registry.role_count[role])return NULL;
    int pick=(int)(resident_id%(unsigned)registry.role_count[role]);
    return registry.type+registry.role_types[role][pick];
}
bool qlcg_doll_logic_register(const char *id,QlcgDollLogicFn sample) {
    ensure();if(!identifier(id)||!sample||behavior_index(&registry,id)>=0||action_index(&registry,id)>=0||native_index(id)>=0||native_count==AI_NATIVE)return false;
    strcpy(native[native_count].id,id);native[native_count++].fn=sample;return true;
}
static void resolve_point(const AiPoint *p,const QlcgDollAIContext *c,float out[2]) {
    const float fixed[2]={p->x,p->z};
    const float *points[]={c->home,square,c->via,c->work,c->meeting,fixed};
    out[0]=points[p->kind][0];out[1]=points[p->kind][1];
}
static bool sample_native(int index,const QlcgDollAIContext *c,QlcgDollAIResult *out) {
    if(index< -native_count||!native[-index-1].fn(c,out))return false;
    return isfinite(out->x)&&isfinite(out->z)&&isfinite(out->yaw)&&fabsf(out->x)<=48&&fabsf(out->z)<=48&&out->state>=0&&out->state<=DOLL_AI_WATCH;
}
static const AiWindow *find_window(const AiDay *day,double hour,bool work_available) {
    for(int i=0;i<day->count;i++) {
        const AiWindow *w=day->windows+i;
        if(w->condition==1&&!work_available)continue;
        if(w->condition==2&&work_available)continue;
        bool within=w->start<w->end?hour>=w->start&&hour<w->end:hour>=w->start||hour<w->end;
        if(within)return w;
    }
    return NULL;
}
static void sample_program(const AiProgram *program,double phase,const QlcgDollAIContext *c,QlcgDollAIResult *out) {
    int segment=0;
    while(segment<program->count-1&&phase>=program->phases[segment].duration)phase-=program->phases[segment++].duration;
    const AiPhase *p=program->phases+segment;float a[2],z[2];resolve_point(&p->from,c,a);resolve_point(&p->to,c,z);
    float t=(float)(phase/p->duration);t=fmaxf(0,fminf(1,t));float vx=z[0]-a[0],vz=z[1]-a[1];
    out->x=a[0]+vx*t;out->z=a[1]+vz*t;out->moving=p->state==DOLL_AI_WALK&&fabsf(vx)+fabsf(vz)>.00001f;
    out->state=p->state;
    if(!out->moving) {
        bool face=p->state==DOLL_AI_WORK||p->state==DOLL_AI_WATCH;
        vx=face?c->facing[0]:0;vz=face?c->facing[1]:1;
        if(p->state==DOLL_AI_CAMPFIRE) {
            const float *target=p->from.kind==POINT_MEETING?square:c->campfire;
            float dx=target[0]-out->x,dz=target[1]-out->z;
            if(fabsf(dx)+fabsf(dz)>.00001f){vx=dx;vz=dz;}
        }
    }
    out->yaw=atan2f(vx,vz);
}
bool qlcg_doll_ai_sample(const QlcgDollType *type,const QlcgDollAIContext *c,QlcgDollAIResult *out) {
    ensure();if(!type||!c||!out||!isfinite(c->local_hour))return false;
    memset(out,0,sizeof(*out));out->x=c->home[0];out->z=c->home[1];out->state=DOLL_AI_SLEEP;
    double hour=fmod(c->local_hour,24);if(hour<0)hour+=24;
    const AiWindow *window=NULL;
    const AiProgram *program=NULL;
    if(type->schedule[0]) {
        if(type->schedule_index<0||type->schedule_index>=registry.schedules)return false;
        QlcgDollDayKind day=c->day_kind;
        if((unsigned)day>=DOLL_DAY_COUNT)day=DOLL_DAY_NORMAL;
        window=find_window(&registry.schedule[type->schedule_index].days[day],hour,c->work_available);
        if(window) {
            int action=window->action_index;
            if(action<0)return sample_native(action,c,out);
            program=&registry.action[action].program;
        }
    }
    if(!window&&type->behavior[0]) {
        int index=type->logic_index;
        if(index<0)return sample_native(index,c,out);
        if(index>=registry.behaviors)return false;
        window=find_window(&registry.behavior[index].day,hour,c->work_available);
        if(window)program=&window->program;
    }
    if(window) {
        double elapsed=hour-window->start;if(elapsed<0)elapsed+=24;
        sample_program(program,fmod(elapsed/window->cycle,1),c,out);
    }
    return true;
}
