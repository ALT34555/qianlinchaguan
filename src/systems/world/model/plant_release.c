#include "plant_forms.h"
#include "plant_def.h"
#include "../../../platform/platform.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const species_ids[] = {
#include "plant_release_ids.inc"
};

static int compare_id(const void *id, const void *element) {
    return strcmp(id, *(const char *const *)element);
}
int qlcg_plant_has_release(const char *id) {
    return id && bsearch(id, species_ids, sizeof(species_ids)/sizeof(*species_ids),
                         sizeof(*species_ids), compare_id) != NULL;
}

static int decode_record(FILE *file, LpGeometry *g) {
    uint16_t h[8];
    if (fread(h,sizeof(*h),8,file)!=8) return 0;
    const int points=h[0], faces=h[1], sockets=h[2], verts=faces*3;
    if (!points || points>2400 || !faces || faces>800 || sockets>LP_SOCKET_CAPACITY ||
        h[3]>1 || h[4]+h[5]+h[6]+h[7]!=faces) return 0;
    size_t bytes=(size_t)points*12+(size_t)faces*22+(size_t)sockets*25;
    unsigned char *data=malloc(bytes);
    if (!data) return 0;
    if (fread(data,1,bytes,file)!=bytes) {free(data);return 0;}
    g->positions=malloc(verts*3*sizeof(float)); g->normals=malloc(verts*3*sizeof(float));
    g->colors=malloc(verts*3); g->wind_parts=malloc(verts); g->indices=malloc(verts*sizeof(uint32_t));
    if (!g->positions || !g->normals || !g->colors || !g->wind_parts || !g->indices) goto invalid;
    const unsigned char *p=data+points*12;
    double max_radius2=0;
    for (int f=0;f<faces;f++,p+=22) {
        for (int c=0;c<3;c++) {
            uint16_t index;memcpy(&index,p+2*c,2);
            if (index>=points) goto invalid;
            int v=f*3+c;float *xyz=g->positions+3*v;
            memcpy(xyz,data+12*index,12);memcpy(g->normals+v*3,p+6,12);
            memcpy(g->colors+v*3,p+18,3);g->wind_parts[v]=p[21];g->indices[v]=v;
            double r2=(double)xyz[0]*xyz[0]+(double)xyz[2]*xyz[2];
            max_radius2=fmax(max_radius2,r2);
            g->height=fmax(g->height,xyz[1]);g->min_y=fmin(g->min_y,xyz[1]);
        }
    }
    int start=0;
    for (int mat=0;mat<4;mat++) if(h[4+mat]) {
        int n=h[4+mat]*3;
        g->groups[g->group_count++]=(LpGroup){mat,start,n};start+=n;
    }
    for (int i=0;i<sockets;i++,p+=25) {
        if (p[0]>=LP_SOCKET_KIND_COUNT) goto invalid;
        g->sockets[i].kind=p[0];memcpy(g->sockets[i].translation,p+1,24);
    }
    g->socket_count=sockets;g->radius=sqrt(max_radius2);
    if (h[3]) g->height-=g->min_y;
    g->vertex_count=g->index_count=verts;g->triangle_count=faces;
    free(data);return 1;
invalid:
    free(data);qlcg_lp_geometry_free(g);return 0;
}

/* 正数发布网格，零用合规源，负数无效 */
int qlcg_plant_read_release(const char *id, int season, int form, LpGeometry *g) {
    memset(g,0,sizeof(*g));
    if (!id || season<PD_SEASON_NONE || season>=PD_SEASON_COUNT) return -1;
    size_t len=strcspn(id,"@");char base[128],path[224];
    if (!len || len>=sizeof(base)) return -1;
    memcpy(base,id,len);base[len]='\0';
    if (!qlcg_plant_has_release(base)) return 0;
    int stage=id[len] ? qlcg_pd_state_of(id+len+1) : PD_STATE_NORMAL;
    if(stage<PD_STATE_NORMAL)stage=PD_STATE_NORMAL;
    int phase=season<0 ? 7+stage : (stage==PD_STATE_HANGING?4:0)+season;
    if (phase>=14) return -1;
    int f=form==VEG_FORM_SIMPLE?0:1+qlcg_plant_model_variant(form);
    snprintf(path,sizeof(path),"content/data/world/plant_models/%s.plm",base);
    FILE *file=qlcg_platform_fopen(path,"rb");
    if(!file)return -1;
    char magic[4];uint32_t slots[56];int result=-1;
    if(fread(magic,1,4,file)==4 && !memcmp(magic,"PLM1",4) && fread(slots,4,56,file)==56) {
        uint32_t offset=slots[phase*4+f];
        if (!offset) offset=slots[(season<0?8:season)*4+f];
        if (offset==1) result=0;
        else if (offset>=228 && offset<=0x7fffffff && !fseek(file,(long)offset,SEEK_SET))
            result=decode_record(file,g)?1:-1;
    }
    fclose(file);return result;
}
