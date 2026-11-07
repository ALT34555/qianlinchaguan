/* 校验：有限值、汇流、板块、湖面、水量 */
#include "gen_validate.h"
#include "gen_water.h"
#include <math.h>
#include <string.h>

static int finite_f(float v) {
    return (v == v) && (v < 1e30f) && (v > -1e30f);
}

static int differs(double a, double b) {
    return !isfinite(a) || !isfinite(b) || fabs(a-b)>1e-8*fmax(1,fmax(fabs(a),fabs(b)));
}

void qlcg_validate_world(const MacroWorld *w, WorldAudit *out) {
    memset(out, 0, sizeof(*out));
    if (!w) return;
    int n = w->cell_count;
    double land_area = 0.0, total_area = 0.0, runoff = 0.0, outlet = 0.0, terminal_area = 0.0;

    for (int i = 0; i < n; i++) {
        if (!finite_f(w->height[i]) || !finite_f(w->temperature[i]) ||
            !finite_f(w->rainfall[i]) || !isfinite(w->flow[i]) || !finite_f(w->spill[i]) ||
            !isfinite(w->catchment[i]) || !isfinite(w->evaporation[i])) {
            out->nan_count++;
        }
        total_area += w->areas[i];
        if (w->height[i] > 0.0f) {
            land_area += w->areas[i];
            runoff += qlcg_runoff_m3s(w->rainfall[i], w->evaporation[i], w->areas[i]);
        }
        int r = w->receiver[i];
        if (r >= n || r == i || r < -1) { out->bad_rank++; continue; }
        if (r >= 0) {
            /* base 的 rank 语义：下游序号更小 */
            if (w->rank[r] >= w->rank[i]) out->bad_rank++;
            if (w->spill[r] > w->spill[i] + 0.01f) out->uphill++;
        } else {
            outlet += w->flow[i];
            terminal_area += w->catchment[i];
        }
        if (w->plate_id && w->plate_id[i] >= w->plate_count) out->bad_plate++;
        if (w->water[i] == 2 && (w->level[i] < w->height[i] - 0.01f ||
            w->lake_id[i] < 0 || w->lake_id[i] >= w->lake_count)) out->bad_lake++;
    }
    out->land_ratio = (total_area > 0.0) ? (float)(land_area / total_area) : 0.0f;
    out->flow_residual = (runoff > 1e-9) ? (float)(outlet / runoff) : 0.0f;
    const HydrologyBalance *s=&w->hydrology;
    double rain=0,replaced=0,loss=0;
    for (int k=0;k<w->lake_count;k++) {
        const MacroLake *l=&w->lakes[k];
        if (l->exit_cell<0 || l->exit_cell>=n || differs(l->inflow,l->outflow+l->evaporation) ||
            (l->exit_cell>=0 && l->exit_cell<n && differs(w->flow[l->exit_cell],l->outflow))) out->bad_balance++;
        rain+=l->rainfall; replaced+=l->runoff_replaced; loss+=l->evaporation;
    }
    if (!s->valid || differs(rain,s->lake_rain) || differs(replaced,s->lake_runoff) ||
        differs(loss,s->lake_loss) || differs(outlet,s->output)) out->bad_balance++;
    double input=s->input+s->lake_rain-s->lake_runoff;
    double output=outlet+s->lake_loss+s->dry_loss+s->river_loss;
    out->conservation=fabs(input-output)/fmax(1e-9,input);
    out->area_error=fabs(terminal_area-land_area)/fmax(1e-9,land_area);
}

int qlcg_audit_ok(const WorldAudit *a) {
    if (!a) return 0;
    return (a->nan_count == 0 && a->bad_rank == 0 && a->uphill == 0 &&
            a->bad_plate == 0 && a->bad_lake == 0 && a->bad_balance == 0 &&
            isfinite(a->conservation) && a->conservation < 1e-9 &&
            isfinite(a->area_error) && a->area_error < 1e-9) ? 1 : 0;
}
