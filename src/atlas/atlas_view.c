/* 舆图资源：上下文、纹理上传与视图复位 */
#include "atlas_view.h"
#include "../core/math_util.h"
#include "../core/dev_tools.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "atlas_shaders.h"
#include "atlas_tiles.h"
#include "atlas_vegetation.h"
#include "../systems/world/world_infinite.h"
#include "../systems/world/climate_zone.h"
#include <limits.h>

#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif

bool qlcg_atlas_gpu_restore(AtlasRenderer *ar) {
    ar->prog = qlcg_gl_compile_program(ATLAS_VS_SRC, ATLAS_FS_SRC);
    if(!ar->prog) return false;
    glUseProgram(ar->prog);
    const char *samplers[]={"uTex0","uTex1","uSatTex","uTileTex","uTileMeta","uRainTex","uTileWater","uZoneTex"};
    const int slots[]={0,1,2,3,4,5,6,8};
    for(int i=0;i<8;i++)glUniform1i(qlcg_gl_uniform(ar->prog,samplers[i]),slots[i]);
    {
        float zc[QLCG_CLIMATE_TYPES * 3];
        for (int z = 0; z < QLCG_CLIMATE_TYPES; z++) {
            const QlcgClimateType *info = qlcg_climate_type_at(z);
            uint32_t rgb=info?info->color_rgb:0x808080;
            zc[z * 3 + 0] = (float)((rgb>>16)&255);
            zc[z * 3 + 1] = (float)((rgb>>8)&255);
            zc[z * 3 + 2] = (float)(rgb&255);
        }
        glUniform3fv(qlcg_gl_uniform(ar->prog, "uZoneColors"), QLCG_CLIMATE_TYPES, zc);
    }

    glGenVertexArrays(1, &ar->vao);
    glGenBuffers(1, &ar->vbo);
    glBindVertexArray(ar->vao);
    glBindBuffer(GL_ARRAY_BUFFER, ar->vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));

    return true;
}

bool qlcg_atlas_init(AtlasRenderer *ar) {
    memset(ar,0,sizeof(*ar));
    if(!qlcg_atlas_gpu_restore(ar)) return false;
    ar->planet_view  = PLANET_VIEW_EQUIRECT;
    ar->sat_mode     = false;
    ar->zoom         = 4.0f;
    ar->target_zoom  = 4.0f;
    ar->brush_radius = 950.0f;
    qlcg_init_atlas_display(&ar->display);
    if (qlcg_dev_atlas_rain()) {
        for (int i = 0; i < LAYER_ID_COUNT; i++) {
            if (ar->display.layers[i].id == LAYER_RAIN) {
                ar->display.layers[i].on = true;
                ar->display.layers[i].opacity = 1.0f;
            }
        }
    }
    return true;
}

void qlcg_atlas_inspect_hover(AtlasRenderer *ar,const MacroWorld *w,int cx,int cz) {
    if(!w || !qlcg_world_chunk_explored(w,cx,cz)) {
        ar->hover_world=NULL;ar->hover_info=(ChunkInspectInfo){0};return;
    }
    if(ar->hover_world==w && ar->hover_cx==cx && ar->hover_cz==cz &&
       ar->hover_delta==w->delta_version && ar->hover_surface==w->surface_revision &&
       ar->hover_territory==w->territory_revision && ar->hover_artificial==w->opt.artificial_enabled && ar->hover_cached_info.type_def) {
        ar->hover_info=ar->hover_cached_info;return;
    }
    qlcg_world_inspect_chunk(w,cx,cz,&ar->hover_info);
    ar->hover_cached_info=ar->hover_info;
    ar->hover_world=w;ar->hover_cx=cx;ar->hover_cz=cz;
    ar->hover_delta=w->delta_version;ar->hover_surface=w->surface_revision;
    ar->hover_territory=w->territory_revision;
    ar->hover_artificial=w->opt.artificial_enabled;
}

void qlcg_atlas_release_world(AtlasRenderer *ar) {
    ar->hover_world=NULL;ar->hover_info=ar->hover_cached_info=(ChunkInspectInfo){0};
    qlcg_atlas_rivers_release(ar);
    qlcg_atlas_vegetation_release(ar);
    qlcg_atlas_tiles_destroy(ar->tiles);
    ar->tiles = NULL;
    qlcg_gl_delete_textures(1,&ar->eclipse_tex);ar->eclipse_tex=0;
    ar->eclipse_ready=false;ar->astronomy_overlay=false;
    GLuint textures[] = {ar->data_tex0, ar->data_tex1, ar->sat_tex, ar->rain_tex, ar->zone_tex};
    qlcg_gl_delete_textures(5, textures);
    ar->data_tex0 = ar->data_tex1 = ar->sat_tex = ar->rain_tex = ar->zone_tex = 0;
    ar->tex_w = ar->tex_h = ar->rain_tex_w = ar->rain_tex_h = 0;
    ar->world_upload_pending = false;
    ar->synced_atlas_rev = ar->synced_terr_rev = 0;
    ar->rain_key_day = ar->rain_key_ver = ar->rain_key_stamp = 0;
    ar->rain_last_ms = ar->last_render_ms = 0.0;
    ar->synced_plane_count=0;
    ar->detail_world=NULL;
}

void qlcg_atlas_shutdown(AtlasRenderer *ar) {
    bool overlay=ar->astronomy_overlay;
    qlcg_atlas_release_world(ar);
    ar->astronomy_overlay=overlay;
    if (ar->vbo)       qlcg_gl_delete_buffers(1, &ar->vbo);
    if (ar->vao)       qlcg_gl_delete_vertex_arrays(1, &ar->vao);
    if (ar->prog)      qlcg_gl_delete_program(ar->prog);
    ar->prog=ar->vao=ar->vbo=0;
}

static const MacroWorld *atlas_plane(const MacroWorld *w,double x,double z) {
    x+=w->opt.plane_x;z+=w->opt.plane_z;
    if(x<INT32_MIN || x>INT32_MAX || z<INT32_MIN || z>INT32_MAX)return NULL;
    return qlcg_infinite_find(w,(int32_t)x,(int32_t)z);
}

typedef struct {
    int width,height,pitch;
    double origin_x,origin_z,span_x,span_z;
} AtlasGrid;

static AtlasGrid atlas_grid(const MacroWorld *w) {
    AtlasGrid g={.width=w->columns,.height=w->rows,.pitch=w->columns+1,
                 .span_x=w->columns,.span_z=w->rows};
    if(qlcg_world_flat(w)) {
        const InfiniteWorld *p=w->planes;
        g.origin_x=(p?(double)p->min_x-w->opt.plane_x:0)*g.pitch-1;
        g.origin_z=(p?(double)p->min_z-w->opt.plane_z:0)*g.pitch-1;
        g.span_x=(p?(double)p->max_x-p->min_x+1:1)*g.pitch+1;
        g.span_z=(p?(double)p->max_z-p->min_z+1:1)*g.pitch+1;
        g.width=(int)fmin(1024,g.span_x);g.height=(int)fmin(1024,g.span_z);
    }
    return g;
}

static int first_texel(int cell,double origin,double span,int pixels) {
    double texel=ceil(((double)cell-origin)*pixels/span-.5);
    return (int)fmax(0,fmin(pixels,texel));
}

static bool upload_rect(const GLuint textures[4],const MacroWorld *w,const AtlasGrid *grid,
                        MacroCellRect rect,bool allocate) {
    int width=rect.x1-rect.x0,height=rect.z1-rect.z0;
    size_t count=(size_t)width*height;
    bool flat=qlcg_world_flat(w);
    int tw=grid->width,th=grid->height,pitch=grid->pitch;
    double origin_x=grid->origin_x,origin_z=grid->origin_z,span_x=grid->span_x,span_z=grid->span_z;
    float *buf0=malloc(count*4*sizeof(float));
    uint8_t *buf1=malloc(count*4),*bufs=malloc(count*4),*bufz=malloc(count);
    bool ok=buf0&&buf1&&bufs&&bufz;
    if(!ok) goto done;
    double cached_x=INFINITY,cached_z=INFINITY;
    const MacroWorld *cached=NULL;
    for (int i=0;i<width*height;i++) {
        int u=rect.x0+i%width,v=rect.z0+i/width;
        bufz[i] = QLCG_ZONE_UNKNOWN;
        const MacroWorld *source=w;int cell=v*tw+u;
        if(flat) {
            double x=floor(origin_x+(u+.5)*span_x/tw),y=floor(origin_z+(v+.5)*span_z/th);
            double px=floor(x/pitch),py=floor(y/pitch);
            int lx=(int)(x-px*pitch),ly=(int)(y-py*pitch);
            if(px!=cached_x || py!=cached_z) {
                cached=atlas_plane(w,px,py);cached_x=px;cached_z=py;
            }
            source=cached;cell=ly*w->columns+lx;
            bool sea=lx==w->columns || ly==w->rows;
            if(sea || !source) {
                bool known=source!=NULL;
                if(lx==w->columns) known|=atlas_plane(w,px+1,py)!=NULL;
                if(ly==w->rows) known|=atlas_plane(w,px,py+1)!=NULL;
                if(lx==w->columns && ly==w->rows)known|=atlas_plane(w,px+1,py+1)!=NULL;
                buf0[i*4]=.16f;buf0[i*4+1]=.32f;buf0[i*4+2]=.52f;buf0[i*4+3]=-3000;
                buf1[i*4]=170;buf1[i*4+1]=128;buf1[i*4+2]=0;buf1[i*4+3]=4;
                bufs[i*4]=41;bufs[i*4+1]=82;bufs[i*4+2]=133;bufs[i*4+3]=sea && known?255:0;
                continue;
            }
        }
        float sh = source->shade[cell];
        for (int ch = 0; ch < 3; ch++)
            buf0[i * 4 + ch] = qlcg_clampi((int)(source->base_rgb[cell * 3 + ch] * sh), 0, 255) / 255.0f;
        buf0[i * 4 + 3] = source->height[cell];

        float tn = qlcg_clampf((source->temperature[cell] + 45.0f) / 90.0f, 0.0f, 1.0f);
        buf1[i * 4 + 0] = (uint8_t)(tn * 255.0f);
        buf1[i * 4 + 1] = (uint8_t)(qlcg_clampf(source->moisture[cell], 0.0f, 1.0f) * 255.0f);
        buf1[i * 4 + 2] = source->water[cell] == 2 ? 255 : 0;

        /* 编码筛查位掩码 */
        uint8_t mask = 0;
        int ctype = source->chunk_type[cell];
        if (!source->chunk_claims&&source->claim_type[cell] == 2) mask |= (1u << 0);
        if (!source->chunk_claims&&source->claim_type[cell] >= 1) mask |= (1u << 1);
        if (ctype >= 0   && ctype < 100)  mask |= (1u << 2);   /* 水体与湿地 */
        if (ctype >= 100 && ctype < 200)  mask |= (1u << 3);   /* 常年冰封 */
        if (ctype >= 200 && ctype < 300)  mask |= (1u << 4);   /* 寒带 */
        if (ctype >= 300 && ctype < 400)  mask |= (1u << 5);   /* 温带 */
        if (ctype >= 400)                 mask |= (1u << 6);   /* 亚热带与热带 */
        buf1[i * 4 + 3] = mask;
        bufz[i] = (uint8_t)qlcg_climate_type_index_cell(source,cell);

        bufs[i * 4 + 0] = (uint8_t)qlcg_clampi((int)((float)source->sat_rgb[cell * 3 + 0] * sh), 0, 255);
        bufs[i * 4 + 1] = (uint8_t)qlcg_clampi((int)((float)source->sat_rgb[cell * 3 + 1] * sh), 0, 255);
        bufs[i * 4 + 2] = (uint8_t)qlcg_clampi((int)((float)source->sat_rgb[cell * 3 + 2] * sh), 0, 255);
        bufs[i * 4 + 3] = 255;
    }

    const void *pixels[]={buf0,buf1,bufs,bufz};
    for(int i=0;i<4;i++) {
        GLenum type=i==0?GL_FLOAT:GL_UNSIGNED_BYTE;
        GLenum format=i==3?GL_RED:GL_RGBA;
        GLint storage=i==0?GL_RGBA32F:(i==3?GL_R8:GL_RGBA8);
        glBindTexture(GL_TEXTURE_2D,textures[i]);
        if(i==3) glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        if(allocate) glTexImage2D(GL_TEXTURE_2D,0,storage,width,height,0,format,type,pixels[i]);
        else glTexSubImage2D(GL_TEXTURE_2D,0,rect.x0,rect.z0,width,height,format,type,pixels[i]);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
done:
    free(buf0);free(buf1);free(bufs);free(bufz);
    return ok;
}

static void finish_textures(const GLuint textures[4],bool flat,bool setup) {
    for(int t=0;t<3;t++) {
        glBindTexture(GL_TEXTURE_2D,textures[t]);
        bool float_base=t==0 && (qlcg_gl_caps.es || !qlcg_gl_caps.float_linear);
        if(!float_base) glGenerateMipmap(GL_TEXTURE_2D);
        if (!setup) continue;
        if(float_base) {
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,qlcg_gl_caps.float_linear?GL_LINEAR:GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,qlcg_gl_caps.float_linear?GL_LINEAR:GL_NEAREST);
        } else if (t == 1) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        } else {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, flat?GL_CLAMP_TO_EDGE:GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    if(setup) {
        glBindTexture(GL_TEXTURE_2D,textures[3]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,flat?GL_CLAMP_TO_EDGE:GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
}

static void update_world(AtlasRenderer *ar,const MacroWorld *w,bool force_full) {
    if(!w)return;
    bool changed_world=ar->detail_world!=w;
    if(changed_world) {
        qlcg_atlas_rivers_release(ar);
        qlcg_atlas_vegetation_release(ar);
        qlcg_atlas_tiles_destroy(ar->tiles);ar->tiles=NULL;
        ar->detail_world=w;ar->eclipse_ready=false;
    }
    bool flat=qlcg_world_flat(w);
    AtlasGrid grid=atlas_grid(w);
    size_t planes=w->planes?w->planes->count:0;
    bool allocate=ar->tex_w!=grid.width||ar->tex_h!=grid.height||!ar->zone_tex;
    bool full=force_full||changed_world||allocate||ar->world_upload_pending||planes!=ar->synced_plane_count||
              (!w->chunk_claims&&ar->synced_terr_rev!=w->territory_revision);
    MacroCellRect rects[4*QLCG_ATLAS_CHANGE_HISTORY];
    int count=full?-1:qlcg_world_atlas_changes(w,ar->synced_atlas_rev,rects,4*QLCG_ATLAS_CHANGE_HISTORY);
    if(count<0) {
        rects[0]=(MacroCellRect){0,0,grid.width,grid.height};
        count=1;full=true;
    }
    ar->world_upload_pending=true;
    if(!ar->data_tex0) glGenTextures(1,&ar->data_tex0);
    if(!ar->data_tex1) glGenTextures(1,&ar->data_tex1);
    if(!ar->sat_tex) glGenTextures(1,&ar->sat_tex);
    if(!ar->zone_tex) glGenTextures(1,&ar->zone_tex);
    GLuint textures[]={ar->data_tex0,ar->data_tex1,ar->sat_tex,ar->zone_tex};
    bool uploaded=false;
    for(int i=0;i<count;i++) {
        MacroCellRect r=rects[i];
        if(flat&&!full) {
            r.x0=first_texel(r.x0,grid.origin_x,grid.span_x,grid.width);
            r.x1=first_texel(r.x1,grid.origin_x,grid.span_x,grid.width);
            r.z0=first_texel(r.z0,grid.origin_z,grid.span_z,grid.height);
            r.z1=first_texel(r.z1,grid.origin_z,grid.span_z,grid.height);
        }
        if(r.x0>=r.x1||r.z0>=r.z1) continue;
        if(!upload_rect(textures,w,&grid,r,allocate))return;
        uploaded=true;
    }
    if(uploaded)finish_textures(textures,flat,full);
    ar->flat_rect[0]=(float)grid.origin_x;ar->flat_rect[1]=(float)grid.origin_z;
    ar->flat_rect[2]=(float)grid.span_x;ar->flat_rect[3]=(float)grid.span_z;
    ar->tex_w=grid.width;ar->tex_h=grid.height;
    ar->synced_atlas_rev=w->atlas_revision;
    ar->synced_terr_rev=w->territory_revision;
    ar->synced_plane_count=planes;
    ar->world_upload_pending=false;
    ar->rain_key_day=UINT32_MAX;
    if(full)qlcg_atlas_tiles_invalidate(ar->tiles);
}

void qlcg_atlas_upload_world(AtlasRenderer *ar,const MacroWorld *w) {
    update_world(ar,w,true);
}

void qlcg_atlas_sync_world(AtlasRenderer *ar,const MacroWorld *w) {
    if(ar->detail_world!=w||ar->world_upload_pending||ar->synced_atlas_rev!=w->atlas_revision||
       (!w->chunk_claims&&ar->synced_terr_rev!=w->territory_revision)||
       (!qlcg_world_flat(w)&&(ar->tex_w!=w->columns||ar->tex_h!=w->rows))||
       ar->synced_plane_count!=(w->planes?w->planes->count:0)||!ar->tex_w||!ar->tex_h)
        update_world(ar,w,false);
}

/* 重置视图（全览或聚焦出生点） */
void qlcg_atlas_reset_view(AtlasRenderer *ar, const MacroWorld *w, bool focus_spawn) {
    if (!w) return;
    bool local = w->opt.play_mode == PLAY_MODE_SURVIVAL;
    if(qlcg_world_flat(w)) ar->planet_view=PLANET_VIEW_EQUIRECT;
    if (local) {
        ar->planet_view = PLANET_VIEW_EQUIRECT;
        ar->zoom = ar->target_zoom = 0.0f;
    } else if (!focus_spawn) {
        ar->zoom = ar->target_zoom = qlcg_atlas_overview_zoom(ar);
    }
    if (focus_spawn || local) {
        qlcg_atlas_center_on(ar, w, (w->spawn_cx + 0.5) * QLCG_CHUNK_SIZE_M,
                             (w->spawn_cz + 0.5) * QLCG_CHUNK_SIZE_M, true);
    } else {
        double b[4]={0};if(qlcg_world_flat(w))qlcg_infinite_bounds(w,b);
        qlcg_atlas_center_on(ar, w, (b[0]+b[2])*.5, (b[1]+b[3])*.5, true);
    }
}

/* 计算当前缩放级对应的像素/米比例 */

/* 图层本地化名称 */
const char *qlcg_atlas_layer_name(AtlasLayerId id) {
    switch (id) {
        case LAYER_TERRITORIES: return tr("layer.civilization");
        case LAYER_CHUNKS:      return tr("layer.chunks");
        case LAYER_MERIDIAN:    return tr("layer.baseline");
        case LAYER_DAYLIGHT:    return tr("layer.daylight");
        case LAYER_CLOUDS:      return tr("layer.rain");
        case LAYER_CONTOURS:    return tr("layer.contour");
        case LAYER_CLIMATE:     return tr("layer.climate");
        case LAYER_TEMPERATURE: return tr("layer.temperature");
        case LAYER_ELEVATION:   return tr("layer.height");
        case LAYER_ICE:         return tr("layer.ice");
        case LAYER_VEGETATION: return tr("layer.vegetation");
        case LAYER_DEPTH:       return tr("layer.depth");
        case LAYER_SOURCE_RIVERS: return tr("layer.sourceRivers");
        default:                return tr("layer.unknown");
    }
}

/* 快速筛查名称 */
const char *qlcg_atlas_filter_name(AtlasQuickFilter f) {
    switch ((int)f) {
        case 0:  return tr("atlas.filter.none");
        case 1:  return tr("atlas.filter.flag");
        case 2:  return tr("atlas.filter.owned");
        case 3:  return tr("atlas.filter.water");
        case 4:  return tr("atlas.filter.frozen");
        case 5:  return tr("atlas.filter.cold");
        case 6:  return tr("atlas.filter.temperate");
        case 7:  return tr("atlas.filter.warm");
        default: return tr("atlas.filter.unknown");
    }
}
