/* 主河定稿与纵剖面编码 */
#include "hydrology_river_internal.h"
#include "hydrology_fields.h"
#include <stdlib.h>

enum { RIVER_TAG_WATERFALL=0x57463131, RIVER_TAG_GROUND=0x47503131,
       RIVER_TAG_RED1=0x31444552, RIVER_TAG_RED2=0x32444552 };

static int profile_record(RiverProfile *p,RiverBuffer *b) {
    int32_t count=0,*indices=NULL; float *values=NULL;
    if(!b->reading) {
        indices=malloc((size_t)p->count*sizeof(*indices)); values=malloc((size_t)p->count*sizeof(*values));
        if(!indices||!values) { free(indices); free(values); return 0; }
        int start=0; double lower=-INFINITY,upper=INFINITY;
        indices[count]=0; values[count++]=p->levels[0];
        for(int i=1;i<p->count;i++) {
            double slope=((double)p->levels[i]-p->levels[start])/(i-start);
            if(i>start+1&&(slope<lower||slope>upper)) {
                start=i-1; indices[count]=start; values[count++]=p->levels[start]; lower=-INFINITY; upper=INFINITY;
            }
            lower=fmax(lower,((double)p->levels[i]-p->levels[start]-.0005)/(i-start));
            upper=fmin(upper,((double)p->levels[i]-p->levels[start]+.0005)/(i-start));
        }
        indices[count]=p->count-1; values[count++]=p->levels[p->count-1];
        if(count*2>=p->count) count=0;
        for(int k=1;k<count;k++) for(int j=indices[k-1];j<=indices[k];j++) {
            float level=(float)qlcg_mixd(values[k-1],values[k],(j-indices[k-1])/(double)(indices[k]-indices[k-1]));
            if(fabs((double)level-p->levels[j])>.0005) { count=0; break; }
        }
    }
    qlcg_river_values(b,&count,(sizeof(count))/4,4);
    if(b->failed||count<0||count>p->count||count==1) { free(indices); free(values); return 0; }
    if(!count) qlcg_river_values(b,p->levels,((size_t)p->count*sizeof(float))/4,4);
    else {
        if(b->reading) { indices=malloc((size_t)count*sizeof(*indices)); values=malloc((size_t)count*sizeof(*values)); }
        if(!indices||!values) { free(indices); free(values); return 0; }
        qlcg_river_values(b,indices,((size_t)count*sizeof(*indices))/4,4); qlcg_river_values(b,values,((size_t)count*sizeof(*values))/4,4);
        if(b->failed||indices[0]!=0||indices[count-1]!=p->count-1) { free(indices); free(values); return 0; }
        for(int k=1;k<count;k++) {
            if(indices[k]<=indices[k-1]||indices[k]>=p->count||!isfinite(values[k])||!isfinite(values[k-1])) { free(indices); free(values); return 0; }
            for(int j=indices[k-1];j<=indices[k];j++)
                p->levels[j]=(float)qlcg_mixd(values[k-1],values[k],(j-indices[k-1])/(double)(indices[k]-indices[k-1]));
        }
    }
    free(indices); free(values); return !b->failed;
}

int qlcg_river_curve_record(const MacroWorld *w,RiverCurve *c,RiverBuffer *b) {
    int32_t meta[3]={c->count,c->receiver,c->profile.count};
    qlcg_river_values(b,meta,(sizeof(meta))/4,4);
    if(b->failed||meta[0]<2||meta[0]>RIVER_CURVE_POINTS||meta[1]<0||meta[1]>=w->cell_count||meta[2]<0||meta[2]==1||meta[2]>1000000) return 0;
    c->count=meta[0]; c->receiver=meta[1];
    qlcg_river_values(b,&c->depth_a,(sizeof(double))/8,8); qlcg_river_values(b,&c->depth_b,(sizeof(double))/8,8);
    qlcg_river_RiverMouthPoint(b,&c->inlet); qlcg_river_RiverMouthPoint(b,&c->outlet);
    for(int k=0;k<c->count;k++) {
        CurvePoint *p=c->points+k;
        double v[5]={p->wx,p->wz,p->x,p->z,p->width}; qlcg_river_values(b,v,(sizeof(v))/8,8);
        if(b->reading) {
            for(int j=0;j<5;j++) if(!isfinite(v[j])) return 0;
            if(v[4]<=0||v[4]>3000) return 0;
            *p=(CurvePoint){v[0],v[1],v[2],v[3],0,v[4],0};
        }
    }
    c->profile.count=meta[2];
    if(meta[2]) {
        if(b->reading) { c->profile.levels=malloc((size_t)meta[2]*sizeof(float)); if(!c->profile.levels) return 0; }
        qlcg_river_values(b,&c->profile.arc,(sizeof(double))/8,8);
        if(!profile_record(&c->profile,b)) return 0;
        if(b->reading) for(int k=0;k<meta[2];k++) if(!isfinite(c->profile.levels[k])) return 0;
        c->profile_ready=1;
    }
    if(!b->reading || b->at<b->size) {
        int32_t tag=RIVER_TAG_WATERFALL,state=c->waterfall.state,style=c->waterfall.style;
        double v[5]={c->waterfall.begin,c->waterfall.end,c->waterfall.top,c->waterfall.bottom,c->waterfall.width};
        qlcg_river_values(b,&tag,(sizeof(tag))/4,4); qlcg_river_values(b,&state,(sizeof(state))/4,4);
        qlcg_river_values(b,&style,(sizeof(style))/4,4); qlcg_river_values(b,v,(sizeof(v))/8,8);
        if(tag!=RIVER_TAG_WATERFALL || state<0 || state>2 || style<0 || style>7) return 0;
        for(int j=0;j<5;j++) if(!isfinite(v[j])) return 0;
        if(state==2 && (v[0]<0 || v[1]<=v[0] || v[1]>c->profile.arc || v[2]<=v[3] || v[4]<=0)) return 0;
        c->waterfall=(MacroWaterfall){state,style,v[0],v[1],v[2],v[3],v[4]};
    }
    if((!b->reading && c->ground_profile.count) || (b->reading && b->at<b->size)) {
        int32_t tag=RIVER_TAG_GROUND;
        qlcg_river_values(b,&tag,(sizeof(tag))/4,4);
        if(tag!=RIVER_TAG_GROUND) {
            if(!b->reading||(tag!=RIVER_TAG_RED1&&tag!=RIVER_TAG_RED2)) return 0;
            b->at-=sizeof(tag);
        } else {
        if(!c->profile.count) return 0;
        if(b->reading) {
            c->ground_profile.count=c->profile.count;
            c->ground_profile.arc=c->profile.arc;
            c->ground_profile.levels=malloc((size_t)c->profile.count*sizeof(float));
            if(!c->ground_profile.levels) return 0;
        }
        if(!profile_record(&c->ground_profile,b)) return 0;
        for(int k=0;k<c->ground_profile.count;k++) if(!isfinite(c->ground_profile.levels[k])) return 0;
        }
    }
    if((!b->reading&&c->edit_mode)||(b->reading&&b->at<b->size)) {
        int32_t tag=c->edit_mode==RIVER_CURVE_LOCAL_EDIT?RIVER_TAG_RED2:RIVER_TAG_RED1;
        qlcg_river_values(b,&tag,(sizeof(tag))/4,4);
        if(tag==RIVER_TAG_RED1) {
            qlcg_river_values(b,&c->edit_mode,(sizeof(int))/4,4); if(c->edit_mode!=RIVER_CURVE_LEGACY) return 0;
        } else if(tag==RIVER_TAG_RED2) {
            c->edit_mode=RIVER_CURVE_LOCAL_EDIT;
            qlcg_river_values(b,c->parameters,((size_t)c->count*sizeof(double))/8,8);
            qlcg_river_values(b,c->saved_curvature,((size_t)c->count*sizeof(double))/8,8);
            qlcg_river_io(b,c->edited_segments,(size_t)c->count);
            for(int k=0;k<c->count;k++) if(!isfinite(c->parameters[k])||!isfinite(c->saved_curvature[k])||c->edited_segments[k]>1||
                c->parameters[k]<0||c->parameters[k]>c->profile.arc+.01||(k&&c->parameters[k]<=c->parameters[k-1])) return 0;
        } else return 0;
    }
    return !b->failed;
}

int qlcg_river_curve_store(const MacroWorld *w,RiverCurve *c) {
    RiverBuffer b={0};
    int ok=qlcg_river_curve_record(w,c,&b)&&qlcg_river_archive_put((MacroWorld *)w,RIVER_RECORD_MAIN,c->cell,&b);
    qlcg_river_buffer_free(&b); return ok;
}

int qlcg_river_main_natural(const MacroWorld *w,RiverBuffer *record) {
    RiverCurve *c=calloc(1,sizeof(*c));if(!c) return -1;
    int ok=qlcg_river_curve_record(w,c,record)&&record->at==record->size;
    int result=!ok?-1:c->edit_mode?-2:1;qlcg_river_curve_free(c);return result;
}
