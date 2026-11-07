/* 状态反馈：七种全屏特效的交叉淡入与呼吸 */
#include "ui_status_feedback.h"
#include "../systems/world/decoration_collision.h"
#include "../core/i18n.h"
#include "../core/math_util.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 三套配色：白、黑、红，与 base COLORS 一致 */
static const uint32_t k_colors[3] = { 0xF4F3F2u, 0x1A1A1Au, 0xAB372Fu };
static const char *k_ids[SFX_COUNT] = { "001", "002", "003", "101", "102", "103", "501" };

/* smoothstep：fade_s 内 from→to */
static float sfx_strength(const StatusFade *f, float now, float fade_s) {
    float p = qlcg_clampf((now - f->changed_at) / fade_s, 0.0f, 1.0f);
    return f->from + (f->to - f->from) * p * p * (3.0f - 2.0f * p);
}

/* 呼吸系数：2.4 秒一个周期，depth 为最大衰减 */
static float sfx_breath(const StatusFade *f, float now, float period, float depth) {
    float phase = (now - f->started_at) * QLCG_TAU / period;
    return 1.0f - (1.0f - cosf(phase)) * 0.5f * depth;
}

void ui_status_feedback_reset(StatusFeedback *sf) {
    memset(sf, 0, sizeof(*sf));
    sf->current = SFX_NONE;
    sf->forced  = SFX_NONE;
    sf->inited  = true;
}

void ui_status_feedback_update(StatusFeedback *sf, int automatic, bool enabled, float now) {
    if (!sf->inited) ui_status_feedback_reset(sf);
    const MovementRules *r = qlcg_movement_rules();
    sf->now = now;
    int target = sf->forced >= 0 ? sf->forced : (enabled ? automatic : SFX_NONE);
    if (target != sf->current) {
        for (int i = 0; i < SFX_COUNT; i++) {
            StatusFade *f = &sf->fades[i];
            if (!f->active) continue;
            f->from = sfx_strength(f, now, r->fade_seconds);
            f->to = (i == target) ? 1.0f : 0.0f;
            f->changed_at = now;
        }
        if (target >= 0 && !sf->fades[target].active) {
            sf->fades[target] = (StatusFade){ true, 0.0f, 1.0f, now, now, ++sf->seq_counter };
        }
        sf->current = target;
    }
    sf->blur_px = 0.0f;
    for (int i = 0; i < SFX_COUNT; i++) {
        StatusFade *f = &sf->fades[i];
        if (!f->active) continue;
        /* 淡出满一个周期后释放该层 */
        if (f->to < 0.5f && now - f->changed_at >= r->fade_seconds) {
            f->active = false;
            continue;
        }
        if (i != SFX_501) continue;
        float s = sfx_strength(f, now, r->fade_seconds);
        float phase = (now - f->started_at) * QLCG_TAU / r->pulse_seconds;
        sf->blur_px = s * 4.0f * (1.0f + 0.15f * sinf(phase));
    }
}

float ui_status_feedback_blur(const StatusFeedback *sf) {
    return sf->blur_px;
}

/* 单层强度：501 为模糊半径，其余含呼吸 */
float ui_status_feedback_layer(const StatusFeedback *sf, int id) {
    if (id < 0 || id >= SFX_COUNT) return 0.0f;
    if (id == SFX_501) return sf->blur_px;
    const MovementRules *r = qlcg_movement_rules();
    const StatusFade *f = &sf->fades[id];
    if (!f->active) return 0.0f;
    float depth = (id >= SFX_101) ? 0.1f : r->pulse_depth;
    return sfx_strength(f, sf->now, r->fade_seconds) *
           sfx_breath(f, sf->now, r->pulse_seconds, depth);
}

bool ui_status_feedback_command(StatusFeedback *sf, const char *arg, char *reply, size_t cap) {
    if (!sf->inited) ui_status_feedback_reset(sf);
    if (!arg || !arg[0] || strcmp(arg, "reset") == 0) {
        sf->forced = SFX_NONE;
        snprintf(reply, cap, "%s", tr("visual.status.reset"));
        return true;
    }
    for (int i = 0; i < SFX_COUNT; i++) {
        if (strcmp(arg, k_ids[i]) != 0) continue;
        sf->forced = i;
        snprintf(reply, cap, tr("visual.status.forced"), arg);
        return true;
    }
    snprintf(reply, cap, "%s", tr("visual.status.usage"));
    return false;
}

void ui_status_feedback_draw(UiContext *ui, const StatusFeedback *sf) {
    const MovementRules *r = qlcg_movement_rules();
    int order[SFX_COUNT], n = 0;
    for (int i = 0; i < SFX_COUNT; i++) {
        if (sf->fades[i].active && i != SFX_501) order[n++] = i;
    }
    /* 按创建顺序叠放，对齐 base 的层叠次序 */
    for (int a = 1; a < n; a++) {
        int key = order[a], b = a - 1;
        while (b >= 0 && sf->fades[order[b]].seq > sf->fades[key].seq) {
            order[b + 1] = order[b];
            b--;
        }
        order[b + 1] = key;
    }
    for (int k = 0; k < n; k++) {
        int id = order[k];
        float op = ui_status_feedback_layer(sf, id);
        uint32_t rgb = (id == SFX_001) ? r->feedback_rgb : k_colors[id % 3];
        if (id <= SFX_003) {
            uint32_t top = rgb | ((uint32_t)(op * r->feedback_opacity * 255.0f + 0.5f) << 24);
            ui_draw_rect_grad(ui, 0, 0, (float)ui->win_w, (float)ui->win_h * r->feedback_height,
                              top, rgb);
        } else {
            uint32_t c = rgb | ((uint32_t)(op * 255.0f + 0.5f) << 24);
            ui_draw_rect_radial(ui, 0, 0, (float)ui->win_w, (float)ui->win_h, c);
        }
    }
}
