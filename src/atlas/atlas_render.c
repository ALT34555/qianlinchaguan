/* 舆图按存档顺序合成图层 */
#include "atlas_view.h"
#include "../systems/world/astronomy.h"
#include "atlas_tiles.h"
#include "atlas_rain.h"
#include "../systems/world/world_infinite.h"
#include "../ui/ui_widgets.h"
#include "../ui/ui_sprite.h"
#include "../systems/world/faction_naming.h"
#include "../core/math_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* 中景隔级抽稀，近景保留四倍距 */
float qlcg_atlas_contour_unit(int level, int columns) {
    static const float units[3][12] = {
        {240,240,180,128,96,64,48,32,32,24,24,16},
        {560,480,480,360,360,320,320,240,240,120,120,100},
        {1024,960,720,512,400,320,320,256,256,128,128,64}
    };
    int size = columns <= 256 ? 0 : columns <= 512 ? 1 : 2;
    int l = qlcg_clampi(level, 4, QLCG_MAX_ATLAS_ZOOM);
    float u = units[size][qlcg_clampi(l, 4, 15) - 4];
    if (l > 15) u = ldexpf(u, 15 - l + qlcg_clampi((l - 14) / 2, 1, 2));
    return u;
}

/* 密度倍率与下限（与着色器一致） */
float qlcg_atlas_contour_step(int level, int columns, int density) {
    if (density <= 0) return 0.0f;
    float mult = (density == 1) ? 2.0f
               : (density == 2) ? 1.0f
               : (density == 3) ? 0.5f : 0.35f;
    return qlcg_maxf(QLCG_ATLAS_CONTOUR_MIN, qlcg_atlas_contour_unit(level, columns) * mult);
}

/* 统一投影入口：球面判背面，平面保持旧裁剪 */
static bool proj_point(const AtlasRenderer *ar, const MacroWorld *w, double wx, double wz,
                       float vp_x, float vp_y, float vp_w, float vp_h,
                       float *sx, float *sy) {
    if (ar->planet_view == PLANET_VIEW_GLOBE)
        return qlcg_atlas_world_to_screen_vis(ar, w, wx, wz, vp_x, vp_y, vp_w, vp_h, sx, sy);
    qlcg_atlas_world_to_screen(ar, w, wx, wz, vp_x, vp_y, vp_w, vp_h, sx, sy);
    return true;
}

/* UI 提交后恢复舆图管线 */
static void draw_map_pass(const AtlasRenderer *ar, int id, float opacity, const AtlasTileView *tv) {
    const GLuint textures[] = { ar->data_tex0, ar->data_tex1, ar->sat_tex,
                                tv->tex, tv->meta, ar->rain_tex, tv->water };
    glUseProgram(ar->prog);
    for (int i = 0; i < 7; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
    }
    glEnable(GL_BLEND);
    if (id == LAYER_DAYLIGHT || id == LAYER_CONTOURS) glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform1i(qlcg_gl_uniform(ar->prog, "uLayerId"), id);
    glUniform1f(qlcg_gl_uniform(ar->prog, "uLayerOpacity"), opacity);
    glBindVertexArray(ar->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
}

static void draw_vector_layer(const AtlasRenderer *ar, const MacroWorld *w, UiContext *ui,
                              const AtlasLayerSetting *layer,
                              float vp_x, float vp_y, float vp_w, float vp_h) {
        /* 文明图层 */
        if (layer->id == LAYER_CIVILIZATION && w->opt.artificial_enabled) {
            const SpriteDef *flag = ui_sprite_by_name("flag-pin");
            uint8_t alpha = (uint8_t)(layer->opacity * 235.0f);
            for (int f = 0; f < w->faction_count; f++) {
                const TerritoryFaction *fc = &w->factions[f];
                if (!fc->active || !qlcg_world_chunk_explored(w, fc->cx, fc->cz)) continue;
                float sx, sy;
                if (!proj_point(ar, w, fc->wx, fc->wz, vp_x, vp_y, vp_w, vp_h, &sx, &sy)) continue;
                float size = (fc->is_player ? 40.0f : 32.0f) * ui->met.size;
                float x = sx - size * 0.42f, y = sy - size * 0.87f;
                if (x + size < vp_x || x > vp_x + vp_w || y + size < vp_y || y > vp_y + vp_h) continue;

                uint32_t main_color = ui_rgba_hex(fc->main_color, 255);
                uint32_t trim_color = ui_rgba_hex(fc->trim_color, 255);
                float saved_alpha = ui->global_alpha;
                ui->global_alpha *= layer->opacity;
                ui_sprite_icon4(ui, flag, x, y, size, trim_color, main_color, trim_color);
                ui->global_alpha = saved_alpha;
                if (ar->zoom >= 4.0f) {
                    char flab[64];
                    snprintf(flab, sizeof(flab), "%s", qlcg_faction_display_name(fc));
                    ui_draw_text_center(ui, FONT_SMALL, sx, y - 16.0f * ui->met.size, flab, ui_rgba(255, 248, 230, alpha));
                }
            }
        }

        /* 区块图层 */
        if (layer->id == LAYER_CHUNKS && ar->zoom >= 10.5f) {
            float scale=fmaxf(1,ui->met.scale);
            int nx=(int)fmaxf(1,floorf(vp_w/(72.0f*scale)));
            int ny=(int)fmaxf(1,floorf(vp_h/(56.0f*scale)));
            for (int z=0;z<ny;z++) for (int x=0;x<nx;x++) {
                float px=vp_x+(x+.5f)*vp_w/nx;
                float py=vp_y+(z+.5f)*vp_h/ny;
                float wx,wz;
                if (!qlcg_atlas_screen_to_world(ar,w,px,py,vp_x,vp_y,vp_w,vp_h,&wx,&wz)) continue;
                int cx=(int)floorf(wx/QLCG_CHUNK_SIZE_M);
                int cz=(int)floorf(wz/QLCG_CHUNK_SIZE_M);
                if (!qlcg_world_chunk_explored(w, cx, cz)) continue;
                if (!proj_point(ar,w,(cx+.5f)*QLCG_CHUNK_SIZE_M,(cz+.5f)*QLCG_CHUNK_SIZE_M,
                    vp_x,vp_y,vp_w,vp_h,&px,&py)) continue;
                ChunkInspectInfo ci;
                qlcg_world_inspect_chunk(w,cx,cz,&ci);
                uint8_t alpha=(uint8_t)(layer->opacity*235.0f);
                const char *code=ci.type_def->code;
                float font=10*scale;
                ui_draw_text_px_center(ui,UI_FACE_MONO,font,true,px+1,py+1,code,ui_rgba(24,27,25,alpha));
                ui_draw_text_px_center(ui,UI_FACE_MONO,font,true,px,py,code,ui_rgba(248,244,232,alpha));
            }
        }

}

/* 玩家箭头与投影切线 */
static void draw_player_marker(const AtlasRenderer *ar, const MacroWorld *w, UiContext *ui,
                               const AtlasPlayerMarker *player,
                               float x, float y, float width, float height) {
    if (!player) return;
    float sx, sy;
    if (!proj_point(ar, w, player->wx, player->wz, x, y, width, height, &sx, &sy)) return;
    float dx = sinf(player->yaw), dy = -cosf(player->yaw);
    if (ar->planet_view == PLANET_VIEW_GLOBE) {
        float lat = (float)(-player->wz / w->circumference * 6.283185307179586);
        float dl = (float)(player->wx / w->circumference * 6.283185307179586 - ar->globe_lon);
        float fx = dx, fz = dy;
        dx = cosf(lat) * cosf(dl) * fx + sinf(lat) * sinf(dl) * fz;
        dy = -sinf(ar->globe_lat) * cosf(lat) * sinf(dl) * fx +
             (cosf(ar->globe_lat) * cosf(lat) + sinf(ar->globe_lat) * sinf(lat) * cosf(dl)) * fz;
    } else if (ar->planet_view == PLANET_VIEW_MERCATOR) {
        float lat = (float)fmax(-1.55, fmin(1.55, player->wz / w->circumference * 6.283185307179586));
        dy *= 3.141592653589793f / (2.9f * cosf(lat));
    }
    float length = hypotf(dx, dy);
    if (length < 1e-6f) return;
    dx /= length;
    dy /= length;
    const float ax[4] = {11, -7, -3, -7}, ay[4] = {0, -7, 0, 7};
    float px[4], py[4];
    for (int i = 0; i < 4; i++) {
        px[i] = sx + ax[i] * dx - ay[i] * dy;
        py[i] = sy + ax[i] * dy + ay[i] * dx;
    }
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        ui_draw_line(ui, px[i] + 1, py[i] + 1, px[j] + 1, py[j] + 1, 4, ui_rgba(0, 0, 0, 100));
    }
    ui_draw_poly(ui, px, py, 4, ui_rgba(194, 51, 63, 255));
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        ui_draw_line(ui, px[i], py[i], px[j], py[j], 2, ui_rgba(255, 255, 255, 255));
    }
}

static void render_viewport(AtlasRenderer *ar, const MacroWorld *w,
                                const CalendarClock *clk, UiContext *ui,
                                float vp_x, float vp_y, float vp_w, float vp_h,
                                bool is_creator_preview, const AtlasPlayerMarker *player) {
    if (!w) return;
    double t0 = qlcg_time_ms();

    bool survival = w->opt.play_mode == PLAY_MODE_SURVIVAL;
    if(qlcg_world_flat(w)) ar->planet_view=PLANET_VIEW_EQUIRECT;
    if (survival) {
        ar->planet_view = PLANET_VIEW_EQUIRECT;
        float local_zoom = qlcg_atlas_local_zoom(w, vp_w, vp_h);
        ar->zoom = ar->target_zoom = is_creator_preview ? local_zoom : fmaxf(ar->zoom, local_zoom);
        if (is_creator_preview)
            qlcg_atlas_center_on(ar, w, (w->spawn_cx + 0.5) * QLCG_CHUNK_SIZE_M,
                                 (w->spawn_cz + 0.5) * QLCG_CHUNK_SIZE_M, true);
    }

    if (ar->detail_world!=w || ar->world_upload_pending || ar->synced_delta_ver != w->delta_version ||
        ar->synced_terr_rev != w->territory_revision ||
        (!qlcg_world_flat(w) && (ar->tex_w != w->columns || ar->tex_h != w->rows)) ||
        !ar->tex_w || !ar->tex_h ||
        ar->synced_plane_count != (w->planes?w->planes->count:0)) {
        qlcg_atlas_upload_world(ar, w);
    }
    if (ar->world_upload_pending) return;

    /* 全览档随视口比例换算：世界宽或高铺满 88% */
    {
        float prior=ar->overview_zoom;
        float base_scale = vp_w / w->circumference;
        float fit = qlcg_minf(vp_w, vp_h * (qlcg_world_flat(w)?1.0f:2.0f)) * 0.88f / w->circumference;
        if(qlcg_world_flat(w)) {
            double b[4];qlcg_infinite_bounds(w,b);
            fit=(float)(fmin(vp_w/(b[2]-b[0]),vp_h/(b[3]-b[1]))*.88);
        }
        if (base_scale > 0.0f && fit > 0.0f) {
            ar->overview_zoom = qlcg_clampf(2.0f + logf(fit / base_scale) / logf(1.42f),
                                            qlcg_world_flat(w)?-128.0f:(float)QLCG_MIN_ATLAS_ZOOM, (float)QLCG_MAX_ATLAS_ZOOM);
            if(qlcg_world_flat(w) && !survival && fabsf(ar->zoom-prior)<.001f)
                ar->zoom=ar->target_zoom=ar->overview_zoom;
        }
    }
    ar->last_vp_x = vp_x + ui->global_dx;
    ar->last_vp_y = vp_y + ui->global_dy;
    ar->last_vp_w = vp_w;
    ar->last_vp_h = vp_h;

    /* 按需分配并分帧烘焙 */
    qlcg_atlas_tiles_update(ar, w, vp_x, vp_y, vp_w, vp_h, 6.0);

    /* 雨云图层：降水场纹理（4 小时档，限流重建） */
    qlcg_atlas_rain_sync(ar, w, clk);

    ui_flush(ui);

    float zscale = calc_zoom_scale(ar, w, vp_w);
    AtlasTileView tile_view = {0};
    qlcg_atlas_tiles_view(ar->tiles,&tile_view);
    float utc_h  = clk ? qlcg_utc_sun_hours(clk->unix_ms) : 12.0f;
    float sun_lon = (12.0f - utc_h) / 24.0f * 6.2831853f;
    float sun_lat = 0.1f;
    if (clk) {
        CalendarSnapshot snap;
        qlcg_calendar_snapshot(clk, 0, &snap);
        sun_lat = sinf(snap.season_phase * 6.2831853f) * 0.409f;
    }

    AstraFrame astro={0};
    if (w->opt.astronomy_enabled) {
        qlcg_astra_sample(&w->opt, clk, 0, 0, NULL, &astro);
        sun_lon = astro.sun_lon; sun_lat = astro.sun_lat;
    }

    float draw_x = ar->last_vp_x, draw_y = ar->last_vp_y;
    float qv[24] = {
        draw_x,        draw_y,        0.0f, 0.0f,
        draw_x + vp_w, draw_y,        1.0f, 0.0f,
        draw_x + vp_w, draw_y + vp_h, 1.0f, 1.0f,
        draw_x,        draw_y,        0.0f, 0.0f,
        draw_x + vp_w, draw_y + vp_h, 1.0f, 1.0f,
        draw_x,        draw_y + vp_h, 0.0f, 1.0f
    };

    glDisable(GL_DEPTH_TEST);
    glUseProgram(ar->prog);
    /* 瓦片层级先于投影取值 */
    glUniform2f(qlcg_gl_uniform(ar->prog, "uScreen"), (float)ui->win_w, (float)ui->win_h);
    glUniform2f(qlcg_gl_uniform(ar->prog, "uVpSize"), vp_w, vp_h);
    glUniform2f(qlcg_gl_uniform(ar->prog, "uCamW"), ar->cam_wx, ar->cam_wz);
    {
        double n=ldexp(1,tile_view.level),world_z=ar->cam_wz,derivative=1;
        if(ar->planet_view==PLANET_VIEW_MERCATOR) {
            double lat=2*atan(exp(ar->cam_wz/((qlcg_world_depth(w)*.5))*1.45))-1.5707963267948966;
            world_z=lat/6.283185307179586*w->circumference;
            derivative=2*1.45/3.141592653589793*cos(lat);
        }
        double tx=(ar->cam_wx/w->circumference+.5)*n;
        double ty=(world_z/w->circumference+qlcg_world_z_offset(w))*n;
        tx-=floor(tx/n)*n;
        glUniform2f(qlcg_gl_uniform(ar->prog,"uTileOrigin"),(float)floor(tx),(float)floor(ty));
        glUniform2f(qlcg_gl_uniform(ar->prog,"uTileFraction"),(float)(tx-floor(tx)),(float)(ty-floor(ty)));
        double step=n/(w->circumference*zscale);
        glUniform2f(qlcg_gl_uniform(ar->prog,"uTilePixelStep"),(float)step,(float)(step*derivative));
    }
    glUniform1f(qlcg_gl_uniform(ar->prog, "uCirc"), w->circumference);
    glUniform1i(qlcg_gl_uniform(ar->prog, "uFlat"), qlcg_world_flat(w));
    glUniform4f(qlcg_gl_uniform(ar->prog,"uFlatRect"),ar->flat_rect[0],ar->flat_rect[1],ar->flat_rect[2],ar->flat_rect[3]);
    glUniform1f(qlcg_gl_uniform(ar->prog,"uFlatCells"),(float)w->columns);
    glUniform1f(qlcg_gl_uniform(ar->prog, "uFlatSun"), astro.sun[1]);
    glUniform1f(qlcg_gl_uniform(ar->prog, "uFlatEclipse"), astro.solar_cover);
    glUniform1i(qlcg_gl_uniform(ar->prog, "uFlatSolarType"), astro.solar_type);
    glUniform1f(qlcg_gl_uniform(ar->prog, "uZoomScale"), zscale);
    glUniform1i(qlcg_gl_uniform(ar->prog, "uViewMode"), (int)ar->planet_view);
    glUniform1i(qlcg_gl_uniform(ar->prog, "uSatMode"), ar->sat_mode ? 1 : 0);
    glUniform2f(qlcg_gl_uniform(ar->prog, "uGlobeRot"), ar->globe_lon, ar->globe_lat);
    glUniform1f(qlcg_gl_uniform(ar->prog, "uGlobeRadius"), qlcg_atlas_globe_radius(ar, vp_w, vp_h));
    glUniform1f(qlcg_gl_uniform(ar->prog, "uSunLon"), sun_lon);
    glUniform1f(qlcg_gl_uniform(ar->prog, "uSunLat"), sun_lat);
    glUniform1i(qlcg_gl_uniform(ar->prog,"uAstro"),w->opt.astronomy_enabled);
    glUniform3f(qlcg_gl_uniform(ar->prog,"uSolarPosition"),astro.sun_world[0],astro.sun_world[1],astro.sun_world[2]);
    glUniform3f(qlcg_gl_uniform(ar->prog,"uLunarPosition"),astro.moon_world[0],astro.moon_world[1],astro.moon_world[2]);
    glUniform1i(qlcg_gl_uniform(ar->prog,"uLunarEclipse"),astro.lunar_type);
    glUniform1f(qlcg_gl_uniform(ar->prog, "uSimTime"), w->sim_time);
    {
        /* 冰层相位 0..4；无冰时刻则整层不画 */
        bool ice_ok = w->ice_time_valid;
        glUniform1f(qlcg_gl_uniform(ar->prog, "uIcePhase"), ice_ok ? w->ice_time.phase : -1.0f);
        glUniform1i(qlcg_gl_uniform(ar->prog, "uIceValid"), ice_ok ? 1 : 0);
    }
    glUniform1i(qlcg_gl_uniform(ar->prog, "uContourDensity"), ar->display.contour_density);
    {
        /* 档位由视图决定，不等瓦片就绪 */
        float ring_px = ar->planet_view == PLANET_VIEW_GLOBE
                      ? qlcg_atlas_globe_radius(ar, vp_w, vp_h) * 6.2831853f
                      : zscale * w->circumference;
        int level = (int)ceilf(log2f(qlcg_maxf(ring_px / ATLAS_TILE_SCREEN_PX, 1.0f)));
        float cstep = qlcg_atlas_contour_step(level, w->columns, ar->display.contour_density);
        glUniform1f(qlcg_gl_uniform(ar->prog, "uContourStep"), cstep);
    }
    glUniform1i(qlcg_gl_uniform(ar->prog, "uQuickFilter"), (int)ar->quick_filter);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ar->data_tex0);
    glUniform1i(qlcg_gl_uniform(ar->prog, "uTex0"), 0);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, ar->data_tex1);
    glUniform1i(qlcg_gl_uniform(ar->prog, "uTex1"), 1);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, ar->sat_tex);
    glUniform1i(qlcg_gl_uniform(ar->prog, "uSatTex"), 2);

    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, tile_view.tex);
    glUniform1i(qlcg_gl_uniform(ar->prog, "uTileTex"), 3);
    glActiveTexture(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_2D, tile_view.meta);
    glUniform1i(qlcg_gl_uniform(ar->prog, "uTileMeta"), 4);
    glActiveTexture(GL_TEXTURE0+6);
    glBindTexture(GL_TEXTURE_2D,tile_view.water);
    glUniform1i(qlcg_gl_uniform(ar->prog,"uTileWater"),6);
    glActiveTexture(GL_TEXTURE0 + 5);
    glBindTexture(GL_TEXTURE_2D, ar->rain_tex);
    glUniform1i(qlcg_gl_uniform(ar->prog, "uRainTex"), 5);
    glUniform1i(qlcg_gl_uniform(ar->prog, "uDetailLevel"), tile_view.level);
    glUniform1f(qlcg_gl_uniform(ar->prog, "uRiverMix"), tile_view.river_mix);
    glUniform1f(qlcg_gl_uniform(ar->prog, "uTileSamples"), (float)ATLAS_TILE_SAMPLE_PX);

    glBindVertexArray(ar->vao);
    glBindBuffer(GL_ARRAY_BUFFER, ar->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(qv), qv);
    draw_map_pass(ar, -1, 1.0f, &tile_view);

    ui_clip_push(ui, vp_x, vp_y, vp_w, vp_h);

    /* 叠加层：球面与平面共用同一投影入口 */
    {
        if(tile_view.river_mix<1 || qlcg_world_flat(w))
            qlcg_atlas_draw_rivers(ar, w, ui, vp_x, vp_y, vp_w, vp_h);

        /* 列表从后向前绘制 */
        for (int i = LAYER_ID_COUNT - 1; i >= 0; i--) {
            const AtlasLayerSetting *layer = &ar->display.layers[i];
            if (!layer->on || layer->opacity <= 0.0f) continue;
            if (layer->id == LAYER_RAIN && !ar->rain_tex) continue;
            if (layer->id == LAYER_SOURCE_RIVERS) {
                ui_flush(ui);
                if(tile_view.river_mix>0) draw_map_pass(ar,layer->id,layer->opacity,&tile_view);
                if(tile_view.river_mix<1)
                    qlcg_atlas_draw_sources(ar,w,ui,layer->opacity*(1-tile_view.river_mix),
                                           vp_x,vp_y,vp_w,vp_h);
            } else if (layer->id == LAYER_CIVILIZATION || layer->id == LAYER_CHUNKS) {
                draw_vector_layer(ar, w, ui, layer, vp_x, vp_y, vp_w, vp_h);
            } else {
                ui_flush(ui);
                draw_map_pass(ar, layer->id, layer->opacity, &tile_view);
            }
        }
        if (ar->quick_filter != FILTER_NONE) {
            ui_flush(ui);
            draw_map_pass(ar, -2, 1.0f, &tile_view);
        }

        if(ar->astronomy_overlay&&w->opt.astronomy_enabled&&clk) {
            bool refresh=!ar->eclipse_ready||ar->eclipse_seed!=w->opt.seed||ar->eclipse_mode!=clk->mode;
            refresh=refresh||ar->eclipse_tilt!=w->opt.axial_tilt_deg||ar->eclipse_prograde!=w->opt.prograde;
            refresh=refresh||clk->unix_ms<ar->eclipse_checked||clk->unix_ms>ar->eclipse.end;
            if(refresh) {
                ar->eclipse_tilt=w->opt.axial_tilt_deg;ar->eclipse_prograde=w->opt.prograde;
                ar->eclipse_ready=true;ar->eclipse_seed=w->opt.seed;ar->eclipse_mode=clk->mode;ar->eclipse_checked=clk->unix_ms;
                if(qlcg_astra_eclipse_find(&w->opt,clk,&ar->eclipse)) {
                    unsigned char *pixels=malloc(ASTRA_CHART_W*ASTRA_CHART_H*4);
                    if(pixels) {
                        qlcg_astra_eclipse_chart(&w->opt,clk,&ar->eclipse,pixels);
                        if(!ar->eclipse_tex)glGenTextures(1,&ar->eclipse_tex);
                        glActiveTexture(GL_TEXTURE0+7);glBindTexture(GL_TEXTURE_2D,ar->eclipse_tex);
                        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,ASTRA_CHART_W,ASTRA_CHART_H,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);free(pixels);
                        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
                    }
                } else ar->eclipse.end=clk->unix_ms+86400000;
            }
            ui_flush(ui);glUseProgram(ar->prog);glActiveTexture(GL_TEXTURE0+7);glBindTexture(GL_TEXTURE_2D,ar->eclipse_tex);
            glUniform1i(qlcg_gl_uniform(ar->prog,"uEclipseChart"),7);
            glUniform1i(qlcg_gl_uniform(ar->prog,"uEclipseKind"),ar->eclipse.type?ar->eclipse.kind:0);
            draw_map_pass(ar,-3,1,&tile_view);
        }
        /* 活跃自然灾害事件波纹标记 */
        for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
            const ActiveNaturalEvent *ev = &w->events[i];
            if (!ev->active || !qlcg_world_chunk_explored(w, (int)floorf(ev->wx / QLCG_CHUNK_SIZE_M),
                                                          (int)floorf(ev->wz / QLCG_CHUNK_SIZE_M))) continue;
            float sx, sy;
            if (!proj_point(ar, w, ev->wx, ev->wz, vp_x, vp_y, vp_w, vp_h, &sx, &sy)) continue;
            if (sx < vp_x || sx > vp_x + vp_w || sy < vp_y || sy > vp_y + vp_h) continue;
            float r_px = qlcg_clampf(ev->radius * zscale, 14.0f, 120.0f);
            ui_draw_circle(ui, sx, sy, r_px * (0.4f + 0.6f * fmodf(w->sim_time * 0.9f, 1.0f)), ui_rgba(220, 68, 52, 95));
            ui_draw_text_center(ui, FONT_BOLD, sx, sy - 10.0f, ev->title, ui_rgba(255, 230, 210, 245));
        }

        /* 出生点与玩家当前位置标记 */
        float sp_sx, sp_sy;
        const MacroWorld *spawn=qlcg_world_flat(w)?qlcg_infinite_find(w,0,0):w;
        double period=w->circumference+(double)w->circumference/w->columns;
        double sp_wx=spawn?(survival?(spawn->spawn_cx+.5)*QLCG_CHUNK_SIZE_M:spawn->spawn_wx):0;
        double sp_wz=spawn?(survival?(spawn->spawn_cz+.5)*QLCG_CHUNK_SIZE_M:spawn->spawn_wz):0;
        if(qlcg_world_flat(w)){sp_wx-=(double)w->opt.plane_x*period;sp_wz-=(double)w->opt.plane_z*period;}
        if (spawn && spawn->spawn_cell>=0 && proj_point(ar, w, sp_wx, sp_wz, vp_x, vp_y, vp_w, vp_h, &sp_sx, &sp_sy)) {
            ui_draw_circle(ui, sp_sx, sp_sy, 6.5f, ui_rgba(232, 184, 72, 245));
            ui_draw_circle(ui, sp_sx, sp_sy, 3.0f, ui_rgba(40, 32, 20, 255));
        }

        if (!is_creator_preview) {
            draw_player_marker(ar, w, ui, player, vp_x, vp_y, vp_w, vp_h);
        }
    }

    ui_clip_pop(ui);
    if (survival) {
        float alpha = ui->global_alpha;
        ui->global_alpha = 1.0f;
        double span = (double)zscale * QLCG_CHUNK_SIZE_M;
        double left = ar->cam_wx / QLCG_CHUNK_SIZE_M - vp_w / (2.0 * span);
        double top = ar->cam_wz / QLCG_CHUNK_SIZE_M - vp_h / (2.0 * span);
        int end_x = (int)ceil(left + vp_w / span);
        int end_z = (int)ceil(top + vp_h / span);
        for (int cz = (int)floor(top); cz < end_z; ++cz) {
            for (int cx = (int)floor(left); cx < end_x; ++cx) {
                float x = vp_x + (float)((cx - left) * span);
                float y = vp_y + (float)((cz - top) * span);
                double lx,lz;
                const MacroWorld *visible=qlcg_infinite_resolve(w,(cx+.5)*QLCG_CHUNK_SIZE_M,(cz+.5)*QLCG_CHUNK_SIZE_M,&lx,&lz);
                bool explored=visible && qlcg_world_chunk_explored(visible,(int)floor(lx/QLCG_CHUNK_SIZE_M),(int)floor(lz/QLCG_CHUNK_SIZE_M));
                if (!explored) {
                    float x0 = fmaxf(x, vp_x), y0 = fmaxf(y, vp_y);
                    float x1 = fminf(vp_x + (float)((cx + 1 - left) * span), vp_x + vp_w);
                    float y1 = fminf(vp_y + (float)((cz + 1 - top) * span), vp_y + vp_h);
                    ui_draw_rect(ui, x0, y0, x1 - x0, y1 - y0, ui_rgba(208, 198, 177, 255));
                } else if (is_creator_preview) {
                    ui_draw_border(ui, x, y, (float)span, (float)span, 1.0f, ui_rgba(36, 28, 19, 64));
                }
            }
        }
        ui->global_alpha = alpha;
    }
    ui_flush(ui);
    /* 舆图已落地，抓一张背景供叠在上面的玻璃面板采样 */
    ui_backdrop_capture(ui);
    ar->last_render_ms = qlcg_time_ms() - t0;
}

static void render_viewed_world(AtlasRenderer *ar,const MacroWorld *w,
                                const CalendarClock *clk,UiContext *ui,
                                float x,float y,float width,float height,
                                bool preview,const AtlasPlayerMarker *player) {
    double dx=0,dz=0;
    const MacroWorld *view=w;
    AtlasPlayerMarker marker=player?*player:(AtlasPlayerMarker){0};
    if(w && qlcg_world_flat(w) && !preview) {
        double lx,lz;
        const MacroWorld *target=qlcg_infinite_resolve(w,ar->cam_wx,ar->cam_wz,&lx,&lz);
        if(target && target!=w) {
            double period=w->circumference+(double)w->circumference/w->columns;
            dx=((double)target->opt.plane_x-w->opt.plane_x)*period;
            dz=((double)target->opt.plane_z-w->opt.plane_z)*period;
            view=target;ar->cam_wx-=dx;ar->cam_wz-=dz;
            marker.wx-=dx;marker.wz-=dz;
        }
    }
    render_viewport(ar,view,clk,ui,x,y,width,height,preview,player?&marker:NULL);
    ar->cam_wx+=dx;ar->cam_wz+=dz;
}

void qlcg_atlas_render_player_viewport(AtlasRenderer *ar, const MacroWorld *w,
                                      const CalendarClock *clk, UiContext *ui,
                                      float x, float y, float width, float height,
                                      const AtlasPlayerMarker *player) {
    render_viewed_world(ar, w, clk, ui, x, y, width, height, false, player);
}

void qlcg_atlas_render_viewport(AtlasRenderer *ar, const MacroWorld *w,
                               const CalendarClock *clk, UiContext *ui,
                               float x, float y, float width, float height,
                               bool is_creator_preview, int player_cx, int player_cz) {
    AtlasPlayerMarker player = {(player_cx + 0.5) * QLCG_CHUNK_SIZE_M,
                                (player_cz + 0.5) * QLCG_CHUNK_SIZE_M, 0};
    render_viewed_world(ar, w, clk, ui, x, y, width, height, is_creator_preview, &player);
}
