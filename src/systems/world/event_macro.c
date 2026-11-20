/* 宏观星球自然事件 */
#include "planet_macro.h"
#include "event_state.h"
#include "surface_internal.h"
#include "world_internal.h"
#include "../../core/math_util.h"
#include "../../core/i18n.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static bool trigger_event(MacroWorld *w,NaturalEventType type,float wx,float wz,bool automatic) {
    if (!w || (unsigned)type>=NAT_EVENT_COUNT || !isfinite(wx) || !isfinite(wz) ||
        w->cell_count<=0 || w->columns<=0 || w->rows<=0 || !(w->circumference>0)) return false;
    ActiveNaturalEvent *ev = NULL;
    for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
        if (!w->events[i].active) { ev = &w->events[i]; break; }
    }
    if (!ev) {
        snprintf(w->last_event_msg,sizeof(w->last_event_msg),"%s",tr("event.macro.full"));
        return false;
    }
    if (!w->event_cells) {
        w->event_cells=calloc((size_t)w->cell_count,1);
        if (!w->event_cells) return false;
    }
    memset(ev, 0, sizeof(*ev));
    ev->active    = true;
    ev->automatic = automatic;
    ev->type      = type;
    ev->wx        = wx;
    ev->wz        = wz;
    ev->radius    = w->circumference * 0.048f;
    ev->intensity = 1.0f;
    ev->age       = 0.0f;
    ev->duration  = (type == NAT_EVENT_TYPHOON) ? 28.0f : 16.0f;
    ev->vx        = (type == NAT_EVENT_TYPHOON) ? -w->circumference * 0.0045f : 0.0f;
    ev->vz        = (type == NAT_EVENT_TYPHOON) ?  w->circumference * 0.0022f : 0.0f;
    snprintf(ev->title, sizeof(ev->title), "%s", qlcg_nat_event_name(type));
    snprintf(w->last_event_msg, sizeof(w->last_event_msg), tr("event.macro.burst"),
             qlcg_i18n_content_text(ev->title), wx, wz);

    MacroCellRect rects[2];
    double r=ev->radius;
    int count=qlcg_macro_rects(w,wx-r,wz-r,wx+r,wz+r,rects);
    uint32_t before=w->surface_revision;
    for (int n=0;n<count;n++) for (int z=rects[n].z0;z<rects[n].z1;z++)
      for (int x=rects[n].x0;x<rects[n].x1;x++) {
        int i=z*w->columns+x;
        float cx, cz;
        qlcg_world_cell_center(w, i, &cx, &cz);
        float dx=qlcg_macro_delta_x(w,cx,wx);
        float d = hypotf(dx, cz - wz);
        if (d >= ev->radius) continue;
        float k = 1.0f - d / ev->radius;
        qlcg_event_mark_cell(w,i,type);
        if (type == NAT_EVENT_EARTHQUAKE) {
            float fault = sinf(dx * 0.004f + (cz - wz) * 0.003f);
            w->height[i] += fault * 85.0f * k;
            w->spill[i]   = w->height[i];
        } else if (type == NAT_EVENT_DESERTIFY) {
            w->desert_factor[i] = qlcg_clampf(w->desert_factor[i] + 0.65f * k, 0.0f, 1.0f);
            w->moisture[i]      = qlcg_clampf(w->moisture[i] - 0.38f * k, 0.0f, 1.0f);
            w->forest_bonus[i]  = qlcg_clampf(w->forest_bonus[i] - 0.55f * k, 0.0f, 1.0f);
        } else if (type == NAT_EVENT_TYPHOON || type == NAT_EVENT_FLOOD) {
            w->moisture[i] = qlcg_clampf(w->moisture[i] + 0.45f * k, 0.0f, 1.0f);
            w->level[i]    = qlcg_maxf(w->level[i], w->height[i] + 6.0f * k);
        } else if (type == NAT_EVENT_COLD_WAVE) {
            w->temperature[i] -= 14.0f * k;
        } else if (type == NAT_EVENT_WILDFIRE) {
            w->forest_bonus[i]  = qlcg_clampf(w->forest_bonus[i] - 0.75f * k, 0.0f, 1.0f);
            w->desert_factor[i] = qlcg_clampf(w->desert_factor[i] + 0.32f * k, 0.0f, 1.0f);
        }
    }
    if (count) {
        qlcg_world_rebuild_atlas_region(w,rects,count,MACRO_RIVERS_UNCHANGED);
        r+=(double)w->circumference/w->columns;
        qlcg_world_macro_surface_dirty(w,w->delta_version,before,wx-r,wz-r,wx+r,wz+r);
    }
    return true;
}

bool qlcg_trigger_macro_event(MacroWorld *w,NaturalEventType type,float wx,float wz) {
    return trigger_event(w,type,wx,wz,false);
}

bool qlcg_trigger_scheduled_macro_event(MacroWorld *w,NaturalEventType type,float wx,float wz) {
    return trigger_event(w,type,wx,wz,true);
}
