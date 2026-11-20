#include "build_internal.h"
#include "systems/world/model/plant_material.h"
#include "systems/world/model/rock_def.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

enum { CAT_MODEL,CAT_LOT,CAT_FARM,CAT_BRUSH,CAT_PLANT,CAT_ROCK,CAT_FLAG,CAT_TEMPLATE };
typedef struct { int kind,value; const char *name; int land; } Card;
static const char *brushes[]={"抬升地面","降低地面","平滑坡面","整平全块","框选平整","挖池引水","填水造陆","加密草皮","清理草皮",
    "青石板","夯土","白沙","黄沙","溪砾","耕土"};
static const char *templates[]={"随机合院","二房一院","三房合院","四房大宅"};
static const char *views[]={"斜视","顶视","实景"};
static bool button(UiContext *ui,float left,float y,float width,float row,int i,int n,const char *s,bool on) {
    float w=width/n;
    float x=left+i*w+1;
    bool hover=ui_hit(ui,x,y+1,w-2,row-2);
    ui_draw_rect(ui,x,y+1,w-2,row-2,on?ui->pal.accent_tea:ui->pal.bg_card);
    ui_draw_border(ui,x,y+1,w-2,row-2,1,hover || on?ui->pal.border_gold:ui->pal.border_soft);
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,row*.39f,on,x+3,y+row*.21f,w-8,1,
        qlcg_i18n_content_text(s),on?ui_rgba(255,250,239,255):ui->pal.text_main);
    return hover && ui->mouse_clicked;
}
static void choose(Card c) {
    build.action=B_PLACE;build.moving=-1;build.has_start=false;build.message[0]=0;build.manual_yaw=false;
    switch(c.kind) {
    case CAT_MODEL: build.model=c.value;break;
    case CAT_LOT: build.lot_type=c.value<0?LOT_PRESET:c.value;if(c.value<0) build.pack=-1-c.value;break;
    case CAT_FARM: build.crop=c.value;build.lot_type=c.land;break;
    case CAT_BRUSH: build.prop=0;build.brush_tool=c.value>=LOT_PAINT?LOT_PAINT:c.value;build.material=c.value>=LOT_PAINT?1+c.value-LOT_PAINT:0;break;
    case CAT_PLANT: build.prop=CHD_PROP_PLANT_BASE+c.value;break;
    case CAT_ROCK: build.prop=CHD_PROP_ROCK_BASE+c.value;break;
    case CAT_FLAG: build.action=B_FLAG;break;
    case CAT_TEMPLATE: build.action=B_TEMPLATE;build.rooms=c.value;break;
    }
}
static bool selected(Card c) {
    if(c.kind==CAT_TEMPLATE) return build.action==B_TEMPLATE && build.rooms==c.value;
    if(c.kind==CAT_FLAG) return build.action==B_FLAG;
    if(build.action!=B_PLACE) return false;
    switch(c.kind) {
    case CAT_MODEL:return build.model==c.value;
    case CAT_LOT:return c.value<0?build.lot_type==LOT_PRESET && build.pack==-1-c.value:build.lot_type==c.value;
    case CAT_FARM:return build.lot_type==c.land && build.crop==c.value;
    case CAT_PLANT:return build.prop==CHD_PROP_PLANT_BASE+c.value;
    case CAT_ROCK:return build.prop==CHD_PROP_ROCK_BASE+c.value;
    case CAT_BRUSH:return !build.prop && c.value==(build.brush_tool==LOT_PAINT?LOT_PAINT+build.material-1:build.brush_tool);
    default:return false;
    }
}
static int catalog(Card cards[512]) {
    int n=0;
    if(!build.mode) {
        cards[n++]=(Card){CAT_LOT,LOT_CUSTOM,"自定义用地"};
        cards[n++]=(Card){CAT_LOT,LOT_LAND,"自然有主之地"};
        for(int i=0;i<qlcg_lot_style_count();i++) cards[n++]=(Card){CAT_LOT,-1-i,qlcg_lot_style_name(i)};
        for(int i=0;i<qlcg_crop_type_count() && n<512;i++) {
            const QlcgCropType *c=qlcg_crop_type_at(i);
            const QlcgFarmlandType *type=qlcg_farmland_type_find(c->farmland_id);
            if(!c->selectable || !type || type->chunk_type<9010) continue;
            cards[n++]=(Card){CAT_FARM,i,c->name,type->chunk_type};
            if(type->chunk_type==9011 && n<512) cards[n++]=(Card){CAT_FARM,i,c->name,9012};
        }
        const char *garden[]={"yao.qiancao","nong.zisu","nong.dadou"};
        for(int i=0;i<3 && n<512;i++) {
            int species=qlcg_plant_species_index(garden[i]);
            const PdSpecies *sp=qlcg_pd_species_by_id(garden[i]);
            if(species>=0 && sp) cards[n++]=(Card){CAT_FARM,-1-species,sp->base.name,9013};
        }
    } else if(build.mode==BUILD_TERRAIN) {
        if(build.filter==0) for(int i=0;i<sizeof(brushes)/sizeof(*brushes);i++) cards[n++]=(Card){CAT_BRUSH,i,brushes[i]};
        if(build.filter==1) for(int i=0;i<qlcg_plant_species_count() && n<512;i++) {
            const PdSpecies *sp=qlcg_pd_species_by_id(qlcg_plant_species_id(i));
            cards[n++]=(Card){CAT_PLANT,i,sp&&sp->base.name?sp->base.name:qlcg_plant_species_id(i)};
        }
        if(build.filter==2) for(int i=0;i<qlcg_rock_count() && n<512;i++) cards[n++]=(Card){CAT_ROCK,i,qlcg_rock_name(i)};
    } else {
        if(build.mode==BUILD_BUY && !build.filter) {
            cards[n++]=(Card){CAT_MODEL,LOT_TEA,"双杯茶席"};
            for(int id=LOT_WOOD_CHAIR;id<=LOT_TEA_SHELTER;id++) cards[n++]=(Card){CAT_MODEL,id,qlcg_lot_model(id)->name};
        }
        if(build.mode==BUILD_BUY && build.filter==3) cards[n++]=(Card){CAT_MODEL,LOT_POT,"陶罐盆栽"};
        for(int ordinal=0;n<500;ordinal++) {
            int i=qlcg_lot_style_unit(build.pack,ordinal);
            if(i<0) break;
            const QlcgBuildingModel *m=qlcg_lot_model(i);
            if(build.mode==BUILD_HOUSE && build.filter==m->kind) cards[n++]=(Card){CAT_MODEL,i,m->name};
            if(build.mode!=BUILD_BUY || m->kind!=QLCG_BUILDING_YARD) continue;
            int group=strstr(m->unit_id,"fire")||strstr(m->unit_id,"stove")?1:strstr(m->unit_id,"garden")?3:2;
            if(build.filter==group) cards[n++]=(Card){CAT_MODEL,i,m->name};
        }
        if(build.mode==BUILD_HOUSE && build.filter==3) for(int i=LOT_FENCE;i<=LOT_DECK;i++) cards[n++]=(Card){CAT_MODEL,i,qlcg_lot_model(i)->name};
        if(build.mode==BUILD_HOUSE && build.filter==3) for(int id=LOT_TIMBER;id<=LOT_WIDE_GATE;id++) cards[n++]=(Card){CAT_MODEL,id,qlcg_lot_model(id)->name};
        if(build.mode==BUILD_HOUSE && build.filter==4) for(int i=0;i<4;i++) cards[n++]=(Card){CAT_TEMPLATE,i?i+1:0,templates[i]};
        if(build.mode==BUILD_BUY && build.filter==1) cards[n++]=(Card){CAT_MODEL,LOT_LAMP,"庭院石灯"};
        if(build.mode==BUILD_BUY && build.filter==4 && build.cx==build.flag_cx && build.cz==build.flag_cz)
            cards[n++]=(Card){CAT_FLAG,0,"移动主旗"};
    }
    return n;
}
/* 卡片标题截断仍保留小型剪影 */
static void card_icon(UiContext *ui,Card card,float x,float y,float size,uint32_t ink) {
    int shape=0;
    if(card.kind==CAT_FARM || card.kind==CAT_PLANT) shape=1;
    else if(card.kind==CAT_MODEL) {
        const QlcgBuildingModel *m=qlcg_lot_model(card.value);
        shape=m->kind==QLCG_BUILDING_ROOM?2:m->kind==QLCG_BUILDING_DOOR?3:4;
    } else if(card.kind==CAT_TEMPLATE || (card.kind==CAT_LOT && card.value<0)) shape=2;
    else if(card.kind==CAT_FLAG) shape=5;
    else if(card.kind==CAT_ROCK || card.kind==CAT_BRUSH) shape=6;
    static const float lines[][16]={
        {0,.4,.5,.14, 0,.4,.5,.72, .5,.72,1,.4, 1,.4,.5,.14},
        {.5,.94,.5,.4, .5,.65,.13,.25, .13,.25,.5,.4, .5,.5,.9,.13},
        {0,.45,.5,.1, .5,.1,1,.45, .18,.4,.18,.84, .18,.84,.86,.84},
        {.18,.9,.18,.14, .18,.14,.84,.14, .84,.14,.84,.9, .34,.8,.34,.3},
        {.08,.46,.88,.46, .2,.46,.2,.86, .76,.46,.76,.86, .75,.46,.75,.16},
        {.22,.96,.22,.08, .22,.08,.88,.14, .88,.14,.67,.43, .67,.43,.22,.37},
        {.05,.79,.32,.36, .32,.36,.67,.19, .67,.19,.94,.79, .94,.79,.05,.79}
    };
    const float *l=lines[shape];
    for(int i=0;i<16;i+=4) ui_draw_line(ui,x+size*l[i],y+size*l[i+1],x+size*l[i+2],y+size*l[i+3],
        fmaxf(1,size*.075f),ink);
}
static void dock(UiContext *ui,float left,float width,float y,float row) {
    if(build.compact) return;
    int n=0;
    const char **filters=NULL;
    static const char *terrain[]={"地形与粉刷","植栽","山石"};
    static const char *house[]={"屋","院","门","围墙与平台","合院模板"};
    static const char *buy[]={"坐具茶席","火塘灯具","工坊","农圃花艺","主旗"};
    if(build.mode==BUILD_TERRAIN) {filters=terrain;n=3;}
    if(build.mode==BUILD_HOUSE) {filters=house;n=5;}
    if(build.mode==BUILD_BUY) {filters=buy;n=5;}
    if(!build.mode) {
        const char *acts[]={"选区块","拓地","收地","编辑此用地","换种子"};
        int ids[]={B_SELECT,B_EXPAND,B_RELEASE};
        for(int i=0;i<5;i++) if(button(ui,left,y,width,row,i,5,acts[i],i<3 && build.action==ids[i])) {
            if(i<3) build.action=ids[i];
            else if(i==3) {
                if(!build_lot() || build_lot()->type!=LOT_CUSTOM) {
                    build.lot_type=LOT_CUSTOM;build.action=B_PLACE;
                    build_click(build.cx,build.cz,0,0,false);
                }
                build_mode(BUILD_HOUSE);
            }
            else {build.seed=build.seed*1664525u+1013904223u;build.action=B_PLACE;build.lot_type=LOT_PRESET;}
        }
    } else for(int i=0;i<n;i++) if(button(ui,left,y,width,row,i,n,filters[i],i==build.filter)) {build.filter=i;build.page=0;}
    Card cards[512];n=catalog(cards);
    int visible=qlcg_clampi((int)(width/(105*ui->met.size)),2,8);
    int pages=(n+visible-1)/visible;if(pages<1) pages=1;
    build.page=qlcg_clampi(build.page,0,pages-1);
    float arrow=fminf(row,width*.08f),cw=(width-2*arrow)/visible;
    if(button(ui,left,y+row,arrow,row,0,1,"‹",false)) build.page=(build.page+pages-1)%pages;
    if(button(ui,left+width-arrow,y+row,arrow,row,0,1,"›",false)) build.page=(build.page+1)%pages;
    if(ui->wheel_delta && ui->mouse_y>=y) build.page=qlcg_clampi(build.page+(ui->wheel_delta>0?-1:1),0,pages-1);
    for(int j=0;j<visible;j++) {
        int i=build.page*visible+j;if(i>=n) break;
        Card card=cards[i];float x=left+arrow+j*cw;
        bool on=selected(card);
        if(ui_hit(ui,x,y+row,cw,row) && ui->mouse_clicked) choose(card);
        ui_draw_rect(ui,x+1,y+row+1,cw-2,row-2,on?ui->pal.accent_tea:ui->pal.bg_card);
        ui_draw_border(ui,x+1,y+row+1,cw-2,row-2,1,on?ui->pal.border_gold:ui->pal.border_soft);
        char details[96]="32 × 32 m";
        if(card.kind==CAT_MODEL) {
            const QlcgBuildingModel *m=qlcg_lot_model(card.value);
            snprintf(details,sizeof(details),"%.1f × %.1f m%s",m->bounds_max[0]-m->bounds_min[0],m->bounds_max[2]-m->bounds_min[2],
                card.value==LOT_TEA?" · 茶聚":strstr(m->unit_id,"campfire")?" · 围火":"");
        } else if(card.kind==CAT_FARM) {
            snprintf(details,sizeof(details),"%s",qlcg_chunk_type_name(qlcg_get_chunk_type(card.land)));
        } else if(card.kind==CAT_TEMPLATE) strcpy(details,"16 × 16 m");
        else if(card.kind==CAT_BRUSH) strcpy(details,"2 m 网格");
        else if(card.kind==CAT_PLANT || card.kind==CAT_ROCK || card.kind==CAT_FLAG) strcpy(details,"定点放置");
        uint32_t ink=on?ui_rgba(255,252,240,255):ui->pal.text_main;
        float icon=fminf(row*.46f,cw*.19f);
        card_icon(ui,card,x+5,y+row*1.16f,icon,ink);
        ui_draw_text_ellipsis(ui,UI_FACE_BODY,row*.34f,true,x+icon+9,y+row*1.04f,cw-icon-13,0,qlcg_i18n_content_text(card.name),ink);
        ui_draw_text_ellipsis(ui,UI_FACE_BODY,row*.28f,false,x+5,y+row*1.55f,cw-10,0,details,ink);
    }
}
void build_toolbar(UiContext *ui,float top,float bottom,float row) {
    float x=ui->safe_edges[0]+3,w=ui->win_w-ui->safe_edges[2]-3-x,y=ui->safe_edges[1]+2;
    ui_draw_rect(ui,x,y,w,2*row,with_alpha(ui->pal.bg_card,.97f));
    ui_draw_rect(ui,x,bottom,w,ui->win_h-ui->safe_edges[3]-bottom,with_alpha(ui->pal.bg_card,.98f));
    const char *tabs[]={"返回","区块规划","编辑地块","搭建建筑","摆放家具",views[build.view],build.compact?"展开目录":"收起目录"};
    for(int i=0;i<7;i++) if(button(ui,x,y,w,row,i,7,tabs[i],i==build.mode+1)) {
        if(i==0) {build_back();return;}
        else if(i<=4) build_mode(i-1);
        else if(i==5) build.view=(build.view+1)%3;
        else build.compact=!build.compact;
    }
    const char *tools[]={"抓手 V","旋转 R","吸管 K","拆除 X","撤销","重做","网格","昼夜","四季"};
    for(int i=0;i<9;i++) if(button(ui,x,y+row,w,row,i,9,tools[i],(i==0 && build.action==B_MOVE) ||
        (i==2 && build.action==B_COPY) || (i==3 && build.action==B_REMOVE) || (i==6 && build.grid))) {
        if(i==0 || i==2 || i==3) {if(build.mode) {build.action=i==0?B_MOVE:i==2?B_COPY:B_REMOVE;build.moving=-1;}}
        else if(i==1) build_rotate();
        else if(i<6) build_history(i==5);
        else if(i==6) build.grid=!build.grid;
        else if(i==7) build.day=!build.day;
        else build.season=build.season==3?-1:build.season+1;
    }
    char title[220];
    const char *hint=build.has_start?"选择终点":build.action==B_MOVE?(build.moving>=0?"选择新位置":"选择要移动的物件"):
        build.action==B_COPY?"点击物件复制":build.action==B_REMOVE?"点击物件拆除":build.action==B_EXPAND?"点击毗邻陆地":
        build.action==B_RELEASE?"点击要收回的区块":build.action==B_SELECT && build.mode?"选择目录中的物件":build.mode?"点击地面放置":"点击区块";
    const QlcgLot *lot=build_lot();const char *type=lot?qlcg_chunk_type_name(qlcg_get_chunk_type(lot->type)):
        build.cx==build.flag_cx && build.cz==build.flag_cz?"置旗地":"领地";
    snprintf(title,sizeof(title),"%s (%d, %d)  ·  %s",type,build.cx,build.cz,build.message[0]?build.message:hint);
    float titlew=w*(build.mode==BUILD_TERRAIN?.51f:.64f);
    ui_draw_text_ellipsis(ui,UI_FACE_BODY,row*.38f,true,x+5,bottom+row*.2f,titlew-7,0,title,ui->pal.text_main);
    float ctl=x+titlew,space=w-titlew;
    if(build.mode==BUILD_TERRAIN) {
        char radius[32],strength[32];snprintf(radius,sizeof(radius),"半径 %.0fm",build.brush);snprintf(strength,sizeof(strength),"力度 %.2f",build.strength);
        if(button(ui,ctl,bottom,space,row,0,3,radius,false)) build.brush=build.brush>=12?2:build.brush+2;
        if(button(ui,ctl,bottom,space,row,1,3,strength,false)) build.strength=build.strength>=2?.25f:build.strength+.25f;
        if(button(ui,ctl,bottom,space,row,2,3,build.square?"方刷":"圆刷",build.square)) build.square=!build.square;
    } else if(build.mode) {
        if(button(ui,ctl,bottom,space,row,0,2,qlcg_lot_style_name(build.pack),false)) build.styles=!build.styles;
        if(button(ui,ctl,bottom,space,row,1,2,build.foundation?"整平地基":"随坡地基",build.foundation)) build.foundation=!build.foundation;
    } else {
        if(button(ui,ctl,bottom,space,row,0,2,"拉近",false)) game_build_gesture(.8f,0);
        if(button(ui,ctl,bottom,space,row,1,2,"拉远",false)) game_build_gesture(1.25f,0);
    }
    dock(ui,x,w,bottom+row,row);
    if(build.styles && (build.mode==BUILD_HOUSE || build.mode==BUILD_BUY)) {
        int styles=qlcg_lot_style_count(),cols=w>600?4:3,rows=(styles+cols-1)/cols;
        float rh=fminf(row,(bottom-top-4)/rows),start=bottom-rh*rows;
        ui_draw_rect(ui,x,start,w,bottom-start,ui->pal.bg_card);
        for(int i=0;i<styles;i++) if(button(ui,x,start+(i/cols)*rh,w,rh,i%cols,cols,qlcg_lot_style_name(i),i==build.pack)) {
            build.pack=i;build.page=0;build.styles=false;build.has_start=false;
            build.action=B_SELECT;build.moving=-1;build.message[0]=0;
        }
    }
}
