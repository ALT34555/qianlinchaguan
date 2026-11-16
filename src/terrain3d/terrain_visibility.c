#include "terrain_visibility.h"
#include "terrain_metrics.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
void qlcg_frustum_set(TerrainFrustum *f,const float m[16]) {
    for(int k=0;k<6;k++) {
        int a=k/2;float sign=(k&1)?-1:1;
        float length=0;
        for(int j=0;j<4;j++) { f->plane[k][j]=m[j*4+3]+sign*m[j*4+a];if(j<3)length+=f->plane[k][j]*f->plane[k][j]; }
        length=sqrtf(length);if(length>0)for(int j=0;j<4;j++)f->plane[k][j]/=length;
    }
}
bool qlcg_frustum_bounds(const TerrainFrustum *f,const TerrainBounds *b) {
    for(int k=0;k<6;k++) {
        const float *n=f->plane[k];float d=n[3];
        for(int j=0;j<3;j++)d+=n[j]*(n[j]>=0?b->max[j]:b->min[j]);
        if(d<0)return false;
    }
    return true;
}
bool qlcg_frustum_sphere(const TerrainFrustum *f,float x,float y,float z,float r) {
    for(int k=0;k<6;k++){const float *n=f->plane[k];if(n[0]*x+n[1]*y+n[2]*z+n[3]<-r)return false;}
    return true;
}
void qlcg_instance_view_free(TerrainInstanceView *view) {
    qlcg_gl_delete_vertex_arrays(3,view->vao);
    qlcg_gl_delete_buffers(3,view->buffer);
    memset(view,0,sizeof(*view));
}
void qlcg_batch_cpu_free(PlantBatch *b) {
    if(b->views)for(int i=0;i<QL_VIEW_COUNT;i++)qlcg_instance_view_free(b->views+i);
    free(b->views);b->views=NULL;
    free(b->all_instances);free(b->visible_instances);
    b->all_instances=b->visible_instances=NULL;b->cpu_capacity=0;
}
static bool view_hit(TerrainInstanceView *view,uint64_t version,const float m[16]) {
    return view->valid&&view->version==version&&!memcmp(view->matrix,m,16*sizeof(float));
}
static void view_commit(TerrainInstanceView *view,uint64_t version,const float m[16],int count) {
    view->valid=true;view->version=version;view->count=count;
    memcpy(view->matrix,m,16*sizeof(float));
}
int qlcg_batch_visible(PlantBatch *b,const float m[16],const TerrainFrustum *frustum,int view_id) {
    if(!b->all_instances||!b->inst_count)return 0;
    if(view_id<0||view_id>=QL_VIEW_COUNT)return 0;
    if(!b->views)b->views=calloc(QL_VIEW_COUNT,sizeof(*b->views));
    if(!b->views){
        glBindVertexArray(b->vao);glBindBuffer(GL_ARRAY_BUFFER,b->inst_vbo);
        glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)((size_t)b->inst_count*sizeof(PlantInstGPU)),b->all_instances,GL_STREAM_DRAW);
        return b->inst_count;
    }
    TerrainInstanceView *view=b->views+view_id;
    if(view_hit(view,b->content_version,m)) {
        qlcg_metrics.culled_instances+=b->inst_count-view->count;
        if(view->count)glBindVertexArray(view->vao[view->cursor]);
        return view->count;
    }
    const PlantInstGPU *all=b->all_instances;PlantInstGPU *out=b->visible_instances;
    int count=0;
    float mid_y=(b->low_y+b->high_y)*.5f;
    float unit_r=hypotf(b->radius,(b->high_y-b->low_y)*.5f);
    float pad_r=b->wind_pad+1.0f;
    for(int i=0;i<b->inst_count;i++) {
        const PlantInstGPU *v=all+i;
        if(qlcg_frustum_sphere(frustum,v->x,v->y+mid_y*v->scale,v->z,unit_r*v->scale+pad_r))out[count++]=*v;
    }
    qlcg_metrics.culled_instances+=b->inst_count-count;
    if(count){
        if(!view->vao[0]) {
            glGenVertexArrays(3,view->vao);glGenBuffers(3,view->buffer);
            for(int i=0;i<3;i++)qlcg_batch_vao(b,view->vao[i],view->buffer[i]);
            view->cursor=2;
        }
        view->cursor=(view->cursor+1)%3;
        glBindVertexArray(view->vao[view->cursor]);glBindBuffer(GL_ARRAY_BUFFER,view->buffer[view->cursor]);
        if(view->capacity[view->cursor] < b->cpu_capacity) {
            glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)((size_t)b->cpu_capacity*sizeof(*out)),NULL,GL_STREAM_DRAW);
            view->capacity[view->cursor]=b->cpu_capacity;
        }
        glBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)((size_t)count*sizeof(*out)),out);
    }
    view_commit(view,b->content_version,m,count);return count;
}
void qlcg_chunk_bounds(TerrainChunkGPU *g,const RegionData *c,double ox,double oz) {
    TerrainBounds *b=&g->bounds;
    b->min[0]=(float)ox-24;b->max[0]=(float)ox+24;
    b->min[2]=(float)oz-24;b->max[2]=(float)oz+24;
    b->min[1]=1e30f;b->max[1]=-1e30f;
    for(int i=0;i<c->width*c->height;i++) {
        b->min[1]=fminf(b->min[1],c->heights[i]-8);
        b->max[1]=fmaxf(b->max[1],c->heights[i]+8);
        if(c->water_levels&&c->water_levels[i]>-1e8f)b->max[1]=fmaxf(b->max[1],c->water_levels[i]+4);
    }
}

int qlcg_far_visible(TerrainRenderer *tr,int kind,const float m[16],const TerrainFrustum *frustum,int view_id) {
    int total=kind?tr->far_bill_count:tr->far_cross_count;
    const FarPropInst *input=tr->far_source[kind];FarPropInst *output=tr->far_visible[kind];
    if(!input||!output)return 0;
    if(view_id<0||view_id>=QL_VIEW_COUNT)return 0;
    TerrainInstanceView *view=&tr->far_views[kind][view_id];
    if(view_hit(view,tr->far_version[kind],m)) {
        qlcg_metrics.culled_instances+=total-view->count;
        if(view->count)glBindVertexArray(view->vao[view->cursor]);
        return view->count;
    }
    int n=0;
    for(int i=0;i<total;i++) {
        const FarPropInst *v=input+i;float h=v->h*.5f;
        if(qlcg_frustum_sphere(frustum,v->x,v->y+h,v->z,hypotf(v->w,h)+2))output[n++]=*v;
    }
    qlcg_metrics.culled_instances+=total-n;
    if(n){
        if(!view->vao[0]) {
            glGenVertexArrays(3,view->vao);glGenBuffers(3,view->buffer);
            for(int i=0;i<3;i++)qlcg_far_vao(view->vao[i],kind?tr->far_bill_vbo:tr->far_cross_vbo,
                kind?tr->far_bill_ebo:tr->far_cross_ebo,view->buffer[i]);
            view->cursor=2;
        }
        view->cursor=(view->cursor+1)%3;
        glBindVertexArray(view->vao[view->cursor]);glBindBuffer(GL_ARRAY_BUFFER,view->buffer[view->cursor]);
        if(view->capacity[view->cursor] < tr->far_cpu_cap[kind]) {
            glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)((size_t)tr->far_cpu_cap[kind]*sizeof(*output)),NULL,GL_STREAM_DRAW);
            view->capacity[view->cursor]=tr->far_cpu_cap[kind];
        }
        glBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)((size_t)n*sizeof(*output)),output);}
    view_commit(view,tr->far_version[kind],m,n);
    return n;
}
