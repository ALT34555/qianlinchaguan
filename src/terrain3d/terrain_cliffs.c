#include "terrain_cliffs.h"
#include "../systems/world/model/cliff_rock.h"
#include "../systems/world/surface_internal.h"
#include "../systems/world/surface_macro.h"
#include "../systems/world/surface_rivers.h"
#include "../core/math_util.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int cx,cz,valid,model,color,wet;
    uint32_t seed;
    double x,z;
    float ux,uz,slope,activity,height;
} CliffAnchor;

typedef struct { double x,z; float depth,edge,reference,r,g,b; } CliffVertex;
static CliffAnchor anchors[4096];
static const MacroWorld *anchor_world;
static uint32_t anchor_revision;

void qlcg_terrain_cliffs_reset(void) {
    memset(anchors,0,sizeof(anchors));
    anchor_world=NULL;
}

void qlcg_terrain_cliff_site(TerrainCliffSite sites[4], int x, int z, float dx, float dz) {
    if (x<0 || x>16 || z<0 || z>16) return;
    int slot=(x>=8)+(z>=8)*2;
    float score=dx*dx+dz*dz;
    if (score>sites[slot].score) sites[slot]=(TerrainCliffSite){x+1,z+1,dx,dz,score};
}

static float field(const MacroWorld *w,const SurfaceMacroContext *c,const float *v) {
    return v ? (float)qlcg_read_macro(w,c,v) : 0;
}

static const CliffAnchor *anchor_at(const MacroWorld *w,int cx,int cz) {
    uint32_t seed=qlcg_hash_u32(w->opt.seed^(uint32_t)cx*73856093u^(uint32_t)cz*19349663u);
    CliffAnchor *a=&anchors[seed%4096];
    if (a->valid && a->cx==cx && a->cz==cz && a->seed==seed) return a;
    *a=(CliffAnchor){.cx=cx,.cz=cz,.seed=seed,.valid=1};
    a->x=(double)cx*32+((seed&65535)/65535.0- .5)*14;
    a->z=(double)cz*32+((seed>>16)/65535.0- .5)*14;
    SurfaceMacroContext c=qlcg_macro_context(w,a->x,a->z);
    float rift=field(w,&c,w->rift), shear=field(w,&c,w->shear), uplift=field(w,&c,w->uplift);
    a->activity=qlcg_clampf(fmaxf(rift*1.6f,fmaxf(shear*1.7f,uplift*1.3f)),0,1);
    a->color=qlcg_cliff_color_at(w,a->x,a->z);
    if (rift>.32f) a->model=0;
    else if (shear>.34f) a->model=3;
    else if (uplift>.44f) a->model=4;
    else a->model=(seed>>24)%2 ? 1 : 5;
    float h[4];
    for(int k=0;k<4;k++) {
        float x=(float)(a->x+(k==0?4:k==1?-4:0)),z=(float)(a->z+(k==2?4:k==3?-4:0));
        float natural=qlcg_sample_natural_height(w,x,z);
        RiverSurfaceSample river={0}; river.level=-1e9f;
        qlcg_river_surface(w,x,z,natural,&river); h[k]=river.height;
        if(river.level>river.height+.02f || river.kind==WATER_KIND_FALL || river.kind==WATER_KIND_RIVER) a->wet=1;
        if(river.waterfall_rock>.1f) a->activity=1;
    }
    float dx=(h[0]-h[1])/8,dz=(h[2]-h[3])/8;
    a->height=(h[0]+h[1]+h[2]+h[3])*.25f;
    a->slope=hypotf(dx,dz);
    if (!isfinite(a->slope)) a->slope=0;
    a->ux=a->slope>.01f ? dx/a->slope : 0;
    a->uz=a->slope>.01f ? dz/a->slope : 1;
    return a;
}

static void bounds_of(const RockGeometry *g,int index,int color,float **lo,float **hi) {
    static float bounds[8][CLIFF_COLOR_COUNT][2][3];
    *lo=bounds[index][color][0]; *hi=bounds[index][color][1];
    if ((*hi)[1]>(*lo)[1]) return;
    for (int k=0;k<3;k++) (*lo)[k]=(*hi)[k]=g->positions[k];
    for (int i=1;i<g->vertex_count;i++) for (int k=0;k<3;k++) {
        (*lo)[k]=fminf((*lo)[k],g->positions[i*3+k]);
        (*hi)[k]=fmaxf((*hi)[k],g->positions[i*3+k]);
    }
}

static CliffVertex interpolate(CliffVertex a,CliffVertex b,double t) {
    return (CliffVertex){a.x+(b.x-a.x)*t,a.z+(b.z-a.z)*t,
        qlcg_lerpf(a.depth,b.depth,t),qlcg_lerpf(a.edge,b.edge,t),
        qlcg_lerpf(a.reference,b.reference,t),
        qlcg_lerpf(a.r,b.r,t),qlcg_lerpf(a.g,b.g,t),qlcg_lerpf(a.b,b.b,t)};
}

static int clip_plane(const CliffVertex *in,int n,CliffVertex *out,const float plane[3]) {
    int count=0;
    for(int i=0;i<n;i++) {
        CliffVertex a=in[i],b=in[(i+1)%n];
        double da=a.x*plane[0]+a.z*plane[1]+plane[2],db=b.x*plane[0]+b.z*plane[1]+plane[2];
        if(da>=0) out[count++]=a;
        if((da>=0)!=(db>=0)) {
            if(a.x>b.x || (a.x==b.x && a.z>b.z)) {
                CliffVertex swap=a;a=b;b=swap;double d=da;da=db;db=d;
            }
            out[count++]=interpolate(a,b,da/(da-db));
        }
    }
    return count;
}

static float surface_slope(const RegionData *reg,float x,float z) {
    float u=(x+18)/2,v=(z+18)/2;
    int ix=qlcg_clampi((int)floorf(u),1,reg->width-3),iz=qlcg_clampi((int)floorf(v),1,reg->height-3);
    float tx=qlcg_clampf(u-ix,0,1),tz=qlcg_clampf(v-iz,0,1),dx[4],dz[4];
    for(int k=0;k<4;k++) {
        int i=(iz+k/2)*reg->width+ix+k%2;
        dx[k]=(reg->heights[i+1]-reg->heights[i-1])*.25f;
        dz[k]=(reg->heights[i+reg->width]-reg->heights[i-reg->width])*.25f;
    }
    return hypotf(qlcg_lerpf(qlcg_lerpf(dx[0],dx[1],tx),qlcg_lerpf(dx[2],dx[3],tx),tz),
        qlcg_lerpf(qlcg_lerpf(dz[0],dz[1],tx),qlcg_lerpf(dz[2],dz[3],tx),tz));
}

static TerrainVert surface_vertex(const RegionData *reg,const MacroWorld *w,
    CliffVertex p,float activity,double ox,double oz,float *lift,int solid) {
    float h=qlcg_region_height_at(reg,p.x,p.z);
    float fit=1-qlcg_smoothf(4,14,fabsf(h-p.reference));
    float wet=qlcg_region_water_at(reg,p.x,p.z);
    int ix=qlcg_clampi((int)lroundf((p.x+18)/2),0,reg->width-1);
    int iz=qlcg_clampi((int)lroundf((p.z+18)/2),0,reg->height-1), src=iz*reg->width+ix;
    int kind=reg->water_kind ? reg->water_kind[src] : WATER_KIND_DRY;
    if(reg->colors) {
        float tone=.24f+(.25f*reg->colors[src*3]+.55f*reg->colors[src*3+1]+.20f*reg->colors[src*3+2])*.4f/255;
        p.r=qlcg_lerpf(tone*1.06f,p.r,.18f);
        p.g=qlcg_lerpf(tone*1.02f,p.g,.18f);
        p.b=qlcg_lerpf(tone*.97f,p.b,.18f);
    }
    if(wet>h+.02f || kind==WATER_KIND_FALL || kind==WATER_KIND_RIVER) {
        *lift=-.15f;
        return (TerrainVert){(float)(ox+p.x),h-.15f,(float)(oz+p.z),0,1,0,p.r,p.g,p.b};
    }
    if(solid) {
        float slope=surface_slope(reg,p.x,p.z);
        float depth=p.depth;
        if(depth>0) depth=3.2f*depth/(3.2f+depth);
        float offset=depth*sqrtf(1+fminf(slope*slope,36));
        offset*=qlcg_smoothf(.65f,1.5f,slope)*fit;
        float edge=qlcg_smoothf(0,1,p.edge);
        float y=h+offset-fmaxf(0,offset+.20f)*(1-edge)-.2f*(1-fit);
        *lift=y-h;
        return (TerrainVert){(float)(ox+p.x),y,(float)(oz+p.z),0,0,0,p.r,p.g,p.b};
    }
    float slope=surface_slope(reg,p.x,p.z);
    float rock=reg->coast_material ? reg->coast_material[src*3+2]/255.0f : 0;
    float threshold=2.05f-.80f*activity-.25f*rock;
    float steep=qlcg_smoothf(threshold-.3f,threshold+.25f,slope)*fit;
    float wx=(float)((reg->center_x+p.x)/19.0),wz=(float)((reg->center_z+p.z)/19.0);
    float noise=.5f+.5f*qlcg_noise3(wx,wz,3,w->opt.seed^0xb27c14u);
    float opening=qlcg_smoothf(.20f+.16f*(1-activity),.40f+.16f*(1-activity),noise);
    float edge=qlcg_smoothf(0,1,p.edge);
    float relief=(.9f+.65f*activity)*edge*steep*opening;
    float offset=relief*(p.depth-.12f)-.10f-1.5f*(1-steep*opening);
    offset*=sqrtf(1+slope*slope);
    if(offset>0) offset+=.08f*sqrtf(1+slope*slope);
    if(offset>0) {
        float limit=(rock>.5f?1.2f:2.8f)*sqrtf(1+fminf(slope*slope,16));
        offset=limit*offset/(limit+offset);
    }
    *lift=offset;
    return (TerrainVert){(float)(ox+p.x),h+offset,(float)(oz+p.z),0,0,0,p.r,p.g,p.b};
}

static void emit_triangle(TerrainCliffMesh *mesh,TerrainVert a,TerrainVert b,TerrainVert c) {
    float ax=b.x-a.x,ay=b.y-a.y,az=b.z-a.z,bx=c.x-a.x,by=c.y-a.y,bz=c.z-a.z;
    float nx=ay*bz-az*by,ny=az*bx-ax*bz,nz=ax*by-ay*bx;
    if (nx*nx+ny*ny+nz*nz<=1e-12f) return;
    if (mesh->count+3>mesh->capacity) {
        int capacity=mesh->capacity ? mesh->capacity*2 : 4096;
        TerrainVert *v=realloc(mesh->vertices,(size_t)capacity*sizeof(*v));
        if (!v) return;
        mesh->vertices=v;mesh->capacity=capacity;
    }
    qlcg_vec3_norm(&nx,&ny,&nz);
    a.nx=b.nx=c.nx=nx;a.ny=b.ny=c.ny=ny;a.nz=b.nz=c.nz=nz;
    mesh->vertices[mesh->count++]=a;mesh->vertices[mesh->count++]=b;mesh->vertices[mesh->count++]=c;
}

static void surface_triangle(TerrainCliffMesh *mesh,const RegionData *reg,const MacroWorld *w,
    CliffVertex a,CliffVertex b,CliffVertex c,float activity,double ox,double oz,int solid) {
    if(!solid && (b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z)<=1e-7f) return;
    if (fminf(a.x,fminf(b.x,c.x))>16 || fmaxf(a.x,fmaxf(b.x,c.x))<-16 ||
        fminf(a.z,fminf(b.z,c.z))>16 || fmaxf(a.z,fmaxf(b.z,c.z))<-16) return;
    int xmin=qlcg_clampi((int)floorf((fminf(a.x,fminf(b.x,c.x))+16)/2),0,15);
    int xmax=qlcg_clampi((int)floorf((fmaxf(a.x,fmaxf(b.x,c.x))+16)/2),0,15);
    int zmin=qlcg_clampi((int)floorf((fminf(a.z,fminf(b.z,c.z))+16)/2),0,15);
    int zmax=qlcg_clampi((int)floorf((fmaxf(a.z,fmaxf(b.z,c.z))+16)/2),0,15);
    for(int z=zmin;z<=zmax;z++) for(int x=xmin;x<=xmax;x++) for(int side=0;side<2;side++) {
        float left=x*2-16,top=z*2-16;
        CliffVertex polygon[2][12];polygon[0][0]=a;polygon[0][1]=b;polygon[0][2]=c;
        int n=3;
        int ix=x+1,iz=z+1,s=side?1:0;
        float da=qlcg_chunk_water_vertex(reg,ix+s,iz+s)-reg->heights[(iz+s)*reg->width+ix+s]-.02f;
        float db=qlcg_chunk_water_vertex(reg,ix+1,iz)-reg->heights[iz*reg->width+ix+1]-.02f;
        float dc=qlcg_chunk_water_vertex(reg,ix,iz+1)-reg->heights[(iz+1)*reg->width+ix]-.02f;
        float gx=side?(da-dc)*.5f:(db-da)*.5f,gz=side?(da-db)*.5f:(dc-da)*.5f;
        float planes[6][3]={{1,0,-left},{-1,0,left+2},{0,1,-top},{0,-1,top+2},
            {side?1:-1,side?1:-1,(side?-1:1)*(left+top+2)},
            {-gx,-gz,gx*(left+2*s)+gz*(top+2*s)-da}};
        for(int k=0;k<6 && n;k++) n=clip_plane(polygon[k%2],n,polygon[(k+1)%2],planes[k]);
        TerrainVert v[12];float lift[12];
        for(int j=0;j<n;j++) v[j]=surface_vertex(reg,w,polygon[0][j],activity,ox,oz,&lift[j],solid);
        for(int j=1;j<n-1;j++) if(fminf(lift[0],fminf(lift[j],lift[j+1]))>-1e5f &&
            fmaxf(lift[0],fmaxf(lift[j],lift[j+1]))>0)
            emit_triangle(mesh,v[0],v[j],v[j+1]);
    }
}

static void build_layer(TerrainCliffMesh *mesh,const RegionData *reg,const MacroWorld *w,
    const CliffAnchor *a,int layer,int layers,double ox,double oz) {
    uint32_t seed=qlcg_hash_u32(a->seed^(uint32_t)(layer+1)*0x9e3779b9u);
    const int companions[8]={4,7,1,6,2,1,3,5};
    int index=(seed&3) ? a->model : companions[a->model];
    if ((seed&15)==0) index=(seed>>16)%8;
    const RockGeometry *g=qlcg_cliff_geometry(index,(CliffRockColor)a->color);
    if (!g || !g->vertex_count) return;
    float *lo,*hi;bounds_of(g,index,a->color,&lo,&hi);
    float angle=((seed&255)/255.0f-.5f)*.28f/sqrtf(1+a->slope*a->slope);
    float ux=a->ux*cosf(angle)-a->uz*sinf(angle),uz=a->uz*cosf(angle)+a->ux*sinf(angle);
    float width=17+8*((seed>>8)&255)/255.0f;
    float length=fminf(12,(10+5*((seed>>16)&255)/255.0f)/fmaxf(1.4f,a->slope));
    float along=(layer-(layers-1)*.5f)*length*1.6f;
    float shift=(((seed>>24)&255)/255.0f-.5f)*6;
    double cx=a->x-reg->center_x+a->ux*along+a->uz*shift;
    double cz=a->z-reg->center_z+a->uz*along-a->ux*shift;
    int start=mesh->count;
    for (int i=0;i<g->index_count;i+=3) {
        CliffVertex polygon[2][10];
        for (int j=0;j<3;j++) {
            int v=g->indices[i+(j ? 3-j : 0)];
            float across=2*(g->positions[v*3]-lo[0])/(hi[0]-lo[0])-1;
            float up=2*(g->positions[v*3+1]-lo[1])/(hi[1]-lo[1])-1;
            float depth=(g->positions[v*3+2]-lo[2])/(hi[2]-lo[2]);
            float edge=qlcg_clampf((1-fabsf(across+.12f*sinf(up*8+index)))*4,0,1)*
                qlcg_clampf((1-fabsf(up+.13f*sinf(across*7+index)))*4,0,1);
            depth*=.72f+.4f*((seed>>10)&255)/255.0f;
            polygon[0][j]=(CliffVertex){cx+uz*width*across+ux*length*up,
                cz-ux*width*across+uz*length*up,depth,edge,0,
                fmaxf(.35f,g->colors[v*3]/255.0f),fmaxf(.36f,g->colors[v*3+1]/255.0f),fmaxf(.37f,g->colors[v*3+2]/255.0f)};
            CliffVertex *p=&polygon[0][j];
            p->reference=a->height+a->slope*((p->x+reg->center_x-a->x)*a->ux+
                (p->z+reg->center_z-a->z)*a->uz);
        }
        surface_triangle(mesh,reg,w,polygon[0][0],polygon[0][1],polygon[0][2],a->activity,ox,oz,0);
    }
    if (mesh->count>start) mesh->patches++;
}

static void build_mass(TerrainCliffMesh *mesh,const RegionData *reg,const MacroWorld *w,
    const CliffAnchor *a,double ox,double oz) {
    if(a->wet) return;
    uint32_t seed=qlcg_hash_u32(a->seed^0x6d617373u);
    if((seed&255)/255.0f>.12f+.22f*a->activity) return;
    static const int forms[3][4]={{2,6,3,0},{2,3,6,1},{3,2,6,4}};
    int group=a->model==0?0:a->model==3?1:2;
    float scale=2.1f+.65f*((seed>>8)&255)/255.0f;
    float sy=fminf(1.8f,scale*(.9f+.3f*((seed>>16)&255)/255.0f));
    float angle=(((seed>>24)&255)/255.0f-.5f)*.7f;
    float run=1/sqrtf(1+a->slope*a->slope),rise=a->slope*run;
    float burial=(2.55f+.2f*((seed>>4)&255)/255.0f)*scale;
    float y=0,total=0;
    int layers=1+(a->activity>.45f)+((seed&7)==0);
    const RockGeometry *models[3];uint32_t keys[3];int count=0;
    int start=mesh->count;
    for(int layer=0;layer<layers;layer++) {
        uint32_t key=qlcg_hash_u32(seed^(uint32_t)(layer+1)*0x9e3779b9u);
        int index=forms[group][layer?1+(key%3):key%4];
        if(layer==layers-1 && (key&15)==0) index=(key&16)?7:5;
        const RockGeometry *g=qlcg_stone_forest_geometry(index,(CliffRockColor)a->color);
        if(!g || !g->vertex_count) continue;
        if(count && total+(float)g->height*sy>36) break;
        models[count]=g;keys[count++]=key;total+=(float)g->height*sy;
    }
    for(int layer=0;layer<count;layer++) {
        const RockGeometry *g=models[layer];uint32_t key=keys[layer];
        float yaw=angle+((key>>8)%3-1.0f)*.785398163f;
        double cx=a->x-reg->center_x,cz=a->z-reg->center_z;
        for(int i=0;i<g->index_count;i+=3) {
            float low=1e9f,high=-1e9f;
            for(int j=0;j<3;j++) {
                float h=g->positions[g->indices[i+j]*3+1];
                low=fminf(low,h);high=fmaxf(high,h);
            }
            if((layer && high<.001f) || (layer<count-1 && low>g->height-.001)) continue;
            CliffVertex p[3];
            for(int j=0;j<3;j++) {
                int v=g->indices[i+j];
                float x=g->positions[v*3]*scale,z=g->positions[v*3+2]*scale;
                float across=x*cosf(yaw)+z*sinf(yaw),along=z*cosf(yaw)-x*sinf(yaw)-burial;
                along*=.46f;
                float up=y+g->positions[v*3+1]*sy-total*.5f;
                float foot=layer?1:qlcg_clampf(g->positions[v*3+1]/fmaxf(1,(float)g->height*.28f),0,1);
                if(layer==count-1) foot*=qlcg_clampf(((float)g->height-g->positions[v*3+1])/
                    fmaxf(1,(float)g->height*.22f),0,1);
                p[j]=(CliffVertex){cx-a->uz*across+a->ux*(up*run-along*rise),
                    cz+a->ux*across+a->uz*(up*run-along*rise),along,foot,0,
                    fmaxf(.35f,g->colors[v*3]/255.0f),fmaxf(.36f,g->colors[v*3+1]/255.0f),
                    fmaxf(.37f,g->colors[v*3+2]/255.0f)};
                p[j].reference=a->height+a->slope*((p[j].x+reg->center_x-a->x)*a->ux+
                    (p[j].z+reg->center_z-a->z)*a->uz);
            }
            surface_triangle(mesh,reg,w,p[0],p[1],p[2],a->activity,ox,oz,1);
        }
        y+=(float)g->height*sy;
    }
    if(mesh->count>start) mesh->patches++;
}

void qlcg_terrain_cliffs_build(TerrainCliffMesh *mesh,const RegionData *reg,const MacroWorld *w,
    const TerrainCliffSite sites[4],double ox,double oz) {
    if (!w || w->columns<1 || w->rows<1 || !(w->circumference>0)) return;
    float slope=0;
    for (int i=0;i<4;i++) slope=fmaxf(slope,sites[i].score);
    if (anchor_world!=w || anchor_revision!=w->surface_revision) {
        qlcg_terrain_cliffs_reset();anchor_world=w;anchor_revision=w->surface_revision;
    }
    for (int dz=-1;dz<=1;dz++) for (int dx=-1;dx<=1;dx++) {
        const CliffAnchor *a=anchor_at(w,reg->center_cx+dx,reg->center_cz+dz);
        if(a->slope<1.2f) continue;
        if (((a->seed>>12)&65535)/65535.0f>.24f+.72f*a->activity) continue;
        if(slope>=1.2f*1.2f) {
            int layers=qlcg_clampi((int)ceilf(a->slope*24/30),1,4);
            for (int layer=0;layer<layers;layer++) build_layer(mesh,reg,w,a,layer,layers,ox,oz);
        }
        build_mass(mesh,reg,w,a,ox,oz);
    }
}
