/* 宏观星球自然事件 */
#include "planet_macro.h"
#include "surface_internal.h"
#include "../../core/math_util.h"
#include "../../core/i18n.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void qlcg_trigger_macro_event(MacroWorld *w, NaturalEventType type, float wx, float wz) {
    int slot = 0;
    for (int i = 0; i < MAX_ACTIVE_EVENTS; i++) {
        if (!w->events[i].active) { slot = i; break; }
    }
    ActiveNaturalEvent *ev = &w->events[slot];
    memset(ev, 0, sizeof(*ev));
    ev->active    = true;
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
             ev->title, wx, wz);

    /* 立即对宏观地表与水热产生自然演变 */
    for (int i = 0; i < w->cell_count; i++) {
        float cx, cz;
        qlcg_world_cell_center(w, i, &cx, &cz);
        float d = hypotf(cx - wx, cz - wz);
        if (d > ev->radius) continue;
        float k = 1.0f - d / ev->radius;
        if (type == NAT_EVENT_EARTHQUAKE) {
            float fault = sinf((cx - wx) * 0.004f + (cz - wz) * 0.003f);
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
    qlcg_world_rebuild_atlas_fields(w);
}

/* 在 3D 山川内触发自然事件并实时改变地 */
