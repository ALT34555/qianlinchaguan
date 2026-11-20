#include "paint_surface.h"
#include "core/math_util.h"
#include "systems/world/gen/gen_noise.h"
#include "systems/world/world_civ_economy.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    int cx,cz,natural;
    uint8_t material;
    bool valid;
} PaintMaterial;

struct PaintSurfaceCache {
    const MacroWorld *world;
    uint32_t seed,delta,surface,territory;
    bool artificial,all_dirty;
    bool mesh_dirty[QLCG_STREAM_CAP];
    PaintMaterial entries[QLCG_STREAM_CAP];
};
static void bleed_axis(float axis[3],float local,float warp,float width) {
    float west=qlcg_smoothf(-width,width,local+CHUNK_SIZE*.5f+warp);
    float east=qlcg_smoothf(-width,width,local-CHUNK_SIZE*.5f+warp);
    axis[0]=1-west;axis[1]=west-east;axis[2]=east;
}
static void bleed_axes(float ax[3],float az[3],const RegionData *r,float x,float z) {
    double wx=(double)r->center_cx*CHUNK_SIZE+x,wz=(double)r->center_cz*CHUNK_SIZE+z;
    double u=wx*.8+wz*.6,v=wz*.8-wx*.6;
    float dx=(float)(7*qlcg_base_noise_d(u/24,0,v/24,0x53a91u)+3*qlcg_base_noise_d(u/8,0,v/8,0x815b7u));
    float dz=(float)(7*qlcg_base_noise_d(u/24,0,v/24,0x79d43u)+3*qlcg_base_noise_d(u/8,0,v/8,0x26f05u));
    float width=(float)(13+3*qlcg_base_noise_d(u/16,0,v/16,0x4ce29u));
    bleed_axis(ax,x,dx,width);bleed_axis(az,z,dz,width);
}
bool qlcg_paint_surface_sync(PaintSurfaceCache **cache,const MacroWorld *w) {
    if(!w)return false;
    if(!*cache)*cache=calloc(1,sizeof(**cache));
    PaintSurfaceCache *c=*cache;if(!c)return false;
    bool territory=c->artificial!=w->opt.artificial_enabled||c->territory!=w->territory_revision;
    if(c->world==w&&c->seed==w->opt.seed&&c->delta==w->delta_version&&
       c->surface==w->surface_revision&&!territory)return false;
    bool changed=c->world!=NULL;
    QlcgSurfaceBounds changes[QLCG_SURFACE_DIRTY_CAP];
    int count=(c->world==w&&c->seed==w->opt.seed)?qlcg_world_surface_changes(w,c->delta,c->surface,changes):-1;
    c->all_dirty=count<0;
    memset(c->mesh_dirty,0,sizeof(c->mesh_dirty));
    if(c->all_dirty) memset(c->entries,0,sizeof(c->entries));
    else {
        for(int j=0;j<count;j++) territory|=changes[j].appearance_only;
        for(int i=0;i<QLCG_STREAM_CAP;i++) {
            PaintMaterial *entry=c->entries+i;
            if(!entry->valid)continue;
            double x=(double)entry->cx*CHUNK_SIZE,z=(double)entry->cz*CHUNK_SIZE;
            for(int j=0;j<count&&entry->valid;j++)
                if(qlcg_surface_bounds_overlap(w,changes+j,x,z,x+CHUNK_SIZE,z+CHUNK_SIZE))
                    entry->valid=false;
            if(entry->valid) {
                if(!territory)continue;
                int type=qlcg_civ_chunk_type(w,entry->cx,entry->cz,entry->natural,0);
                int material=qlcg_chunk_material(type);
                if(material==entry->material)continue;
                entry->material=(uint8_t)material;
            }
            for(int dz=-1;dz<=1;dz++) for(int dx=-1;dx<=1;dx++)
                c->mesh_dirty[qlcg_stream_slot(entry->cx+dx,entry->cz+dz)]=true;
        }
    }
    c->world=w;c->seed=w->opt.seed;c->delta=w->delta_version;
    c->surface=w->surface_revision;c->territory=w->territory_revision;c->artificial=w->opt.artificial_enabled;
    return changed;
}
bool qlcg_paint_mesh_dirty(const PaintSurfaceCache *cache,int slot) {
    return cache->all_dirty||cache->mesh_dirty[slot];
}
int qlcg_paint_surface_material(PaintSurfaceCache *c,const MacroWorld *w,int cx,int cz) {
    int slot=qlcg_stream_slot(cx,cz);
    if(c&&c->entries[slot].valid&&c->entries[slot].cx==cx&&c->entries[slot].cz==cz)
        return c->entries[slot].material;
    ChunkInspectInfo info;qlcg_world_inspect_chunk(w,cx,cz,&info);
    int material=qlcg_chunk_material(info.type_id);
    if(c) c->entries[slot]=(PaintMaterial){cx,cz,info.natural_type_id,(uint8_t)material,true};
    return material;
}
void qlcg_paint_surface_blend(TerrainVert *v,const RegionData *r,int i,int season,const uint8_t materials[9]) {
    float base[6]={0};
    float arid=r->aridities?r->aridities[i]:.4f,temp=r->temperatures?r->temperatures[i]:15;
    float cover=qlcg_clampf((.85f-arid)*1.5f,0,1)*qlcg_smoothf(-8,4,temp);
    if(season==2)cover*=.60f;
    if(season==3)cover*=.35f;
    float ax[3]={0,1,0},az[3]={0,1,0};
    if(materials&&r->width>1) {
        float x=(i%r->width-(r->width-1)*.5f)*r->step;
        float z=(i/r->width-(r->height-1)*.5f)*r->step;
        bool mixed=false;for(int j=0;j<9;j++)if(materials[j]!=materials[4])mixed=true;
        if(mixed)bleed_axes(ax,az,r,x,z);
    }
    for(int z=0;z<3;z++)for(int x=0;x<3;x++) {
        float weight=ax[x]*az[z];if(weight<=0)continue;
        int material=materials?materials[z*3+x]:qlcg_chunk_material(r->site_chunk_type);
        base[material>=1&&material<=5?material:2]+=weight;
    }
    float total=base[1]+base[2]+base[3]+base[4]+base[5];
    float grass=cover*(base[2]/total),sand=base[4]/total;
    float stone=base[1]/total+.65f*(base[5]/total),salt=base[3]/total;
    if(r->coast_material) {
        const uint8_t *c=r->coast_material+i*3;
        float total=qlcg_clampf((c[0]+c[1]+c[2])/255.0f,0,1);
        grass*=1-total;sand=sand*(1-total)+c[1]/255.0f;
        stone=stone*(1-total)+c[2]/255.0f;salt*=1-total;
    }
    float sediment=r->river_sediment?r->river_sediment[i]/255.0f:0;
    grass*=1-sediment;stone*=1-sediment;salt*=1-sediment;sand=sand*(1-sediment)+sediment;
    float crust=r->source_effects?r->source_effects[i].salt/255.0f:0;
    grass*=1-crust;sand*=1-crust;stone*=1-crust;salt=salt*(1-crust)+crust;
    float weights[4]={grass,sand,stone,salt},sum=grass+sand+stone+salt;
    for(int j=0;j<4;j++)v->paint[j]=(uint8_t)(qlcg_clampf(weights[j]/fmaxf(sum,1),0,1)*255);
}
void qlcg_paint_surface_weights(TerrainVert *v,const RegionData *r,int i,int season) {
    qlcg_paint_surface_blend(v,r,i,season,NULL);
}
void qlcg_terrain_vertex_layout(size_t base) {
    for(int i=0;i<3;i++) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i,3,GL_FLOAT,GL_FALSE,sizeof(TerrainVert),(void *)(base+i*3*sizeof(float)));
    }
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(TerrainVert),(void *)(base+offsetof(TerrainVert,paint)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(TerrainVert),(void *)(base+offsetof(TerrainVert,farm)));
}
