/* 舆图相机：缩放比例、屏幕与世界坐标互转 */
#include "atlas_view.h"
#include "../systems/world/world_infinite.h"
#include "../core/math_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

float calc_zoom_scale(const AtlasRenderer *ar, const MacroWorld *w, float vp_w) {
    float base_scale = vp_w / w->circumference;
    return base_scale * powf(1.42f, ar->zoom - 2.0f);
}

/* 总览档：世界宽/高铺满 88% */
float qlcg_atlas_overview_zoom(const AtlasRenderer *ar) {
    if (ar && (ar->overview_zoom > 0.0f || ar->last_vp_w>0)) return ar->overview_zoom;
    return QLCG_ATLAS_OVERVIEW_ZOOM;
}

/* 球体半径：min(w,h)×0.43×缩放倍率 */
float qlcg_atlas_globe_radius(const AtlasRenderer *ar, float vp_w, float vp_h) {
    float f = powf(1.42f, ar->zoom - qlcg_atlas_overview_zoom(ar));
    return qlcg_minf(vp_w, vp_h) * 0.43f * qlcg_clampf(f, 1.0f, 8.0f);
}

/* 河流 LOD 层级：绕行一圈像素 / 128 的二进制阶 */
int qlcg_atlas_river_level(const AtlasRenderer *ar, const MacroWorld *w,
                           float vp_w, float vp_h) {
    (void)vp_h;
    if (!ar || !w || w->circumference <= 1.0f) return 8;
    /* 与投影无关，统一按平面口径取档，球面放大步调一致 */
    float ring_px = calc_zoom_scale(ar, w, vp_w) * w->circumference;
    if (!(ring_px > 1.0f)) return 2;
    int lvl = (int)ceilf(log2f(ring_px / 128.0f));
    return qlcg_clampi(lvl, 2, QLCG_MAX_ATLAS_ZOOM);
}

/* 视图中心移到指定世界坐标 */
static void clamp_flat_camera(AtlasRenderer *ar,const MacroWorld *w) {
    double b[4];qlcg_infinite_bounds(w,b);
    ar->cam_wx=fmax(b[0],fmin(b[2],ar->cam_wx));
    ar->cam_wz=fmax(b[1],fmin(b[3],ar->cam_wz));
}

void qlcg_atlas_center_on(AtlasRenderer *ar, const MacroWorld *w,
                          double wx, double wz, bool keep_zoom) {
    if (!ar || !w) return;
    ar->cam_wx = wx;
    double limit = qlcg_world_flat(w)?w->circumference*.505:w->circumference * (w->opt.play_mode == PLAY_MODE_SURVIVAL ? 0.25 : 0.24);
    ar->cam_wz = fmax(-limit, fmin(limit, wz));
    if(qlcg_world_flat(w)){ar->cam_wz=wz;clamp_flat_camera(ar,w);}
    if (ar->planet_view == PLANET_VIEW_MERCATOR)
        ar->cam_wz = log(tan(0.7853981633974483 + ar->cam_wz / w->circumference *
                            3.141592653589793)) / 1.45 * (w->circumference * 0.25);
    ar->globe_lon = (wx / w->circumference) * 6.2831853f;
    ar->globe_lat = qlcg_clampf(-(wz / (w->circumference * 0.5f)) * 3.1415926f, -1.45f, 1.45f);
    if (!keep_zoom) {
        ar->zoom = ar->target_zoom = qlcg_atlas_overview_zoom(ar);
    }
}

/* 球面缩放锚点：让光标下的世界点保持在原地 */
static void globe_anchor(AtlasRenderer *ar, const MacroWorld *w, float vp_w, float vp_h,
                         float rad_before, float before_wx, float before_wz) {
    float rad_after = qlcg_atlas_globe_radius(ar, vp_w, vp_h);
    if (rad_before < 1.0f || rad_after < 1.0f) return;
    float lon = (before_wx / w->circumference) * 6.2831853f;
    float lat = -(before_wz / (w->circumference * 0.5f)) * 3.1415926f;
    float vx = cosf(lat) * sinf(lon), vy = sinf(lat), vz = cosf(lat) * cosf(lon);
    float clat = cosf(ar->globe_lat), slat = sinf(ar->globe_lat);
    float fx = clat * sinf(ar->globe_lon), fy = slat, fz = clat * cosf(ar->globe_lon);
    float d = qlcg_clampf(fx * vx + fy * vy + fz * vz, -1.0f, 1.0f);
    if (d <= 0.0f) return;                    /* 背面不锚定 */
    float a_before = acosf(d);
    float sa = qlcg_clampf(sinf(a_before) * rad_before / rad_after, 0.0f, 0.9995f);
    float delta = a_before - asinf(sa);
    if (fabsf(delta) < 1e-5f) return;
    float ux = vx - fx * d, uy = vy - fy * d, uz = vz - fz * d;
    float ul = sqrtf(ux * ux + uy * uy + uz * uz);
    if (ul < 1e-6f) return;
    ux /= ul; uy /= ul; uz /= ul;
    float cd = cosf(delta), sd = sinf(delta);
    float nx = fx * cd + ux * sd, ny = fy * cd + uy * sd, nz = fz * cd + uz * sd;
    float nl = sqrtf(nx * nx + ny * ny + nz * nz);
    if (nl < 1e-6f) return;
    nx /= nl; ny /= nl; nz /= nl;
    ar->globe_lat = qlcg_clampf(asinf(qlcg_clampf(ny, -1.0f, 1.0f)), -1.45f, 1.45f);
    ar->globe_lon = atan2f(nx, nz);
}

/* 屏幕坐标转世界坐标（支持球体与平面投影） */
bool qlcg_atlas_screen_to_world_d(const AtlasRenderer *ar, const MacroWorld *w,
                                float sx, float sy,
                                float vp_x, float vp_y, float vp_w, float vp_h,
                                double *out_wx, double *out_wz) {
    float px = sx - (vp_x + vp_w * 0.5f);
    float py = sy - (vp_y + vp_h * 0.5f);
    float zscale = calc_zoom_scale(ar, w, vp_w);

    if (ar->planet_view == PLANET_VIEW_GLOBE) {
        float rad = qlcg_atlas_globe_radius(ar, vp_w, vp_h);
        float spx = px / rad;
        float spy = py / rad;
        float r2  = spx * spx + spy * spy;
        if (r2 > 1.0f) return false;
        float z = sqrtf(1.0f - r2);
        float nx = spx, ny = -spy, nz = z;
        float clat = cosf(ar->globe_lat), slat = sinf(ar->globe_lat);
        float n1x = nx, n1y = ny * clat + nz * slat, n1z = -ny * slat + nz * clat;
        float clon = cosf(ar->globe_lon), slon = sinf(ar->globe_lon);
        float wx3 = n1x * clon + n1z * slon;
        float wy3 = n1y;
        float wz3 = -n1x * slon + n1z * clon;
        float lon = atan2f(wx3, wz3);
        float lat = asinf(qlcg_clampf(wy3, -1.0f, 1.0f));
        *out_wx = (lon / 6.2831853f) * w->circumference;
        *out_wz = -(lat / 3.1415926f) * (w->circumference * 0.5f);
        return true;
    }

    double wx = ar->cam_wx + px / (double)zscale;
    double wz = ar->cam_wz + py / (double)zscale;
    float half_z = qlcg_world_depth(w) * 0.5f;
    if (ar->planet_view==PLANET_VIEW_MERCATOR) {
        double merc_y=fmax(-.98,fmin(.98,wz/half_z))*1.45;
        wz=(2*atan(exp(merc_y))-1.5707963267948966)/3.141592653589793*(w->circumference*.5);
    } else if (!qlcg_world_flat(w) && (wz < -half_z || wz > half_z)) return false;
    if(qlcg_world_flat(w)) {
        double lx,lz;
        if(!qlcg_infinite_resolve(w,wx,wz,&lx,&lz))return false;
    }
    else wx -= floor(wx / w->circumference + .5)*w->circumference;
    *out_wx = wx;
    *out_wz = wz;
    return true;
}

bool qlcg_atlas_screen_to_world(const AtlasRenderer *ar,const MacroWorld *w,
                                float sx,float sy,float x,float y,float width,float height,
                                float *wx,float *wz) {
    double px,pz;
    if(!qlcg_atlas_screen_to_world_d(ar,w,sx,sy,x,y,width,height,&px,&pz)) return false;
    *wx=(float)px; *wz=(float)pz; return true;
}

/* 世界坐标转屏幕坐标 */
void qlcg_atlas_world_to_screen(const AtlasRenderer *ar, const MacroWorld *w,
                                double wx, double wz,
                                float vp_x, float vp_y, float vp_w, float vp_h,
                                float *out_sx, float *out_sy) {
    float zscale = calc_zoom_scale(ar, w, vp_w);
    double dx = wx - ar->cam_wx;
    if(!qlcg_world_flat(w)) dx -= floor(dx/w->circumference+.5)*w->circumference;
    if (ar->planet_view==PLANET_VIEW_MERCATOR) {
        double lat=fmax(-1.55,fmin(1.55,wz/(w->circumference*.5)*3.141592653589793));
        wz=log(tan(.7853981633974483+lat*.5))/1.45*(w->circumference*.25);
    }
    double dz = wz - ar->cam_wz;
    *out_sx = vp_x + vp_w * 0.5f + dx * zscale;
    *out_sy = vp_y + vp_h * 0.5f + dz * zscale;
}

/* 投影并判可见：球面按正射与背面剔除，平面按视口裁剪 */
bool qlcg_atlas_world_to_screen_vis(const AtlasRenderer *ar, const MacroWorld *w,
                                    double wx, double wz,
                                    float vp_x, float vp_y, float vp_w, float vp_h,
                                    float *out_sx, float *out_sy) {
    if (!w) return false;
    if (ar->planet_view == PLANET_VIEW_GLOBE) {
        float circ = w->circumference;
        float lon  = (wx / circ) * 6.2831853f;
        float lat  = -(wz / (circ * 0.5f)) * 3.1415926f;
        float dl   = lon - ar->globe_lon;
        dl -= 6.2831853f * floorf(dl / 6.2831853f + 0.5f);
        float clat = cosf(ar->globe_lat), slat = sinf(ar->globe_lat);
        float cl = cosf(lat), sl = sinf(lat);
        if (sl * slat + cl * clat * cosf(dl) < 0.0f) return false;
        float rad = qlcg_atlas_globe_radius(ar, vp_w, vp_h);
        *out_sx = vp_x + vp_w * 0.5f + rad * cl * sinf(dl);
        *out_sy = vp_y + vp_h * 0.5f - rad * (clat * sl - slat * cl * cosf(dl));
        return true;
    }
    qlcg_atlas_world_to_screen(ar, w, wx, wz, vp_x, vp_y, vp_w, vp_h, out_sx, out_sy);
    return *out_sx >= vp_x && *out_sx <= vp_x + vp_w &&
           *out_sy >= vp_y && *out_sy <= vp_y + vp_h;
}

float qlcg_atlas_local_zoom(const MacroWorld *w, float vp_w, float vp_h) {
    float span = (2 * QLCG_SPAWN_REVEAL_RADIUS + 1) * QLCG_CHUNK_SIZE_M;
    float scale = fminf(vp_w, vp_h) / span;
    return 2.0f + logf(scale * w->circumference / vp_w) / logf(1.42f);
}

void qlcg_atlas_set_projection(AtlasRenderer *ar, const MacroWorld *w, PlanetView view) {
    if(qlcg_world_flat(w)) view=PLANET_VIEW_EQUIRECT;
    if (ar->planet_view == view || w->opt.play_mode == PLAY_MODE_SURVIVAL) return;
    double wx = ar->cam_wx, wz = ar->cam_wz;
    if (ar->last_vp_w > 0.0f && ar->last_vp_h > 0.0f)
        qlcg_atlas_screen_to_world_d(ar, w, ar->last_vp_w * 0.5f, ar->last_vp_h * 0.5f,
                                    0, 0, ar->last_vp_w, ar->last_vp_h, &wx, &wz);
    ar->planet_view = view;
    qlcg_atlas_center_on(ar, w, wx, wz, true);
}

void qlcg_atlas_drag(AtlasRenderer *ar, const MacroWorld *w, float x, float y, float dx, float dy) {
    float width = ar->last_vp_w, height = ar->last_vp_h;
    if (width < 1.0f || height < 1.0f) return;
    if (ar->planet_view != PLANET_VIEW_GLOBE) {
        float scale = calc_zoom_scale(ar, w, width);
        double limit = qlcg_world_flat(w)?w->circumference*.505:w->circumference * (w->opt.play_mode == PLAY_MODE_SURVIVAL ? 0.25 : 0.24);
        ar->cam_wx -= dx / (double)scale;
        ar->cam_wz-=dy/(double)scale;
        if(qlcg_world_flat(w))clamp_flat_camera(ar,w);
        else {
            ar->cam_wx -= floor(ar->cam_wx / w->circumference + 0.5) * w->circumference;
            ar->cam_wz = fmax(-limit, fmin(limit, ar->cam_wz));
        }
        return;
    }

    float radius = qlcg_atlas_globe_radius(ar, width, height);
    if (radius < 1.0f) return;
    float nx = (x - width * 0.5f) / radius;
    float ny = (height * 0.5f - y) / radius;
    float depth2 = 1.0f - nx * nx - ny * ny;
    double wx, wz;
    if (depth2 > 0.0f &&
        qlcg_atlas_screen_to_world_d(ar, w, x - dx, y - dy, 0, 0, width, height, &wx, &wz)) {
        float lon = (float)(wx / w->circumference) * QLCG_TAU;
        float lat = (float)(-wz / w->circumference) * QLCG_TAU;
        float clat = cosf(lat);
        if (clat > fabsf(nx) && cosf(lon - ar->globe_lon) > 0.0f) {
            float forward = sqrtf(clat * clat - nx * nx);
            float pitch = atan2f(sinf(lat), forward) - atan2f(ny, sqrtf(depth2));
            if (fabsf(pitch) <= 1.45f) {
                ar->globe_lon = lon - asinf(nx / clat);
                ar->globe_lat = pitch;
                return;
            }
        }
    }
    ar->globe_lon = remainderf(ar->globe_lon - dx / radius, QLCG_TAU);
    ar->globe_lat = qlcg_clampf(ar->globe_lat + dy / radius, -1.45f, 1.45f);
}

/* 以鼠标位置为锚点平滑缩放舆图 */
void qlcg_atlas_zoom_step(AtlasRenderer *ar, const MacroWorld *w, float delta_zoom,
                          float pivot_sx, float pivot_sy,
                          float vp_x, float vp_y, float vp_w, float vp_h) {
    double before_wx = 0, before_wz = 0;
    bool valid = qlcg_atlas_screen_to_world_d(ar, w, pivot_sx, pivot_sy, vp_x, vp_y, vp_w, vp_h, &before_wx, &before_wz);
    float rad_before = qlcg_atlas_globe_radius(ar, vp_w, vp_h);
    float ov = qlcg_atlas_overview_zoom(ar);
    float max_zoom=2+logf(128.0f*powf(2.0f,QLCG_MAX_ATLAS_ZOOM)/fmaxf(1,vp_w))/logf(1.42f);
    bool local = w->opt.play_mode == PLAY_MODE_SURVIVAL;
    float min_zoom = local ? qlcg_atlas_local_zoom(w, vp_w, vp_h) : qlcg_world_flat(w)?ov:(float)QLCG_MIN_ATLAS_ZOOM;
    ar->zoom = qlcg_clampf(ar->zoom + delta_zoom, min_zoom, fmaxf(min_zoom, max_zoom));
    ar->target_zoom = ar->zoom;

    /* 球面缩放贴住光标，切平面后再走平面锚点 */
    if (ar->planet_view == PLANET_VIEW_GLOBE && valid) {
        globe_anchor(ar, w, vp_w, vp_h, rad_before, before_wx, before_wz);
    }

    /* 总览五倍切平面，缩回转球体 */
    if (ar->planet_view == PLANET_VIEW_GLOBE && ar->zoom >= ov + 4.83f) {
        ar->cam_wx = (ar->globe_lon / 6.2831853f) * w->circumference;
        ar->cam_wz = -(ar->globe_lat / 3.1415926f) * (w->circumference * 0.5f);
        ar->planet_view = PLANET_VIEW_EQUIRECT;
        return;
    }
    if (!local && !qlcg_world_flat(w) && ar->planet_view != PLANET_VIEW_GLOBE && ar->zoom <= ov + 4.2f) {
        ar->globe_lon = ar->cam_wx / w->circumference * 6.2831853f;
        ar->globe_lat = qlcg_clampf(-(ar->cam_wz / (w->circumference * 0.5f)) * 3.1415926f, -1.45f, 1.45f);
        ar->cam_wx = ar->cam_wz = 0.0f;
        ar->planet_view = PLANET_VIEW_GLOBE;
        return;
    }

    if (valid && ar->planet_view != PLANET_VIEW_GLOBE) {
        double after_wx = 0, after_wz = 0;
        if (qlcg_atlas_screen_to_world_d(ar, w, pivot_sx, pivot_sy, vp_x, vp_y, vp_w, vp_h, &after_wx, &after_wz)) {
            ar->cam_wx += (before_wx - after_wx);
            if(ar->planet_view==PLANET_VIEW_MERCATOR) {
                double scale=w->circumference*.25/1.45;
                before_wz=log(tan(.7853981633974483+before_wz/w->circumference*3.141592653589793))*scale;
                after_wz=log(tan(.7853981633974483+after_wz/w->circumference*3.141592653589793))*scale;
            }
            double limit = qlcg_world_flat(w)?w->circumference*.505:w->circumference * (local ? 0.25 : 0.24);
            ar->cam_wz+=before_wz-after_wz;
            if(qlcg_world_flat(w))clamp_flat_camera(ar,w);
            else ar->cam_wz = fmax(-limit, fmin(limit, ar->cam_wz));
        }
    }
}
