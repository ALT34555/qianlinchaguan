#include "paint_surface.h"
/* 山川单帧渲染：天空、地形、水面与天气 */
#include "terrain_view.h"
#include "terrain_visual.h"
#include "core/dev_tools.h"
#include "sky_climate.h"
#include "../systems/world/gen/gen_cover.h"
#include "../systems/world/model/plant_material.h"
#include "../systems/world/model/rock_material.h"
#include "terrain_types.h"
#include "terrain_visibility.h"
#include "terrain_metrics.h"
#include "terrain_ice_cells.h"
#include "water_reflection.h"
#include "../core/math_util.h"
#include "../core/measurements.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifndef GL_SAMPLE_ALPHA_TO_COVERAGE
#define GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#endif

typedef struct {
    float x,y,z,distance,seed;
    float strength,mist;
} SpringDraw;

static int collect_springs(const RegionData *c,float ox,float oz,const float cam[3],
                           SpringDraw *out,int capacity) {
    int count=0;
    if(c->water_form==WATER_FORM_CUTOFF||!c->source_effects) return 0;
    for(int i=0;i<c->spring_count&&count<capacity;i++) {
        SpringEmitter s=c->springs[i];
        float x=ox+s.x,z=oz+s.z;
        float distance=hypotf(x-cam[0],z-cam[2]);
        if(distance>=260) continue;
        float gx=(s.x+CHUNK_SIZE*.5f)/c->step,gz=(s.z+CHUNK_SIZE*.5f)/c->step;
        int ix=qlcg_clampi((int)gx,0,CHUNK_CELLS-1);
        int iz=qlcg_clampi((int)gz,0,CHUNK_CELLS-1);
        if(qlcg_cell_draws_ice(c,ix,iz)) continue;
        int idx=((int)floorf(gz+.5f)+1)*c->width+(int)floorf(gx+.5f)+1;
        if(!c->source_effects[idx].spring) continue;
        float y=qlcg_region_water_at(c,s.x,s.z);
        if(!isfinite(y)||y<=-1e8f) continue;
        distance=hypotf(distance,y-cam[1]);
        if(distance>=260) continue;
        out[count++]=(SpringDraw){x,y,z,distance,(s.seed&65535)/65536.0f,s.strength,s.mist};
    }
    return count;
}

void qlcg_terrain_render(TerrainRenderer *tr, const RegionData *reg, const MacroWorld *w,
                         const CalendarClock *clk, int utc_offset_min,
                         const GameSettings *s, int win_w, int win_h) {
    if (!reg || !reg->stream) return;
    if (clk) tr->weather_unix_ms = clk->unix_ms;

    WaterReflection reflection_choices[2]={{0}};
    RegionStream *st = reg->stream;
    float aspect = (win_h > 0) ? ((float)win_w / (float)win_h) : 1.77f;
    float fov_rad = (float)s->fov * (QLCG_PI / 180.0f);

    /* 昼夜与天气调色（对齐 base Sky） */
    const WeatherProfile *wp = tr->cur_weather ? tr->cur_weather : qlcg_get_weather_profile(0);
    float precip_eff = tr->precip_intensity;
    qlcg_dev_weather(&wp, &precip_eff);
    precip_eff = qlcg_maxf(precip_eff, tr->rain_override);
    if (!tr->reflection_pass || !tr->weather_visual.ready)
        qlcg_weather_visual_update(&tr->weather_visual, wp, precip_eff, qlcg_time_ms());
    wp = &tr->weather_visual.profile;
    precip_eff = tr->weather_visual.precipitation;

    AstraFrame astro = {0};
    bool astronomy = !tr->reflection_pass && w->opt.astronomy_enabled && qlcg_astra_upload(&tr->astronomy, &w->opt);
    if (astronomy) {
        double lon = ((double)reg->center_x + tr->x) / w->circumference * 6.283185307179586;
        double lat = -((double)reg->center_z + tr->z) / qlcg_world_depth(w) * 3.141592653589793;
        lat = fmax(-1.570796326794897, fmin(1.570796326794897, lat));
        qlcg_astra_sample(&w->opt, clk, lon, lat, tr->astronomy.catalog, &astro);
    }
    SkyClimate sky;
    if (tr->reflection_pass) {
        astronomy=tr->frame_has_astronomy;astro=tr->frame_astronomy;sky=tr->frame_sky;
    } else {
        qlcg_sky_climate_astra(clk, utc_offset_min, tr->plant_season, wp, s->weather_atmosphere,
                         w->sim_time, precip_eff, astronomy ? &astro : NULL, &sky);
        tr->frame_has_astronomy=astronomy;tr->frame_astronomy=astro;tr->frame_sky=sky;
    }
    float sx = sky.sun_dir[0], sy = sky.sun_dir[1], sz = sky.sun_dir[2];
    float sky_top[3] = { sky.zenith[0], sky.zenith[1], sky.zenith[2] };
    float sky_bot[3] = { sky.horizon[0], sky.horizon[1], sky.horizon[2] };

    /* base 水下雾与天空遮蔽 */
    float water_level=qlcg_region_water_at(reg,tr->x,tr->z);
    float bed_level=qlcg_region_height_at(reg,tr->x,tr->z);
    bool underwater=!tr->reflection_pass&&tr->y<water_level && tr->y>bed_level;
    if (underwater) {
        const float night[3]={8/255.0f,21/255.0f,35/255.0f};
        const float day[3]={22/255.0f,75/255.0f,96/255.0f};
        for (int k=0;k<3;k++) sky_top[k]=sky_bot[k]=qlcg_lerpf(night[k],day[k],sky.daylight);
    }

    float shake_x = (tr->cam_shake > 0.01f) ? sinf(w->sim_time * 42.0f) * tr->cam_shake * 0.35f : 0.0f;
    float shake_y = (tr->cam_shake > 0.01f) ? cosf(w->sim_time * 37.0f) * tr->cam_shake * 0.35f : 0.0f;

    float cp = cosf(tr->pitch), sp = sinf(tr->pitch);
    float cy = cosf(tr->yaw),   syaw = sinf(tr->yaw);
    float fwd[3] = { syaw * cp, sp, -cy * cp };
    float rgt[3] = { cy, 0.0f, syaw };
    float up[3]  = { -syaw * sp, cp, cy * sp };

    /* 相机坐标改用宏观格锚点基准，与地形顶点一致 */
    float cam_pos[3] = {
        (float)((double)tr->x + (double)reg->center_x - tr->anchor_x) + shake_x,
        tr->y + shake_y,
        (float)((double)tr->z + (double)reg->center_z - tr->anchor_z)
    };
    float proj[16], view[16], mvp[16];
    qlcg_mat4_perspective(proj, fov_rad, aspect, 0.15f, (st->radius+2)*CHUNK_SIZE*1.5f);
    qlcg_mat4_look_dir(view, cam_pos, tr->yaw, tr->pitch);
    if(tr->reflection_pass) qlcg_reflection_clip(proj,view,cam_pos[1],tr->reflection_plane);
    qlcg_mat4_mul(proj, view, mvp);
    if(tr->reflection_pass) memcpy(tr->reflection_targets[tr->reflection_pass-1].matrix,mvp,sizeof(mvp));

    TerrainFrustum frustum;qlcg_frustum_set(&frustum,mvp);
    /* 地形与水体复用相机相对投影 */
    float relative_mvp[16];
    view[12] = view[13] = view[14] = 0.0f;
    qlcg_mat4_mul(proj, view, relative_mvp);

    float fog_near=0.0f,fog_far=1.0f;
    qlcg_fog_range((float)st->radius*CHUNK_SIZE,wp,s->weather_atmosphere,&fog_near,&fog_far);
    TerrainVisualFrame visual;
    qlcg_visual_begin(tr,reg,s,&sky,wp,cam_pos,mvp,fog_near,fog_far,
        (st->radius+2)*CHUNK_SIZE*1.5f,w->sim_time,underwater,&visual);
    GLuint terrain_prog=qlcg_visual_program(tr,tr->terrain_prog,QL_VIS_TERRAIN);
    GLuint plant_prog=qlcg_visual_program(tr,tr->plant_prog,QL_VIS_PLANT);
    GLuint water_prog=qlcg_visual_program(tr,tr->water_prog,QL_VIS_WATER);
    GLuint prop_prog=qlcg_visual_program(tr,tr->prop_prog,QL_VIS_PROP);
    GLuint flag_prog=qlcg_visual_program(tr,tr->flag_prog,QL_VIS_FLAG);
    GLuint far_prog=qlcg_visual_program(tr,tr->far_prog,QL_VIS_FAR);

    if(!tr->reflection_pass)qlcg_profile_begin(QL_GPU_SKY);
    qlcg_visual_clouds_prepare(tr,reg,w);
    /* 1. 绘制天空穹顶 */
    glDisable(GL_DEPTH_TEST);
    if(!visual.paint) {
    qlcg_visual_bind(tr,tr->sky_prog);
    glUniform1i(qlcg_gl_uniform(tr->sky_prog, "uAstronomy"), astronomy);
    glUniform1i(qlcg_gl_uniform(tr->sky_prog, "uLunarPhase"), astronomy && wp->id < 900);
    if (astronomy) {
        qlcg_astra_uniforms(tr->sky_prog, &astro);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tr->astronomy.texture);
    }
    glUniform1i(qlcg_gl_uniform(tr->sky_prog,"uUnderwater"),underwater?1:0);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uCamFwd"), fwd[0], fwd[1], fwd[2]);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uCamRight"), rgt[0], rgt[1], rgt[2]);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uCamUp"), up[0], up[1], up[2]);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uTanHalfFov"), tanf(fov_rad * 0.5f));
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uAspect"), aspect);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uSunDir"), sx, sy, sz);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uHorizon"), sky_bot[0], sky_bot[1], sky_bot[2]);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uZenith"), sky_top[0], sky_top[1], sky_top[2]);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uGlowColor"), sky.glow[0], sky.glow[1], sky.glow[2]);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uGlow"), sky.glow_amt);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uMoonDir"), sky.moon_dir[0], sky.moon_dir[1], sky.moon_dir[2]);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uStar"), sky.star);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uMoon"), sky.moon);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uCloudColor"), sky.cloud[0], sky.cloud[1], sky.cloud[2]);
    glUniform3f(qlcg_gl_uniform(tr->sky_prog, "uRainCloudColor"), sky.rain_cloud[0], sky.rain_cloud[1], sky.rain_cloud[2]);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uRainAlpha"), s->weather_atmosphere ? sky.rain_alpha : 0.0f);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uFlash"), sky.flash);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uCloudDaylight"),sky.daylight);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uCloudDusk"),sky.dusk);
    qlcg_visual_clouds_bind(tr,tr->sky_prog);
    glUniform1f(qlcg_gl_uniform(tr->sky_prog, "uSimTime"), w->sim_time);
    }
    if (!qlcg_dev_skip_draw(QLCG_DEV_SKIP_SKY)) {
        if(!qlcg_visual_sky(tr,fwd,rgt,up,aspect,tanf(fov_rad*0.5f),astronomy?&astro:NULL)) {
            glBindVertexArray(tr->sky_vao);
            glDrawArrays(GL_TRIANGLES,0,6);
        }
        if (astronomy && !underwater) qlcg_astra_draw(&tr->astronomy, &astro, &sky, fwd, rgt, up,
            aspect, tanf(fov_rad * .5f), w->sim_time, s->decorative_clouds ? sky.cloud_cov : 0,
            s->weather_atmosphere ? sky.rain_alpha : 0);
        if(astronomy && !underwater) qlcg_astra_draw_events(&tr->astronomy,w->astra_events,clk,&astro);
    }

    if(!tr->reflection_pass){qlcg_profile_end(QL_GPU_SKY);qlcg_profile_begin(QL_GPU_TERRAIN);}
    /* 2. 绘制视距圆内地形 */
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    qlcg_visual_bind(tr,terrain_prog);
    glUniform1i(qlcg_gl_uniform(terrain_prog,"uUnderwater"),underwater?1:0);
    glUniformMatrix4fv(qlcg_gl_uniform(terrain_prog, "uMVP"), 1, GL_FALSE, relative_mvp);
    glUniform3f(qlcg_gl_uniform(terrain_prog, "uSunDir"), sx, sy, sz);
    glUniform3f(qlcg_gl_uniform(terrain_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
    if(!visual.paint) glUniform3f(qlcg_gl_uniform(terrain_prog, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
    glUniform2f(qlcg_gl_uniform(terrain_prog, "uFogRange"), fog_near, fog_far);
    if(!visual.paint) glUniform1f(qlcg_gl_uniform(terrain_prog, "uLightScale"), sky.light_scale);
    if(!visual.paint) glUniform3f(qlcg_gl_uniform(terrain_prog, "uTint"), sky.tint[0], sky.tint[1], sky.tint[2]);
    if(!visual.paint) {
    glUniform1f(qlcg_gl_uniform(terrain_prog, "uGroundDetail"), 1.0f);
    /* 按就地温度选相邻两个气候带纹理，带内连续插值 */
    float bp = 2.0f;
    if (reg->temperatures && reg->width > 0 && reg->height > 0) {
        int tmid = (reg->height / 2) * reg->width + reg->width / 2;
        bp = qlcg_cover_band_of_temp(reg->temperatures[tmid]);
    }
    int b0 = qlcg_clampi((int)bp, 0, QLCG_GROUND_BAND_N - 1);
    int b1 = qlcg_clampi(b0 + 1, 0, QLCG_GROUND_BAND_N - 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tr->ground_tex[b0]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tr->ground_tex[b1]);
    glActiveTexture(GL_TEXTURE0);
    glUniform1f(qlcg_gl_uniform(terrain_prog, "uGroundMix"),
                qlcg_clampf(bp - (float)b0, 0.0f, 1.0f));
    glUniform2f(qlcg_gl_uniform(terrain_prog, "uGroundAnchor"),
                (float)fmod(tr->anchor_x, (double)QLCG_GROUND_TEX_SPAN),
                (float)fmod(tr->anchor_z, (double)QLCG_GROUND_TEX_SPAN));
    glUniform1f(qlcg_gl_uniform(terrain_prog, "uGroundSpan"), QLCG_GROUND_TEX_SPAN);
    float b_rad = (tr->eng_tool != ENG_TOOL_NONE && tr->has_hover) ? tr->brush_radius : 0.0f;
    glUniform3f(qlcg_gl_uniform(terrain_prog, "uBrushPos"), tr->hover_lx, tr->hover_ly, tr->hover_lz);
    glUniform1f(qlcg_gl_uniform(terrain_prog, "uBrushRad"), b_rad);

    }
    /* 合批：可见块一次 baseVertex 多重绘制 */
    const int vpb = (CHUNK_CELLS + 1) * (CHUNK_CELLS + 1);
    int md_n = 0;
    glUniform1i(qlcg_gl_uniform(terrain_prog,"uCliffMaterial"),1);
    for (int i = 0; i < st->count; i++) {
        int slot = st->active[i];
        RegionData *c = st->slots[slot].data;
        if (!tr->chunks[slot].terrain_ready) continue;
        int dx = c->center_cx-st->center_cx, dz = c->center_cz-st->center_cz;
        if (dx*dx+dz*dz > (st->radius+0.5f)*(st->radius+0.5f)) continue;
        float wx = (float)((double)c->center_x - tr->anchor_x) - cam_pos[0];
        float wz = (float)((double)c->center_z - tr->anchor_z) - cam_pos[2];
        float wl = sqrtf(wx * wx + wz * wz);
        if(s->mirror_reflections&&!tr->reflection_pass&&!underwater)
            qlcg_reflection_collect(reflection_choices,tr->reflection_targets,c,tr->chunks+slot,
                                    wx+cam_pos[0],wz+cam_pos[2],cam_pos,fwd,mvp);
        if(!qlcg_frustum_bounds(&frustum,&tr->chunks[slot].bounds)){qlcg_metrics.culled_chunks++;continue;}
        bool far_lod = wl>220.0f && !tr->chunks[slot].cliff_detail;
        tr->md_count[md_n] = far_lod ? tr->terrain_lod_count : tr->terrain_idx_count;
        tr->md_index[md_n] = far_lod ? (GLvoid *)(size_t)(tr->terrain_lod_off * (int)sizeof(uint32_t))
                                     : (GLvoid *)0;
        tr->md_base[md_n] = tr->chunks[slot].terrain_slot * vpb;
        md_n++;
        if (tr->chunks[slot].cliff_vertex_count && !qlcg_dev_skip_draw(QLCG_DEV_SKIP_TERRAIN)) {
            glBindVertexArray(tr->chunks[slot].cliff_vao);
            glDrawArrays(GL_TRIANGLES,0,tr->chunks[slot].cliff_vertex_count);
        }
    }
    glUniform1i(qlcg_gl_uniform(terrain_prog,"uCliffMaterial"),0);
    if (md_n > 0 && !qlcg_dev_skip_draw(QLCG_DEV_SKIP_TERRAIN)) {
        glBindVertexArray(tr->terrain_vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tr->terrain_ebo);
        if (glMultiDrawElementsBaseVertex) {
            glMultiDrawElementsBaseVertex(GL_TRIANGLES,tr->md_count,GL_UNSIGNED_INT,(const void *const *)tr->md_index,md_n,tr->md_base);
        } else {
            glBindBuffer(GL_ARRAY_BUFFER,tr->terrain_vbo);
            for(int draw=0;draw<md_n;draw++) {
                size_t base=(size_t)tr->md_base[draw]*sizeof(TerrainVert);
                qlcg_terrain_vertex_layout(base);
                glDrawElements(GL_TRIANGLES,tr->md_count[draw],GL_UNSIGNED_INT,tr->md_index[draw]);
            }
            qlcg_terrain_vertex_layout(0);
        }
    }

    if(!tr->reflection_pass){qlcg_profile_end(QL_GPU_TERRAIN);qlcg_profile_begin(QL_GPU_PROPS);}
    /* 3. 绘制硬件实例化植被、岩石与领地旗帜 */
    if (qlcg_dev_skip_draw(QLCG_DEV_SKIP_PROP)) goto skip_props;
    qlcg_visual_bind(tr,prop_prog);
    glUniform1i(qlcg_gl_uniform(prop_prog,"uUnderwater"),underwater?1:0);
    glUniformMatrix4fv(qlcg_gl_uniform(prop_prog, "uMVP"), 1, GL_FALSE, mvp);
    if(!visual.paint) glUniform3f(qlcg_gl_uniform(prop_prog, "uSunDir"), sx, sy, sz);
    glUniform3f(qlcg_gl_uniform(prop_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
    if(!visual.paint) glUniform3f(qlcg_gl_uniform(prop_prog, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
    glUniform2f(qlcg_gl_uniform(prop_prog, "uFogRange"), fog_near, fog_far);
    if(!visual.paint) glUniform1f(qlcg_gl_uniform(prop_prog, "uLightScale"), sky.light_scale);
    if(!visual.paint) glUniform3f(qlcg_gl_uniform(prop_prog, "uTint"), sky.tint[0], sky.tint[1], sky.tint[2]);
    glUniform1f(qlcg_gl_uniform(prop_prog, "uTime"), w->sim_time);
    glUniform1f(qlcg_gl_uniform(prop_prog, "uWind"), qlcg_maxf(tr->wind_strength, wp->wind));

    if (tr->tree_inst_count > 0) {
        glBindVertexArray(tr->tree_vao);
        glDrawElementsInstanced(GL_TRIANGLES, tr->tree_mesh_indices, GL_UNSIGNED_SHORT, 0, tr->tree_inst_count);
    }
    if (tr->rock_inst_count > 0) {
        glBindVertexArray(tr->rock_vao);
        glDrawElementsInstanced(GL_TRIANGLES, tr->rock_mesh_indices, GL_UNSIGNED_SHORT, 0, tr->rock_inst_count);
    }
    if (tr->flag_inst_count > 0) {
        qlcg_visual_bind(tr,flag_prog);
        glUniform1i(qlcg_gl_uniform(flag_prog,"uUnderwater"),underwater?1:0);
        glUniformMatrix4fv(qlcg_gl_uniform(flag_prog, "uMVP"), 1, GL_FALSE, mvp);
        if(!visual.paint) glUniform3f(qlcg_gl_uniform(flag_prog, "uSunDir"), sx, sy, sz);
        glUniform3f(qlcg_gl_uniform(flag_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
        if(!visual.paint) glUniform3f(qlcg_gl_uniform(flag_prog, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
        glUniform2f(qlcg_gl_uniform(flag_prog, "uFogRange"), fog_near, fog_far);
        if(!visual.paint) glUniform1f(qlcg_gl_uniform(flag_prog, "uLightScale"), sky.light_scale);
        if(!visual.paint) glUniform3f(qlcg_gl_uniform(flag_prog, "uTint"), sky.tint[0], sky.tint[1], sky.tint[2]);
        glUniform1f(qlcg_gl_uniform(flag_prog, "uTime"), w->sim_time);
        glUniform1f(qlcg_gl_uniform(flag_prog, "uEnable"), s->natural_physics ? 1.0f : 0.0f);
        glBindVertexArray(tr->flag_vao);
        glDrawElementsInstanced(GL_TRIANGLES, tr->flag_mesh_indices, GL_UNSIGNED_SHORT, 0, tr->flag_inst_count);
    }

    /* 植物：真实网格按物种分批 */
    bool plant_bound = false;
    if (tr->plant_batch_count > 0 && tr->plant_inst_total > 0) {
        qlcg_visual_bind(tr,plant_prog);
        plant_bound = true;
        glUniform1i(qlcg_gl_uniform(plant_prog,"uUnderwater"),underwater?1:0);
        glUniformMatrix4fv(qlcg_gl_uniform(plant_prog, "uMVP"), 1, GL_FALSE, relative_mvp);
        if(!visual.paint) glUniform3f(qlcg_gl_uniform(plant_prog, "uSunDir"), sx, sy, sz);
        glUniform3f(qlcg_gl_uniform(plant_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
        if(!visual.paint) glUniform3f(qlcg_gl_uniform(plant_prog, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
        glUniform2f(qlcg_gl_uniform(plant_prog, "uFogRange"), fog_near, fog_far);
        if(!visual.paint) glUniform1f(qlcg_gl_uniform(plant_prog, "uLightScale"), sky.light_scale);
        if(!visual.paint) glUniform3f(qlcg_gl_uniform(plant_prog, "uTint"), sky.tint[0], sky.tint[1], sky.tint[2]);
        glUniform1f(qlcg_gl_uniform(plant_prog, "uTime"), w->sim_time);
        glUniform1f(qlcg_gl_uniform(plant_prog, "uEnable"), s->natural_physics ? 1.0f : 0.0f);
        glUniform1i(qlcg_gl_uniform(plant_prog,"uIsRock"),0);
        GLint is_cao=qlcg_gl_uniform(plant_prog,"uIsCao"),height=qlcg_gl_uniform(plant_prog,"uHeight");
        GLint shrub=qlcg_gl_uniform(plant_prog,"uShrub"),grass=qlcg_gl_uniform(plant_prog,"uGrass");
        for (int bi = 0; bi < tr->plant_batch_count; bi++) {
            PlantBatch *pb = &tr->plants[bi];
            if (pb->inst_count <= 0 || !pb->vao) continue;
            int visible=qlcg_batch_visible(pb,mvp,&frustum,tr->reflection_pass);
            if(!visible)continue;
            glUniform1i(is_cao,pb->category==PLANT_CAT_CAO);
            glUniform1f(height,pb->height);
            glUniform1f(shrub,pb->category==PLANT_CAT_GUAN?1.0f:0.0f);
            glUniform1f(grass,pb->category==PLANT_CAT_CAO?1.0f:0.0f);
            if(visible)glDrawElementsInstanced(GL_TRIANGLES, pb->idx_count, GL_UNSIGNED_INT, 0, visible);
        }
    }

    /* 岩石：真实网格按原型分批 */
    if (tr->rock_batch_count > 0 && tr->rock_inst_total > 0) {
        if (!plant_bound) {
            qlcg_visual_bind(tr,plant_prog);
            glUniform1i(qlcg_gl_uniform(plant_prog,"uUnderwater"),underwater?1:0);
            glUniformMatrix4fv(qlcg_gl_uniform(plant_prog, "uMVP"), 1, GL_FALSE, relative_mvp);
            if(!visual.paint) glUniform3f(qlcg_gl_uniform(plant_prog, "uSunDir"), sx, sy, sz);
            glUniform3f(qlcg_gl_uniform(plant_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
            if(!visual.paint) glUniform3f(qlcg_gl_uniform(plant_prog, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
            glUniform2f(qlcg_gl_uniform(plant_prog, "uFogRange"), fog_near, fog_far);
            if(!visual.paint) glUniform1f(qlcg_gl_uniform(plant_prog, "uLightScale"), sky.light_scale);
            glUniform1f(qlcg_gl_uniform(plant_prog, "uTime"), w->sim_time);
            if(!visual.paint) glUniform3f(qlcg_gl_uniform(plant_prog, "uTint"), sky.tint[0], sky.tint[1], sky.tint[2]);
        }
        glUniform1f(qlcg_gl_uniform(plant_prog, "uEnable"), 0.0f);
        glUniform1f(qlcg_gl_uniform(plant_prog, "uShrub"), 0.0f);
        glUniform1f(qlcg_gl_uniform(plant_prog, "uGrass"), 0.0f);
        glUniform1f(qlcg_gl_uniform(plant_prog, "uHeight"), 1.0f);
        glUniform1i(qlcg_gl_uniform(plant_prog,"uIsRock"),1);
        glUniform1i(qlcg_gl_uniform(plant_prog,"uIsCao"),0);
        for (int bi = 0; bi < tr->rock_batch_count; bi++) {
            PlantBatch *rb = &tr->rocks[bi];
            if (rb->inst_count <= 0 || !rb->vao) continue;
            int visible=qlcg_batch_visible(rb,mvp,&frustum,tr->reflection_pass);
            if(visible)glDrawElementsInstanced(GL_TRIANGLES, rb->idx_count, GL_UNSIGNED_INT, 0, visible);
        }
    }
    /* 远景贴图：12–18 交叉面片，18 外朝向玩家 */
    if (far_prog && qlcg_prop_sprites_can_far(&tr->sprites) &&
        (tr->far_cross_count > 0 || tr->far_bill_count > 0)) {
        float cr_x = cosf(tr->yaw), cr_z = sinf(tr->yaw);
        qlcg_visual_bind(tr,far_prog);
        glUniform1i(qlcg_gl_uniform(far_prog, "uUnderwater"), underwater ? 1 : 0);
        glUniform1i(qlcg_gl_uniform(far_prog, "uMsaa"), tr->msaa_active ? 1 : 0);
        glUniformMatrix4fv(qlcg_gl_uniform(far_prog, "uMVP"), 1, GL_FALSE, relative_mvp);
        glUniform3f(qlcg_gl_uniform(far_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
        glUniform3f(qlcg_gl_uniform(far_prog, "uCamRight"), cr_x, 0.0f, cr_z);
        glUniform1f(qlcg_gl_uniform(far_prog, "uNow"),
                    tr->fade_origin_ms > 0.0
                        ? (float)((qlcg_time_ms() - tr->fade_origin_ms) * 0.001)
                        : 0.0f);
        if(!visual.paint) glUniform3f(qlcg_gl_uniform(far_prog, "uSunDir"), sx, sy, sz);
        if(!visual.paint) glUniform3f(qlcg_gl_uniform(far_prog, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
        glUniform2f(qlcg_gl_uniform(far_prog, "uFogRange"), fog_near, fog_far);
        if(!visual.paint) glUniform3f(qlcg_gl_uniform(far_prog, "uTint"), sky.tint[0], sky.tint[1], sky.tint[2]);
        if(!visual.paint) glUniform1f(qlcg_gl_uniform(far_prog, "uLightScale"), sky.light_scale);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tr->sprites.tex);
        if(!visual.paint) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        if (tr->far_cross_count > 0) {
            glUniform1i(qlcg_gl_uniform(far_prog, "uMode"), 0);
            glBindVertexArray(tr->far_cross_vao);
            glDrawElementsInstanced(GL_TRIANGLES, tr->far_cross_idx, GL_UNSIGNED_SHORT, 0,
                                    qlcg_far_visible(tr,0,mvp,&frustum,tr->reflection_pass));
        }
        if (tr->far_bill_count > 0) {
            glUniform1i(qlcg_gl_uniform(far_prog, "uMode"), 1);
            glBindVertexArray(tr->far_bill_vao);
            glDrawElementsInstanced(GL_TRIANGLES, tr->far_bill_idx, GL_UNSIGNED_SHORT, 0,
                                    qlcg_far_visible(tr,1,mvp,&frustum,tr->reflection_pass));
        }
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }
skip_props:
    if(tr->reflection_pass) return;
    qlcg_profile_end(QL_GPU_PROPS);qlcg_profile_begin(QL_GPU_REFLECTION);
    qlcg_reflection_render(tr,reflection_choices,reg,w,clk,utc_offset_min,s,win_w,win_h);
    qlcg_profile_end(QL_GPU_REFLECTION);
    qlcg_visual_resume(tr,&visual);
    qlcg_profile_begin(QL_GPU_WATER);

    /* 4. 绘制动态水体与冰壳 */
    if (!qlcg_dev_skip_draw(QLCG_DEV_SKIP_WATER)) {
    /* 4a. 冰顶：不透明，先画在开水之前，避免半透明叠色 */
    if (!qlcg_dev_skip_draw(QLCG_DEV_SKIP_ICE) && tr->ice_vis.ice_top_prog) {
        GLuint ip=qlcg_visual_program(tr,tr->ice_vis.ice_top_prog,QL_VIS_ICE);
        QLCG_DEV_ONLY(int dbg_draw = 0;)
        qlcg_visual_bind(tr,ip);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDisable(GL_CULL_FACE);
        /* 冰顶与水共面：多边形偏移稳定压住水面，消除远处闪烁 */
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -8.0f);
        glUniform1i(qlcg_gl_uniform(ip,"uUnderwater"),underwater?1:0);
        glUniformMatrix4fv(qlcg_gl_uniform(ip, "uMVP"), 1, GL_FALSE, relative_mvp);
        glUniform3f(qlcg_gl_uniform(ip, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
        glUniform3f(qlcg_gl_uniform(ip, "uSunDir"), sx, sy, sz);
        glUniform3f(qlcg_gl_uniform(ip, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
        glUniform3f(qlcg_gl_uniform(ip, "uZenith"), sky_top[0], sky_top[1], sky_top[2]);
        glUniform2f(qlcg_gl_uniform(ip, "uFogRange"), fog_near, fog_far);
        glUniform1f(qlcg_gl_uniform(ip, "uLightScale"), sky.light_scale);
        glUniform3f(qlcg_gl_uniform(ip, "uTint"), sky.tint[0], sky.tint[1], sky.tint[2]);
        glUniform1f(qlcg_gl_uniform(ip, "uLift"), tr->ice_lift);
        GLint u_bottom = qlcg_gl_uniform(ip, "uBottom");
        GLint u_off = qlcg_gl_uniform(ip, "uChunkOffset");
        for (int pass = 0; pass < 2; pass++) {
            glUniform1i(u_bottom, pass);
            for (int i = 0; i < st->count; i++) {
                int slot = st->active[i];
                RegionData *c = st->slots[slot].data;
                TerrainChunkGPU *g = &tr->chunks[slot];
                if (g->ice_idx_count <= 0) continue;
                if(!qlcg_frustum_bounds(&frustum,&g->bounds))continue;
                int ddx = c->center_cx-st->center_cx, ddz = c->center_cz-st->center_cz;
                if (ddx*ddx+ddz*ddz > (st->radius+0.5f)*(st->radius+0.5f)) continue;
                float cxw = (float)((double)c->center_x - tr->anchor_x);
                float czw = (float)((double)c->center_z - tr->anchor_z);
                float d2 = (cxw-cam_pos[0])*(cxw-cam_pos[0]) + (czw-cam_pos[2])*(czw-cam_pos[2]);
                if (pass == 1 && !underwater && d2 > 60.0f*60.0f) continue;
                /* 冰底只在相机低于该块水面时才可能看见 */
                if (pass == 1) {
                    int mid = (c->height / 2) * c->width + c->width / 2;
                    float wl_c = c->water_levels ? c->water_levels[mid] : -1e9f;
                    if (cam_pos[1] > wl_c + 0.05f) continue;
                }
                glUniform3f(u_off, cxw, 0.0f, czw);
                glBindVertexArray(g->water_vao);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g->ice_ebo);
                glDrawElements(GL_TRIANGLES, g->ice_idx_count, GL_UNSIGNED_INT, 0);
                QLCG_DEV_ONLY(dbg_draw++;)
            }
        }
        qlcg_dev_ice_pass(dbg_draw, underwater, tr->ice_lift);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if(visual.paint) glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
    qlcg_visual_bind(tr,water_prog);
    qlcg_reflection_uniforms(tr);
    glUniform1f(qlcg_gl_uniform(water_prog,"uSkyReflection"),s->water_reflections?1.0f:0.0f);
    glUniform1i(qlcg_gl_uniform(water_prog,"uWaterDetail"),s->graphics_preset>=PRESET_HIGH);
    glUniform1i(qlcg_gl_uniform(water_prog,"uUnderwater"),underwater?1:0);
    glUniform2f(qlcg_gl_uniform(water_prog,"uWorldOrigin"),fmod(reg->center_x,4096.0),fmod(reg->center_z,4096.0));
    glUniformMatrix4fv(qlcg_gl_uniform(water_prog, "uMVP"), 1, GL_FALSE, relative_mvp);
    glUniform3f(qlcg_gl_uniform(water_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
    if(!visual.paint) glUniform3f(qlcg_gl_uniform(water_prog, "uSunDir"), sx, sy, sz);
    if(!visual.paint) glUniform3f(qlcg_gl_uniform(water_prog, "uFogCol"), sky_bot[0], sky_bot[1], sky_bot[2]);
    glUniform2f(qlcg_gl_uniform(water_prog, "uFogRange"), fog_near, fog_far);
    if(!visual.paint) glUniform1f(qlcg_gl_uniform(water_prog, "uLightScale"), sky.light_scale);
    if(!visual.paint) glUniform3f(qlcg_gl_uniform(water_prog, "uZenith"), sky_top[0], sky_top[1], sky_top[2]);
    glUniform1f(qlcg_gl_uniform(water_prog, "uTime"), w->sim_time);
    glUniform1f(qlcg_gl_uniform(water_prog, "uWaveAmp"), s->water_waves ? 1.0f : 0.0f);
    SpringDraw falls[16];int fall_count=0;
    GLint water_offset = qlcg_gl_uniform(water_prog, "uChunkOffset");
    GLint water_form = qlcg_gl_uniform(water_prog, "uWaterForm");
    SpringDraw springs[16];
    int spring_count=0;
    glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1.0f,-8.0f);
    for (int i = 0; i < st->count; i++) {
        int slot = st->active[i];
        RegionData *c = st->slots[slot].data;
        if (tr->chunks[slot].water_idx_count <= 0) continue;
        if(!qlcg_frustum_bounds(&frustum,&tr->chunks[slot].bounds))continue;
        int dx = c->center_cx-st->center_cx, dz = c->center_cz-st->center_cz;
        if (dx*dx+dz*dz > (st->radius+0.5f)*(st->radius+0.5f)) continue;
        float ox=(float)((double)c->center_x-tr->anchor_x);
        float oz=(float)((double)c->center_z-tr->anchor_z);
        glUniform3f(water_offset,ox,0,oz);
        glUniform1i(water_form,(int)c->water_form);
        glBindVertexArray(tr->chunks[slot].water_vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tr->chunks[slot].water_ebo);
        glDrawElements(GL_TRIANGLES, tr->chunks[slot].water_idx_count, GL_UNSIGNED_INT, 0);
        if(c->water_form!=WATER_FORM_CUTOFF && (s->particles||s->water_mist) && !underwater) {
            for(int j=0;j<4 && fall_count+spring_count<16;j++) {
                const float *e=tr->chunks[slot].fall_emitters[j];
                if(e[3]<=1) continue;
                int ix=qlcg_clampi((int)((e[0]+16)/2),0,15),iz=qlcg_clampi((int)((e[2]+16)/2),0,15);
                if(qlcg_cell_draws_ice(c,ix,iz)) continue;
                float distance=hypotf(hypotf(ox+e[0]-cam_pos[0],oz+e[2]-cam_pos[2]),e[1]-cam_pos[1]);
                if(distance<420) falls[fall_count++]=(SpringDraw){ox+e[0],e[1],oz+e[2],distance,
                    (qlcg_hash_u32((uint32_t)c->center_cx*73856093u^(uint32_t)c->center_cz*19349663u^j)&65535)/65536.0f};
            }
        }
        if((s->particles||s->water_mist)&&!underwater&&c->spring_count)
            spring_count+=collect_springs(c,ox,oz,cam_pos,springs+spring_count,16-spring_count-fall_count);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    if((s->particles||s->water_mist) && !underwater && tr->spring_prog) {
        GLuint program=qlcg_visual_program(tr,tr->spring_prog,QL_VIS_SPRING);
        qlcg_gl_program_point_size(true);glDepthMask(GL_FALSE);qlcg_visual_bind(tr,program);
        glUniformMatrix4fv(qlcg_gl_uniform(program,"uMVP"),1,GL_FALSE,mvp);
        glUniform1f(qlcg_gl_uniform(program,"uTime"),w->sim_time);
        glUniform1f(qlcg_gl_uniform(program,"uLight"),sky.light_scale);
        glUniform1f(qlcg_gl_uniform(program,"uFallSpray"),1);
        glUniform1f(qlcg_gl_uniform(program,"uPixels"),win_h);
        glUniform1f(qlcg_gl_uniform(program,"uMist"),s->water_mist?1:0);
        glUniform1f(qlcg_gl_uniform(program,"uDrops"),s->particles?1:0);
        glUniform1f(qlcg_gl_uniform(program,"uStrength"),1);
        GLint u_origin=qlcg_gl_uniform(program,"uOrigin");
        GLint u_seed=qlcg_gl_uniform(program,"uSeed");
        GLint u_distance=qlcg_gl_uniform(program,"uDistance");
        glBindVertexArray(tr->weather_vao);
        for(int i=0;i<fall_count;i++) {
            SpringDraw a=falls[i];
            glUniform3f(u_origin,a.x,a.y+.12f,a.z);
            glUniform1f(u_seed,a.seed);
            glUniform1f(u_distance,a.distance);
            glDrawArrays(GL_POINTS,0,72);
        }
        glUniform1f(qlcg_gl_uniform(program,"uFallSpray"),0);
        glDepthMask(GL_TRUE);qlcg_gl_program_point_size(false);
    }
    if(spring_count&&tr->spring_prog) {
        GLuint program=qlcg_visual_program(tr,tr->spring_prog,QL_VIS_SPRING);
        qlcg_gl_program_point_size(true);
        glDepthMask(GL_FALSE);
        qlcg_visual_bind(tr,program);
        glUniformMatrix4fv(qlcg_gl_uniform(program,"uMVP"),1,GL_FALSE,mvp);
        glUniform1f(qlcg_gl_uniform(program,"uTime"),w->sim_time);
        glUniform1f(qlcg_gl_uniform(program,"uLight"),sky.light_scale);
        glUniform1f(qlcg_gl_uniform(program,"uPixels"),win_h);
        glUniform1f(qlcg_gl_uniform(program,"uDrops"),s->particles?1:0);
        glUniform1f(qlcg_gl_uniform(program,"uFallSpray"),.25f);
        GLint u_mist=qlcg_gl_uniform(program,"uMist");
        GLint u_strength=qlcg_gl_uniform(program,"uStrength");
        GLint u_origin=qlcg_gl_uniform(program,"uOrigin");
        GLint u_seed=qlcg_gl_uniform(program,"uSeed");
        GLint u_distance=qlcg_gl_uniform(program,"uDistance");
        glBindVertexArray(tr->weather_vao);
        for(int i=0;i<spring_count;i++) {
            SpringDraw a=springs[i];
            glUniform1f(u_mist,s->water_mist?a.mist:0);
            glUniform1f(u_strength,a.strength);
            glUniform3f(u_origin,a.x,a.y,a.z);
            glUniform1f(u_seed,a.seed);
            glUniform1f(u_distance,a.distance);
            glDrawArrays(GL_POINTS,0,72);
        }
        glDepthMask(GL_TRUE);
        qlcg_gl_program_point_size(false);
    }

    /* 4b. 浮冰、破冰碎片与裂纹 */
    if (!qlcg_dev_skip_draw(QLCG_DEV_SKIP_ICE)) {
        static const float tint_default[3] = {1.0f, 1.0f, 1.0f};
        const float *tnt = sky.tint;
        if (!tnt) tnt = tint_default;
        qlcg_ice_visual_draw_flow(tr, mvp, cam_pos, sky_bot, fog_near, fog_far, sky.light_scale,
                                  (const float *)&sky.sun_dir[0], tnt, w->sim_time);
        qlcg_ice_visual_draw_cracks(tr, mvp, cam_pos, sky_bot, fog_near, fog_far, sky.light_scale);
    }
    }

    qlcg_visual_strokes(tr,reg);

    /* 5. 绘制雨/雪/冰雹/沙尘天气粒子 */
    float p_rain = qlcg_maxf(wp->rain, tr->rain_override);
    float p_snow = wp->snow;
    float p_hail = wp->hail;
    float p_dust = wp->dust;
    float p_any = qlcg_maxf(qlcg_maxf(p_rain, p_snow), qlcg_maxf(p_hail, p_dust));
    if (!underwater && s->particles && p_any > 0.02f) {
        int limit = s->weather_atmosphere ? 1200 : 240;
        static const float RAIN_COL[3] = { 0.76f, 0.84f, 0.94f };
        static const float SNOW_COL[3] = { 0.96f, 0.98f, 1.00f };
        static const float HAIL_COL[3] = { 1.00f, 1.00f, 1.00f };
        static const float DUST_COL[3] = { 0.75f, 0.76f, 0.70f };
        static const float SAND_COL[3] = { 0.80f, 0.69f, 0.48f };
        static const float INK_RAIN[3] = { 0.22f, 0.20f, 0.18f };
        static const float INK_DUST[3] = { 0.58f, 0.56f, 0.53f };
        bool ink_weather=visual.paint&&s->scroll_palette==2;
        qlcg_gl_program_point_size(true);
        GLuint weather_prog=qlcg_visual_program(tr,tr->weather_prog,QL_VIS_WEATHER);
        qlcg_visual_bind(tr,weather_prog);
        glUniformMatrix4fv(qlcg_gl_uniform(weather_prog, "uMVP"), 1, GL_FALSE, mvp);
        glUniform3f(qlcg_gl_uniform(weather_prog, "uCamPos"), cam_pos[0], cam_pos[1], cam_pos[2]);
        glUniform1f(qlcg_gl_uniform(weather_prog, "uTime"), w->sim_time);
        glUniform1f(qlcg_gl_uniform(weather_prog, "uWind"), wp->wind);
        glUniform1f(qlcg_gl_uniform(weather_prog, "uVortex"), wp->vortex ? 1.0f : 0.0f);
        GLint u_fall = qlcg_gl_uniform(weather_prog, "uFall");
        GLint u_size = qlcg_gl_uniform(weather_prog, "uSize");
        GLint u_alpha = qlcg_gl_uniform(weather_prog, "uAlpha");
        GLint u_streak = qlcg_gl_uniform(weather_prog, "uStreak");
        GLint u_part_col = qlcg_gl_uniform(weather_prog, "uPartCol");
        glBindVertexArray(tr->weather_vao);
        struct { float rate, fall, size, alpha, streak; const float *col; } passes[4] = {
            { p_rain, 16.0f, 4.0f, 0.60f, 1.0f, ink_weather ? INK_RAIN : RAIN_COL },
            { p_snow,  3.5f, 4.2f, 0.80f, 0.0f, SNOW_COL },
            { p_hail, 10.0f, 4.6f, 0.90f, 0.0f, HAIL_COL },
            { p_dust,  2.0f, 2.4f, 0.50f, 0.0f, ink_weather ? INK_DUST : ((wp->id == 304) ? SAND_COL : DUST_COL) },
        };
        for (int i = 0; i < 4; i++) {
            int cnt = qlcg_clampi((int)(passes[i].rate * (float)limit), 0, limit);
            if (cnt <= 0) continue;
            glUniform1f(u_fall, passes[i].fall);
            glUniform1f(u_size, passes[i].size);
            glUniform1f(u_alpha, passes[i].alpha);
            glUniform1f(u_streak, passes[i].streak);
            glUniform3f(u_part_col,
                        passes[i].col[0], passes[i].col[1], passes[i].col[2]);
            glDrawArrays(GL_POINTS, 0, cnt);
        }
    }
    qlcg_profile_end(QL_GPU_WATER);
    qlcg_visual_end(tr);
    qlcg_profile_begin(QL_GPU_UI);
}
