#include "civ_content.h"
#include "../world/model/model_json.h"
#include "../world/model/plant_def.h"
#include "../world/climate_zone.h"
#include "../../platform/platform.h"
#include "../../core/binary.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    QlcgFarmlandType farms[QLCG_FARMLAND_TYPES];
    QlcgCropType crops[QLCG_CROP_TYPES];
    QlcgClimateType climates[QLCG_CLIMATE_TYPES];
    int nf,nc,nt;
} CivCatalog;
static CivCatalog *catalog;
static bool attempted;
static const QlcgCivRange temp_range={-100,100},amp_range={0,100},wet_range={0,1},rain_range={0,20000};

static int order(const void *a,const void *b) { return strcmp(a,b); }
static bool identifier(const char *s) {
    if (!s || !*s || strlen(s)>=QLCG_CIV_TYPE_ID) return false;
    for (;*s;s++) if (!((*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||strchr("._:-",*s))) return false;
    return true;
}
static bool text(const JsVal *o,const char *key,char *out,size_t size,bool id) {
    const char *s=qlcg_js_str(qlcg_js_get(o,key),NULL);
    if (!s || !*s || strlen(s)>=size || !qlcg_utf8_valid(s,strlen(s)) || (id&&!identifier(s))) return false;
    memcpy(out,s,strlen(s)+1);return true;
}
static bool number(const JsVal *o,const char *key,double fallback,double min,double max,double *out) {
    const JsVal *v=qlcg_js_get(o,key);double n=v?qlcg_js_num(v,NAN):fallback;
    if ((v&&v->type!=JS_NUM)||!isfinite(n)||n<min||n>max) return false;
    *out=n;return true;
}
static bool integer(const JsVal *o,const char *key,int fallback,int min,int max,int *out) {
    double n;if (!number(o,key,fallback,min,max,&n)||floor(n)!=n)return false;*out=(int)n;return true;
}
static bool real(const JsVal *o,const char *key,float fallback,float min,float max,float *out) {
    double n;if(!number(o,key,fallback,min,max,&n))return false;*out=(float)n;return true;
}
static bool range(const JsVal *o,const char *key,QlcgCivRange bounds,QlcgCivRange *out) {
    const JsVal *v=qlcg_js_get(o,key);*out=bounds;if(!v)return true;
    if(v->type!=JS_ARR||v->count!=2||v->items[0]->type!=JS_NUM||v->items[1]->type!=JS_NUM)return false;
    double a=qlcg_js_num(v->items[0],NAN),b=qlcg_js_num(v->items[1],NAN);
    if(!isfinite(a)||!isfinite(b)||a>b||a<bounds.min||b>bounds.max)return false;
    out->min=(float)a;out->max=(float)b;return true;
}
static bool rgb(const JsVal *o,const char *key,uint32_t fallback,uint32_t *out) {
    const JsVal *v=qlcg_js_get(o,key);if(!v){*out=fallback;return true;}
    const char *s=qlcg_js_str(v,NULL);if(!s||strlen(s)!=7||s[0]!='#')return false;
    for(int i=1;i<7;i++)if(!strchr("0123456789abcdefABCDEF",s[i]))return false;
    *out=(uint32_t)strtoul(s+1,NULL,16);return true;
}
static const QlcgFarmlandType *farm_in(const CivCatalog *c,const char *id) {
    for(int i=0;i<c->nf;i++)if(!strcmp(c->farms[i].id,id))return c->farms+i;return NULL;
}
static int climate_in(const CivCatalog *c,const char *id) {
    for(int i=0;i<c->nt;i++)if(!strcmp(c->climates[i].id,id))return i;return -1;
}
static bool farmland(CivCatalog *c,const JsVal *o) {
    if(c->nf>=QLCG_FARMLAND_TYPES||o->type!=JS_OBJ)return false;
    QlcgFarmlandType t={0};
    if(!text(o,"id",t.id,sizeof(t.id),true)||!text(o,"name",t.name,sizeof(t.name),false)||
       farm_in(c,t.id)||!strncmp(t.id,"qlcg.legacy.",12))return false;
    if(!rgb(o,"soil_rgb",0x705135,&t.soil_rgb)||!real(o,"width",QLCG_FARMLAND_SIDE,4,QLCG_FARMLAND_SIDE,&t.width)||
       !real(o,"depth",QLCG_FARMLAND_SIDE,4,QLCG_FARMLAND_SIDE,&t.depth)||!real(o,"ridge_period",4,4,24,&t.ridge_period)||
       !real(o,"ridge_strength",.25f,0,1,&t.ridge_strength)||!integer(o,"priority",0,-9999,9999,&t.priority)||
       !integer(o,"chunk_type",9010,9010,9019,&t.chunk_type))return false;
    c->farms[c->nf++]=t;return true;
}
static bool crop(CivCatalog *c,const JsVal *o) {
    if(c->nc>=QLCG_CROP_TYPES||o->type!=JS_OBJ)return false;
    QlcgCropType t={0};t.selectable=true;
    if(!text(o,"id",t.id,sizeof(t.id),true)||!text(o,"name",t.name,sizeof(t.name),false)||
       !text(o,"plant_id",t.plant_id,sizeof(t.plant_id),true)||!text(o,"farmland_id",t.farmland_id,sizeof(t.farmland_id),true)||
       !text(o,"role",t.role,sizeof(t.role),true)||strncmp(t.plant_id,"nong.",5)||!strncmp(t.id,"qlcg.legacy.",12))return false;
    char model_path[256];snprintf(model_path,sizeof(model_path),"content/data/world/plants/herbaceous/%s.json",t.plant_id);
    if(!qlcg_pd_species_by_id(t.plant_id)&&!qlcg_platform_exists(model_path))return false;
    for(int i=0;i<c->nc;i++)if(!strcmp(c->crops[i].id,t.id))return false;
    if(!integer(o,"annual_food",0,0,100000,&t.annual_food)||!integer(o,"annual_wood",0,0,100000,&t.annual_wood)||
       !integer(o,"annual_fiber",0,0,100000,&t.annual_fiber)||!integer(o,"maturity_days",30,1,3650,&t.maturity_days)||
       !integer(o,"continuous_days",90,1,3650,&t.continuous_days)||!integer(o,"priority",0,-9999,9999,&t.priority)||
       !range(o,"mean_temp",temp_range,&t.mean_temp)||!range(o,"amplitude",amp_range,&t.amplitude)||
       !range(o,"temperature",temp_range,&t.temperature)||!range(o,"moisture",wet_range,&t.moisture))return false;
    const JsVal *sel=qlcg_js_get(o,"selectable");if(sel&&sel->type!=JS_BOOL)return false;
    if(sel)t.selectable=sel->bval!=0;
    int mask;if(!integer(o,"base_zone_mask",0,0,0xfffff,&mask))return false;t.base_zone_mask=(uint32_t)mask;
    const JsVal *ids=qlcg_js_get(o,"climate_ids");
    if(ids){if(ids->type!=JS_ARR||ids->count>QLCG_CROP_CLIMATES)return false;
        for(int i=0;i<ids->count;i++){const char *id=qlcg_js_str(ids->items[i],NULL);if(!identifier(id))return false;strcpy(t.climate_ids[t.climate_count++],id);}}
    c->crops[c->nc++]=t;return true;
}
static bool climate(CivCatalog *c,const JsVal *o) {
    if(c->nt>=QLCG_CLIMATE_TYPES||o->type!=JS_OBJ)return false;
    QlcgClimateType t={0};
    if(!text(o,"id",t.id,sizeof(t.id),true)||!text(o,"name",t.name,sizeof(t.name),false)||climate_in(c,t.id)>=0||
       !integer(o,"base_zone",-1,0,19,&t.base_zone)||!integer(o,"priority",0,-9999,9999,&t.priority)||
       !rgb(o,"color_rgb",0x808080,&t.color_rgb)||!range(o,"mean_temp",temp_range,&t.mean_temp)||
       !range(o,"amplitude",amp_range,&t.amplitude)||!range(o,"moisture",wet_range,&t.moisture)||
       !range(o,"rainfall",rain_range,&t.rainfall)||!range(o,"monsoon",wet_range,&t.monsoon))return false;
    int mask;if(!integer(o,"match_zone_mask",0,0,0xfffff,&mask))return false;t.match_zone_mask=(uint32_t)mask;
    c->climates[c->nt++]=t;return true;
}
static bool definitions(CivCatalog *c,const JsVal *root) {
    int schema;if(!root||root->type!=JS_OBJ||!qlcg_js_unique_keys(root)||!integer(root,"schema",0,1,1,&schema))return false;
    const char *names[]={"farmland_types","climate_types","crop_types"};
    for(int k=0;k<3;k++){
        const JsVal *a=qlcg_js_get(root,names[k]);if(!a)continue;if(a->type!=JS_ARR)return false;
        for(int i=0;i<a->count;i++)if(!(k==0?farmland(c,a->items[i]):k==1?climate(c,a->items[i]):crop(c,a->items[i])))return false;
    }
    for(int i=0;i<c->nc;i++){
        const QlcgCropType *t=c->crops+i;if(!farm_in(c,t->farmland_id))return false;
        for(int j=0;j<t->climate_count;j++)if(climate_in(c,t->climate_ids[j])<0)return false;
    }
    return true;
}
static void legacy(CivCatalog *c) {
    QlcgFarmlandType f={0};strcpy(f.id,"qlcg.legacy.field");strcpy(f.name,"兼容旱田");
    f.width=f.depth=QLCG_FARMLAND_SIDE;f.ridge_period=4;f.ridge_strength=.25f;f.soil_rgb=0x705135;f.chunk_type=9010;c->farms[c->nf++]=f;
    for(int i=0;i<2;i++){
        QlcgCropType t={0};strcpy(t.id,i?"qlcg.legacy.ma":"qlcg.legacy.su");strcpy(t.name,i?"兼容麻田":"兼容粟田");
        strcpy(t.plant_id,i?"nong.ma":"nong.su");strcpy(t.role,i?"fiber":"food");strcpy(t.farmland_id,f.id);
        t.annual_food=i?600:1200;t.annual_fiber=i?120:0;t.maturity_days=30;t.continuous_days=90;
        t.mean_temp=temp_range;t.amplitude=amp_range;t.temperature=(QlcgCivRange){8,100};t.moisture=(QlcgCivRange){.12f,1};c->crops[c->nc++]=t;
    }
    for(int i=0;i<QLCG_ZONE_COUNT;i++){
        QlcgClimateType t={0};const QlcgClimateZoneInfo *z=qlcg_climate_zone_info(i);
        snprintf(t.id,sizeof(t.id),"%s",z->key);strcpy(t.name,t.id);t.base_zone=i;t.priority=-100000;
        t.color_rgb=((uint32_t)z->rgb[0]<<16)|((uint32_t)z->rgb[1]<<8)|z->rgb[2];t.match_zone_mask=1u<<i;
        t.mean_temp=temp_range;t.amplitude=amp_range;t.moisture=wet_range;t.rainfall=rain_range;t.monsoon=wet_range;c->climates[c->nt++]=t;
    }
}
static bool load_file(CivCatalog *c,const char *path) {
    FILE *file=qlcg_platform_fopen(path,"rb");if(!file)return false;
    if(fseek(file,0,SEEK_END)){fclose(file);return false;}long n=ftell(file);rewind(file);
    if(n<=0||n>256*1024){fclose(file);return false;}char *buf=malloc((size_t)n+1);if(!buf){fclose(file);return false;}
    bool read=fread(buf,1,(size_t)n,file)==(size_t)n;fclose(file);buf[n]=0;
    if(memchr(buf,0,(size_t)n)||!qlcg_utf8_valid(buf,(size_t)n))read=false;
    const char *error=NULL;const char *json=buf;
    if(n>=3&&(unsigned char)buf[0]==239&&(unsigned char)buf[1]==187&&(unsigned char)buf[2]==191)json+=3;
    JsVal *root=read?qlcg_js_parse(json,&error):NULL;free(buf);
    CivCatalog *staged=malloc(sizeof(*staged));if(!staged){qlcg_js_free(root);return false;}*staged=*c;
    bool ok=definitions(staged,root);if(ok)*c=*staged;free(staged);qlcg_js_free(root);return ok;
}
static bool directory(CivCatalog *c,const char *path,bool required,char *error,size_t capacity) {
    QlDirectory *dir=qlcg_platform_dir_open(path);if(!dir){if(required&&error&&capacity)snprintf(error,capacity,"Cannot open definitions directory: %s",path);return !required;}
    char names[64][256];int count=0;QlDirectoryEntry e;bool ok=true;
    while(qlcg_platform_dir_next(dir,&e)){
        size_t n=strlen(e.name);if(e.directory||n<6||strcmp(e.name+n-5,".json"))continue;
        if(count==64||n>=sizeof(names[0])||e.size>256*1024){if(error&&capacity)snprintf(error,capacity,"Definition directory limit: %s/%s",path,e.name);ok=false;break;}
        strcpy(names[count++],e.name);
    }
    qlcg_platform_dir_close(dir);if(!ok)return false;qsort(names,count,sizeof(names[0]),order);
    for(int i=0;i<count;i++){
        char full[1024];int n=snprintf(full,sizeof(full),"%s/%s",path,names[i]);
        if(n<0||n>=(int)sizeof(full)||!load_file(c,full)){
            if(error&&capacity)snprintf(error,capacity,"Invalid civilization definitions: %s/%s",path,names[i]);
            fprintf(stderr,"[civilization] skipped invalid definitions: %s/%s\n",path,names[i]);if(required)return false;
        }
    }
    if(required&&!count&&error&&capacity)snprintf(error,capacity,"No JSON definitions in %s",path);
    return !required||count>0;
}
bool qlcg_civ_content_load(const char *builtins,const char *mods,char *error,size_t capacity) {
    if(error&&capacity)*error=0;if(catalog)return true;attempted=true;
    CivCatalog *made=calloc(1,sizeof(*made));if(!made){if(error&&capacity)snprintf(error,capacity,"Cannot allocate civilization registry");return false;}legacy(made);
    if(!builtins&&error&&capacity)snprintf(error,capacity,"Missing builtin definitions path");
    bool ok=builtins&&directory(made,builtins,true,error,capacity);
    if(!ok){
        memset(made,0,sizeof(*made));legacy(made);
        QlcgFarmlandType f=made->farms[0];strcpy(f.id,"qlcg.dry_field");strcpy(f.name,"旱田");made->farms[made->nf++]=f;
        for(int i=0;i<2;i++){
            QlcgCropType t=made->crops[i];strcpy(t.id,i?"nong.ma":"nong.su");strcpy(t.farmland_id,f.id);
            strcpy(t.name,i?"麻":"粟");t.annual_food=i?0:1800;t.selectable=true;made->crops[made->nc++]=t;
        }
        fprintf(stderr,"[civilization] builtin definitions unavailable; safe defaults retained\n");
    } else if(mods&&!directory(made,mods,false,error,capacity)) {
        fprintf(stderr,"[civilization] mod directory limits exceeded; mods ignored\n");
    }
    qsort(made->farms,made->nf,sizeof(made->farms[0]),order);qsort(made->crops,made->nc,sizeof(made->crops[0]),order);
    catalog=made;return ok;
}
static void ensure(void) {
    if(catalog||attempted)return;
    char error[512];if(!qlcg_civ_content_load("content/data/civilization","userdata/mods/civilization",error,sizeof(error))){
        if(!catalog){catalog=calloc(1,sizeof(*catalog));if(catalog){legacy(catalog);qsort(catalog->crops,catalog->nc,sizeof(catalog->crops[0]),order);}}
        fprintf(stderr,"[civilization] builtin load failed; safe fallback active\n");
    }
}
bool qlcg_civ_content_ready(void){ensure();return catalog!=NULL;}
void qlcg_civ_content_free(void){free(catalog);catalog=NULL;attempted=false;}
int qlcg_farmland_type_count(void){ensure();return catalog?catalog->nf:0;}
int qlcg_crop_type_count(void){ensure();return catalog?catalog->nc:0;}
int qlcg_climate_type_count(void){ensure();return catalog?catalog->nt:0;}
const QlcgFarmlandType *qlcg_farmland_type_at(int i){ensure();return catalog&&i>=0&&i<catalog->nf?catalog->farms+i:NULL;}
const QlcgCropType *qlcg_crop_type_at(int i){ensure();return catalog&&i>=0&&i<catalog->nc?catalog->crops+i:NULL;}
const QlcgClimateType *qlcg_climate_type_at(int i){ensure();return catalog&&i>=0&&i<catalog->nt?catalog->climates+i:NULL;}
const QlcgFarmlandType *qlcg_farmland_type_find(const char *id){ensure();return catalog&&id?bsearch(id,catalog->farms,catalog->nf,sizeof(catalog->farms[0]),order):NULL;}
const QlcgCropType *qlcg_crop_type_find(const char *id){ensure();return catalog&&id?bsearch(id,catalog->crops,catalog->nc,sizeof(catalog->crops[0]),order):NULL;}
const QlcgClimateType *qlcg_climate_type_find(const char *id){ensure();int i=catalog&&id?climate_in(catalog,id):-1;return i>=0?catalog->climates+i:NULL;}
static bool contains(QlcgCivRange r,float n){return isfinite(n)&&n>=r.min&&n<=r.max;}
int qlcg_climate_type_match(float mean,float amp,float wet,float rain,float monsoon,int base){
    ensure();if(!catalog||base<0||base>=QLCG_ZONE_COUNT)return QLCG_ZONE_UNKNOWN;
    int best=base;
    for(int i=QLCG_ZONE_COUNT;i<catalog->nt;i++){
        const QlcgClimateType *t=catalog->climates+i,*b=catalog->climates+best;
        if((t->match_zone_mask&&!(t->match_zone_mask&(1u<<base)))||!contains(t->mean_temp,mean)||!contains(t->amplitude,amp)||
           !contains(t->moisture,wet)||!contains(t->rainfall,rain)||!contains(t->monsoon,monsoon))continue;
        if(t->priority>b->priority||(t->priority==b->priority&&strcmp(t->id,b->id)<0))best=i;
    }
    return best;
}
const QlcgCropType *qlcg_crop_type_select(const MacroWorld *w,int cell,const char *role){
    ensure();if(!catalog||!w||!role||cell<0||cell>=w->cell_count)return NULL;
    const QlcgClimateType *climate=qlcg_climate_type_cell(w,cell);const QlcgCropType *best=NULL;
    if(!climate)return NULL;
    float mean=w->temperature?w->temperature[cell]:15,amp=w->season_amp?fabsf(w->season_amp[cell]):0,wet=w->moisture?w->moisture[cell]:.5f;
    for(int i=0;i<catalog->nc;i++){
        const QlcgCropType *t=catalog->crops+i;
        if(!t->selectable||strcmp(t->role,role)||!contains(t->mean_temp,mean)||!contains(t->amplitude,amp)||!contains(t->moisture,wet)||
           (t->base_zone_mask&&!(t->base_zone_mask&(1u<<climate->base_zone))))continue;
        if(t->climate_count){bool found=false;for(int j=0;j<t->climate_count;j++)if(!strcmp(t->climate_ids[j],climate->id))found=true;if(!found)continue;}
        if(!best||t->priority>best->priority||(t->priority==best->priority&&strcmp(t->id,best->id)<0))best=t;
    }
    return best;
}
