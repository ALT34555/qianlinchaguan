#include "plant_def.h"

/* 空心枯木沿 X 轴分布，子实体锚在树皮 */
static void arch_deadwood(RockBuilder *b, const RockBuildDef *def) {
    enum { SIDES=9, RINGS=4 };
    double shell[RINGS][SIDES][3], lip[SIDES][3], inner[SIDES][3];
    double radius=pnum(def,"radius",.34), length=pnum(def,"length",2.4);
    int moss=(int)pnum(def,"moss",0), fork=(int)pnum(def,"fork",0);
    double bend=pnum(def,"bend",.1), hollow=pnum(def,"hollow",.52);
    double sink=def->form==ROCK_FORM_BURIED ? .22*radius : 0;
    LpRgb bark=qlcg_lp_rgb(102,74,52), exposed=qlcg_lp_rgb(149,110,72);
    LpRgb heart=qlcg_lp_rgb(68,49,39), cavity=qlcg_lp_rgb(43,34,30);
    for (int r=0;r<RINGS;r++) for (int j=0;j<SIDES;j++) {
        double a=(j+.21)*TAU/SIDES, t=(double)r/(RINGS-1);
        double rad=radius*(1.08-.24*t)*(1+.09*sin(j*9.3+def->seed));
        shell[r][j][0]=(t-.5)*length + (r==0 || r==RINGS-1 ? radius*.43*sin(j*3.8+r) : 0);
        shell[r][j][1]=radius*.87-sink+rad*sin(a)+radius*.1*t;
        shell[r][j][2]=bend*sin(t*PI)+rad*cos(a);
    }
    for(int r=0;r<RINGS-1;r++)for(int j=0;j<SIDES;j++){
        int next=(j+1)%SIDES;
        int green=moss && j>=1 && j<=3 && (r+j)%3!=0;
        LpRgb col=green ? (def->season==PD_SEASON_WINTER ? qlcg_lp_rgb(152,156,133) : qlcg_lp_rgb(82,99,53))
                        : ((j+r*2)%6==0 ? exposed : bark);
        const double *p=shell[r][j], *q=shell[r+1][j], *s=shell[r+1][next], *v=shell[r][next];
        qlcg_rock_quad(b,p,q,s,v,col,green?ROCK_MAT_MOSS:ROCK_MAT_SOLID);
        if ((j==0 || j==3) && r!=1) {
            LpSocket *site=&b->fungus[b->fungus_count++];
            site->kind=LP_SOCKET_FUNGUS;
            for(int k=0;k<3;k++)site->translation[k]=(p[k]+q[k]+s[k])/3;
        }
    }
    for(int end=0;end<2;end++){
        int r=end?RINGS-1:0;double cy=radius*(.87+.1*end)-sink;
        for(int j=0;j<SIDES;j++){
            const double *s=shell[r][j];
            lip[j][0]=s[0];lip[j][1]=cy+(s[1]-cy)*hollow;lip[j][2]=s[2]*hollow;
            inner[j][0]=lip[j][0]+(end?-1:1)*radius*.85;
            inner[j][1]=cy+(lip[j][1]-cy)*.78;inner[j][2]=lip[j][2]*.78;
        }
        for(int j=0;j<SIDES;j++){
            int a=end?(j+1)%SIDES:j, c=end?j:(j+1)%SIDES;
            qlcg_rock_quad(b,shell[r][a],shell[r][c],lip[c],lip[a],exposed,ROCK_MAT_SOLID);
            qlcg_rock_quad(b,lip[a],lip[c],inner[c],inner[a],heart,ROCK_MAT_SOLID);
        }
        for(int j=1;j<SIDES-1;j++)
            qlcg_rock_tri(b,inner[0],inner[end?j+1:j],inner[end?j:j+1],cavity,ROCK_MAT_SOLID);
    }
    if(fork){
        RockColumnOpts stub={.base={length*.16,radius*1.25-sink,0},.radius=radius*.46,
              .height=radius*1.55,.segments=2,.tip_ratio=.55,.sides=6,.color=bark,
              .tip_color={149,110,72},.skew=.45,.mat=ROCK_MAT_SOLID};
        qlcg_rock_column(b,&stub);
    }
}
