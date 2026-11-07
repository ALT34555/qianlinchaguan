/* 贴地纵剖面 */
#include "river_profile.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { double arc, height, lake; } Station;
typedef struct {
    Station *items;
    int count, capacity, samples, failed;
    RiverGroundAt sample;
    void *context;
} ProfileBuilder;

static double clampd(double x, double a, double b) { return fmax(a,fmin(b,x)); }
static RiverGround ground(ProfileBuilder *b, double arc) {
    b->samples++;
    RiverGround g=b->sample(b->context,arc);
    if (!isfinite(g.height)) g=(RiverGround){0,NAN,0};
    return g;
}
static void push(ProfileBuilder *b, double arc, RiverGround g) {
    if (b->failed) return;
    if (b->count==b->capacity) {
        int cap=b->capacity?b->capacity*2:512;
        Station *items=realloc(b->items,(size_t)cap*sizeof(*items));
        if (!items) { b->failed=1; return; }
        b->items=items; b->capacity=cap;
    }
    b->items[b->count++]=(Station){arc,g.height,isfinite(g.lake)?g.lake:NAN};
}
static void refine(ProfileBuilder *b, double a, RiverGround ga, double c, RiverGround gc) {
    if (b->failed || c-a<=16) return;
    double mid=(a+c)*.5; RiverGround gm=ground(b,mid);
    if (fabs(gm.height-(ga.height+gc.height)*.5)<=2.2 && fabs(gc.height-ga.height)<=8) return;
    refine(b,a,ga,mid,gm); push(b,mid,gm); refine(b,mid,gm,c,gc);
}

int qlcg_river_profile_build(RiverProfile *out, double arc, RiverGroundAt sample, void *context) {
    memset(out,0,sizeof(*out));
    out->count=(int)fmax(2,ceil(arc/8)+1); out->arc=arc;
    out->levels=malloc((size_t)out->count*sizeof(float));
    if (!out->levels) return 0;
    for (int i=0;i<out->count;i++) out->levels[i]=NAN;
    if (!(arc>0)) return 1;
    ProfileBuilder b={0}; b.sample=sample; b.context=context;
    int coarse=(int)fmax(2,fmin(256,ceil(arc/256)));
    double at=0; RiverGround g=ground(&b,0); push(&b,0,g);
    for (int i=1;i<=coarse;i++) {
        double next=arc*i/coarse; RiverGround gn=ground(&b,next);
        refine(&b,at,g,next,gn); push(&b,next,gn); at=next; g=gn;
    }
    double *s=malloc((size_t)b.count*sizeof(double));
    if (b.failed || !s) { free(s); free(b.items); qlcg_river_profile_free(out); return 0; }
    for (int k=0;k<b.count;k++) {
        double v=b.items[k].height;
        if (k && k<b.count-1 && fabs(b.items[k+1].height-b.items[k-1].height)<=4.4)
            v=(b.items[k-1].height+2*v+b.items[k+1].height)*.25;
        s[k]=v;
    }
    double end=s[b.count-1];
    for (int k=1;k<b.count-1;k++) {
        double prev=s[k-1],rem=fmax(1,arc-b.items[k-1].arc),step=b.items[k].arc-b.items[k-1].arc;
        double ceiling=prev+fmax(0,end-prev)*(step/rem);
        if (s[k]>ceiling) s[k]=fmax(s[k]-14,ceiling);
    }
    for (int k=1;k<b.count-1;k++) if (fabs(s[k+1]-s[k-1])<=4.4) s[k]=(s[k-1]+2*s[k]+s[k+1])*.25;
    int k=0;
    for (int i=0;i<out->count;i++) {
        double target=i*8.0;
        while (k+1<b.count && b.items[k+1].arc<target) k++;
        int next=k+1<b.count?k+1:k;
        double span=b.items[next].arc-b.items[k].arc,t=span>0?clampd((target-b.items[k].arc)/span,0,1):0;
        double height=s[k]+(s[next]-s[k])*t;
        double lake=fmax(isfinite(b.items[k].lake)?b.items[k].lake:-INFINITY,
                         isfinite(b.items[next].lake)?b.items[next].lake:-INFINITY);
        out->levels[i]=(float)(isfinite(lake)?fmax(height,lake):height);
    }
    out->stations=b.count; out->samples=b.samples;
    free(s); free(b.items); return 1;
}

double qlcg_river_profile_level(const RiverProfile *p, double arc) {
    if (!p || !p->count) return NAN;
    double at=clampd(arc/8,0,p->count-1);
    int i=(int)fmin(p->count-2,floor(at));
    return p->levels[i]+((double)p->levels[i+1]-p->levels[i])*(at-i);
}
void qlcg_river_profile_free(RiverProfile *p) { free(p->levels); memset(p,0,sizeof(*p)); }
