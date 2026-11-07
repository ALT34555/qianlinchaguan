/* 球面气候、两季热输送与粗网格降水 */
#include "gen_climate.h"
#include "../world_infinite.h"
#include <stdlib.h>
#include <math.h>

static const double PI = 3.14159265358979323846;
static double clampd(double v, double a, double b) { return fmax(a, fmin(b, v)); }
static double mix(double a, double b, double t) { return a+(b-a)*t; }
static double smooth(double t) { t=clampd(t,0,1); return t*t*(3-2*t); }
static double gauss(double v, double m, double s) { double z=(v-m)/s; return exp(-z*z); }

static int index_at(int cols, int rows, int x, int y) {
    if(rows==cols) return (y<0?0:y>=rows?rows-1:y)*cols+(x<0?0:x>=cols?cols-1:x);
    y=(y%(rows*2)+rows*2)%(rows*2);
    if (y>=rows) { y=rows*2-y-1; x+=cols/2; }
    return y*cols+(x%cols+cols)%cols;
}

static double read_at(int cols, int rows, const float *v, double x, double y) {
    int ix=(int)floor(x), iy=(int)floor(y);
    double tx=x-ix, ty=y-iy;
    return mix(mix(v[index_at(cols,rows,ix,iy)],v[index_at(cols,rows,ix+1,iy)],tx),
               mix(v[index_at(cols,rows,ix,iy+1)],v[index_at(cols,rows,ix+1,iy+1)],tx),ty);
}

typedef struct {
    float *memory, *temperature, *rainfall, *evaporation, *moisture;
    float *wind_e, *wind_s, *shadow, *heat, *season_amp, *monsoon_amp;
} ClimateField;

/* 黄赤交角对季节振幅的倍率 */
static double tilt_amp_scale(const WorldCreatorOptions *o) {
    double deg = o ? o->axial_tilt_deg : 23.5;
    deg = clampd(deg, 0.0, 60.0);
    if (deg < 1e-6) return 0.0;
    return sin(deg * PI / 180.0) / sin(23.5 * PI / 180.0);
}

static int thermal(int cols, int rows, const WorldCreatorOptions *o, const float *land,
                   const float *peaks, const float *fraction, ClimateField *f) {
    int n=cols*rows;
    float *mem=calloc((size_t)n*10,sizeof(float));
    int *up=malloc((size_t)n*4*sizeof(int));
    double amp_scale=tilt_amp_scale(o);
    if (!mem||!up) { free(mem); free(up); return 0; }
    float *base=mem,*amplitude=base+n,*air=amplitude+n,*next=air+n;
    float *weights=next+n,*passage=weights+n*4;
    for (int y=0;y<rows;y++) {
        double lat=PI/2-(y+.5)*PI/rows;
        for (int x=0;x<cols;x++) {
            int i=y*cols+x;
            base[i]=(float)(o->pole_temp+(o->equator_temp-o->pole_temp)*pow(cos(lat),1.4));
            amplitude[i]=(float)((5+12*(double)fraction[i])*pow(fabs(sin(lat)),.7)*amp_scale);
            if(o->map_mode==WORLD_MAP_INFINITE) {
                base[i]=qlcg_infinite_climate_base(o,x,y,cols,rows);
                amplitude[i]=(float)((4+8*fraction[i])*amp_scale*qlcg_infinite_climate_weight(x,y,cols,rows));
            }
        }
    }
    for (int phase=-1;phase<=1;phase+=2) {
        for (int y=0;y<rows;y++) for (int x=0;x<cols;x++) {
            int i=y*cols+x,k=i*4,hemisphere=y<rows/2?1:-1,winter=phase*hemisphere<0;
            double dx=clampd(f->wind_e[i]*(winter?.3:.8),-1.3,1.3);
            double dy=clampd(f->wind_s[i]*.3+hemisphere*(winter?.95:-.35),-1.3,1.3);
            double sx=x-dx,sy=y-dy,tx=sx-floor(sx),ty=sy-floor(sy);
            int ix=(int)floor(sx),iy=(int)floor(sy);
            up[k]=index_at(cols,rows,ix,iy); up[k+1]=index_at(cols,rows,ix+1,iy);
            up[k+2]=index_at(cols,rows,ix,iy+1); up[k+3]=index_at(cols,rows,ix+1,iy+1);
            weights[k]=(float)((1-tx)*(1-ty)); weights[k+1]=(float)(tx*(1-ty));
            weights[k+2]=(float)((1-tx)*ty); weights[k+3]=(float)(tx*ty);
            double barrier=0;
            for (int d=0;d<4;d++) barrier+=fmax(0,fmax(peaks[i],peaks[up[k+d]])-fmin(land[i],land[up[k+d]]))*(double)weights[k+d];
            passage[i]=(float)exp(-barrier/(winter?650:1300));
            air[i]=base[i]+phase*hemisphere*amplitude[i];
        }
        for (int pass=0;pass<16;pass++) {
            for (int y=0;y<rows;y++) for (int x=0;x<cols;x++) {
                int i=y*cols+x,k=i*4,hemisphere=y<rows/2?1:-1;
                double local=base[i]+(double)phase*hemisphere*amplitude[i],source=0;
                for (int d=0;d<4;d++) source+=(double)air[up[k+d]]*weights[k+d];
                next[i]=(float)(local+(source-local)*(.7*fraction[i]*passage[i]));
            }
            float *tmp=air; air=next; next=tmp;
        }
        for (int i=0;i<n;i++) f->heat[i]=(float)(f->heat[i]+((double)air[i]-base[i])*.5);
    }
    for (int i=0;i<n;i++) f->heat[i]=(float)clampd(f->heat[i],-6,6);
    for (int i=0;i<n;i++) f->season_amp[i]=amplitude[i];
    free(mem); free(up); return 1;
}

static int climate_base(int cols, int rows, const WorldCreatorOptions *o, const float *height,
                        const float *peaks, const float *fraction, ClimateField *f) {
    int n=cols*rows;
    f->memory=calloc((size_t)n*10,sizeof(float));
    float *mem=calloc((size_t)n*14,sizeof(float));
    int *up=malloc((size_t)n*4*sizeof(int));
    if (!f->memory||!mem||!up) { free(f->memory); free(mem); free(up); f->memory=NULL; return 0; }
    f->temperature=f->memory; f->rainfall=f->temperature+n;
    f->evaporation=f->rainfall+n; f->moisture=f->evaporation+n;
    f->wind_e=f->moisture+n; f->wind_s=f->wind_e+n; f->shadow=f->wind_s+n; f->heat=f->shadow+n;
    f->season_amp=f->heat+n; f->monsoon_amp=f->season_amp+n;
    float *land=mem,*barrier=land+n,*grad_e=barrier+n,*grad_s=grad_e+n;
    float *lift=grad_s+n,*lee=lift+n,*vapor=lee+n,*next=vapor+n,*weights=next+n,*frac=weights+4*n;
    for (int y=0;y<rows;y++) for (int x=0;x<cols;x++) {
        int i=y*cols+x; double lat=PI/2-(y+.5)*PI/rows;
        f->temperature[i]=(float)(o->pole_temp+(o->equator_temp-o->pole_temp)*pow(cos(lat),1.4)-fmax(0,height[i])*.0065);
        if(o->map_mode==WORLD_MAP_INFINITE)
            f->temperature[i]=qlcg_infinite_climate_base(o,x,y,cols,rows)-fmaxf(0,height[i])*.0065f*qlcg_infinite_climate_weight(x,y,cols,rows);
        land[i]=fmaxf(0,height[i]);
        barrier[i]=peaks?(float)(land[i]+((double)peaks[i]-land[i])*.65):land[i];
        frac[i]=fraction?fraction[i]:(height[i]>0?1.0f:0.0f);
    }
    for (int y=0;y<rows;y++) for (int x=0;x<cols;x++) {
        int i=y*cols+x;
        grad_e[i]=(float)((read_at(cols,rows,land,x+1,y)-read_at(cols,rows,land,x-1,y))*.5);
        grad_s[i]=(float)((read_at(cols,rows,land,x,y+1)-read_at(cols,rows,land,x,y-1))*.5);
    }
    double mrad=o->monsoon_direction*PI/180;
    double amp_scale=tilt_amp_scale(o);
    double zonal=o->prograde?1.0:-1.0;
    for (int y=0;y<rows;y++) {
        double lat=PI/2-(y+.5)*PI/rows,deg=fabs(lat)*180/PI;
        double east=(-gauss(deg,12,15)+gauss(deg,48,14)-.8*gauss(deg,80,9))*zonal;
        double merid=(lat>0?1:-1)*(.35*gauss(deg,18,15)-.22*gauss(deg,45,15)+.25*gauss(deg,72,12));
        double monsoon=gauss(deg,18,13)*.6;
        for (int x=0;x<cols;x++) {
            int i=y*cols+x;
            double ax=east/fmax(.25,cos(lat))+monsoon*cos(mrad),ay=merid+monsoon*sin(mrad);
            double ahead=0,behind=0;
            for (int k=1;k<=3;k++) {
                ahead=fmax(ahead,read_at(cols,rows,barrier,x+ax*k,y+ay*k)-land[i]);
                behind=fmax(behind,read_at(cols,rows,barrier,x-ax*k,y-ay*k)-land[i]);
            }
            double blocked=smooth((ahead-150)/650),shadow=smooth(behind/650);
            double tx=-grad_s[i],ty=grad_e[i],tl=hypot(tx,ty),speed=hypot(ax,ay);
            if (!tl) tl=1; if (!speed) speed=1;
            double sign=ax*tx+ay*ty>=0?1:-1;
            f->wind_e[i]=(float)(ax*(1-.6*blocked)+tx/tl*sign*.7*blocked*speed);
            f->wind_s[i]=(float)(ay*(1-.6*blocked)+ty/tl*sign*.7*blocked*speed);
            double land_frac=fraction?fraction[i]:(height[i]>0?1.0:0.0);
            f->monsoon_amp[i]=(float)clampd(monsoon*(.45+.35*blocked+.20*land_frac)*
                                            (1-.35*shadow)*amp_scale,0,1);
            double v=ax*grad_e[i]+ay*grad_s[i];
            lift[i]=(float)(fmax(0,v)*(1+.8*blocked)/1700);
            lee[i]=(float)(fmax(0,-v)*(.5+1.2*shadow)/1700); f->shadow[i]=(float)shadow;
        }
    }
    if (!thermal(cols,rows,o,land,peaks?peaks:land,frac,f)) { free(mem); free(up); free(f->memory); f->memory=NULL; return 0; }
    for (int i=0;i<n;i++) {
        double weight=o->map_mode==WORLD_MAP_INFINITE?qlcg_infinite_climate_weight(i%cols,i/cols,cols,rows):1;
        f->temperature[i]=(float)((double)f->temperature[i]+f->heat[i]*weight);
        f->evaporation[i]=(float)clampd(650+(f->temperature[i]-5.0)*28,80,1900);
    }
    for (int y=0;y<rows;y++) for (int x=0;x<cols;x++) {
        int i=y*cols+x,k=i*4;
        double sx=x-(double)f->wind_e[i],sy=y-(double)f->wind_s[i],tx=sx-floor(sx),ty=sy-floor(sy);
        int ix=(int)floor(sx),iy=(int)floor(sy);
        up[k]=index_at(cols,rows,ix,iy); up[k+1]=index_at(cols,rows,ix+1,iy);
        up[k+2]=index_at(cols,rows,ix,iy+1); up[k+3]=index_at(cols,rows,ix+1,iy+1);
        weights[k]=(float)((1-tx)*(1-ty)); weights[k+1]=(float)(tx*(1-ty));
        weights[k+2]=(float)((1-tx)*ty); weights[k+3]=(float)(tx*ty);
        vapor[i]=height[i]<=0?1.0f:0.0f;
    }
    int passes=(int)clampd(cols/4,24,96); double decay=exp(-.005*128/cols);
    for (int pass=0;pass<passes;pass++) {
        for (int y=0;y<rows;y++) {
            double deg=fabs(PI/2-(y+.5)*PI/rows)*180/PI;
            double base=.05*128/cols*(.5+.65*gauss(deg,0,16)+.4*gauss(deg,50,13)-.3*gauss(deg,28,9));
            for (int x=0;x<cols;x++) {
                int i=y*cols+x,k=i*4;
                double source=0; for (int d=0;d<4;d++) source+=(double)vapor[up[k+d]]*weights[k+d];
                double rate=source*clampd(base+(double)lift[i]-lee[i],0,.75);
                f->rainfall[i]=(float)(f->rainfall[i]+rate);
                double recycled=fmax(0,source-rate)*decay+.03*clampd(f->evaporation[i]/1200.0,0,1)*clampd(source*.8+.2,.2,1);
                next[i]=(float)(height[i]<=0?.65+.35*clampd((f->temperature[i]+15.0)/45,0,1):clampd(recycled-.06*f->shadow[i],0,1));
            }
        }
        float *tmp=vapor; vapor=next; next=tmp;
    }
    for (int i=0;i<n;i++) {
        double deg=fabs(PI/2-(i/cols+.5)*PI/rows)*180/PI;
        double conv=.08*clampd(f->evaporation[i]/1200.0,0,1)*clampd(vapor[i]*1.5,.35,1)*(.35+.8*gauss(deg,22,22));
        f->rainfall[i]=(float)((f->rainfall[i]/(double)passes+conv)*18000*(cols/128.0)*o->precipitation);
    }
    free(mem); free(up); return 1;
}

void qlcg_gen_climate(MacroWorld *w) {
    if (!w) return;
    int cols=w->columns,rows=w->rows,n=w->cell_count,c=cols>128?128:cols;
    ClimateField f={0};
    float *mem=NULL,*coarse=NULL,*peaks=NULL,*fraction=NULL;
    if (cols>128) {
        int cn=c*c/2;
        mem=calloc((size_t)cn*3+rows,sizeof(float));
        if (!mem) return;
        coarse=mem; peaks=coarse+cn; fraction=peaks+cn; float *row_cos=fraction+cn;
        for (int y=0;y<rows;y++) row_cos[y]=(float)cos(PI/2-(y+.5)*PI/rows);
        for (int y=0;y<c/2;y++) for (int x=0;x<c;x++) {
            double sum=0,weight=0,land_weight=0,peak=0;
            for (int sy=y*rows/(c/2);sy<(y+1)*rows/(c/2);sy++) {
                double a=row_cos[sy];
                for (int sx=x*cols/c;sx<(x+1)*cols/c;sx++) {
                    double v=w->height[sy*cols+sx]; sum+=v*a; weight+=a;
                    if (v>0) land_weight+=a; peak=fmax(peak,v);
                }
            }
            int i=y*c+x; coarse[i]=(float)(sum/weight); peaks[i]=(float)peak; fraction[i]=(float)(land_weight/weight);
        }
    }
    if (!climate_base(c,qlcg_world_flat(w)?c:c/2,&w->opt,coarse?coarse:w->height,peaks,fraction,&f)) { free(mem); return; }
    for (int y=0;y<rows;y++) {
        double lat=PI/2-(y+.5)*PI/rows;
        double base=w->opt.pole_temp+(w->opt.equator_temp-w->opt.pole_temp)*pow(cos(lat),1.4);
        for (int x=0;x<cols;x++) {
            int i=y*cols+x;
            if (cols<=128) {
                w->rainfall[i]=f.rainfall[i]; w->temperature[i]=f.temperature[i]; w->evaporation[i]=f.evaporation[i];
            } else {
                double scale=cols/(double)c,sx=(x+.5)/scale-.5,sy=(y+.5)/scale-.5;
                int near=index_at(c,c/2,(int)floor(sx+.5),(int)floor(sy+.5));
                double we=f.wind_e[near],ws=f.wind_s[near],speed=hypot(we,ws); if (!speed) speed=1;
                double east=fmax(0,w->height[index_at(cols,rows,x+2,y)])-fmax(0,w->height[index_at(cols,rows,x-2,y)]);
                double south=fmax(0,w->height[index_at(cols,rows,x,y+2)])-fmax(0,w->height[index_at(cols,rows,x,y-2)]);
                double slope=(we*east+ws*south)*.25/speed;
                double fine=clampd(1+slope/220*(slope>0?1.25:1.15*(.5+.5*f.shadow[near])),.2,3);
                w->rainfall[i]=(float)(read_at(c,c/2,f.rainfall,sx,sy)*fine);
                w->temperature[i]=(float)(base-fmax(0,w->height[i])*.0065+read_at(c,c/2,f.heat,sx,sy));
                w->evaporation[i]=(float)clampd(650+(w->temperature[i]-5.0)*28,80,1900);
            }
        }
    }
    double total=0,area=0;
    for (int i=0;i<n;i++) { total+=(double)w->rainfall[i]*w->areas[i]; area+=w->areas[i]; }
    double mean=total/area,factor=mean>0?985.5*w->opt.precipitation/mean:0;
    for (int i=0;i<n;i++) {
        w->rainfall[i]=(float)(w->rainfall[i]*factor);
        w->moisture[i]=(float)(clampd(w->rainfall[i]/fmax(150,w->evaporation[i]),0,2)/2);
    }
    w->season_amp=(float*)calloc((size_t)n,sizeof(float));
    w->monsoon_amp=(float*)calloc((size_t)n,sizeof(float));
    if (w->season_amp) {
        for (int y=0;y<rows;y++) {
            for (int x=0;x<cols;x++) {
                int i=y*cols+x;
                if (cols<=128) w->season_amp[i]=f.season_amp[i];
                else {
                    double scale=cols/(double)c,sx=(x+.5)/scale-.5,sy=(y+.5)/scale-.5;
                    w->season_amp[i]=(float)read_at(c,c/2,f.season_amp,sx,sy);
                }
            }
        }
    }
    if (w->monsoon_amp) {
        for (int y=0;y<rows;y++) {
            for (int x=0;x<cols;x++) {
                int i=y*cols+x;
                if (cols<=128) w->monsoon_amp[i]=f.monsoon_amp[i];
                else {
                    double scale=cols/(double)c,sx=(x+.5)/scale-.5,sy=(y+.5)/scale-.5;
                    w->monsoon_amp[i]=(float)read_at(c,c/2,f.monsoon_amp,sx,sy);
                }
            }
        }
    }
    free(f.memory); free(mem);
}
