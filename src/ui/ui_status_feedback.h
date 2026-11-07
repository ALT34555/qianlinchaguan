#ifndef QLCG_UI_STATUS_FEEDBACK_H
#define QLCG_UI_STATUS_FEEDBACK_H

#include <stdbool.h>
#include <stddef.h>
#include "ui_core.h"

/* 七种特效：0xx 顶部渐变，1xx 晕影，501 模糊 */
#define SFX_COUNT 7

typedef enum {
    SFX_NONE = -1,
    SFX_001 = 0, SFX_002, SFX_003,
    SFX_101, SFX_102, SFX_103,
    SFX_501
} StatusEffect;

/* 单层淡入淡出：from/to 强度与时间戳（秒） */
typedef struct {
    bool  active;
    float from, to;
    float changed_at, started_at;
    int   seq;
} StatusFade;

/* 状态反馈状态机：强制优先，自动 001 受设置开关约束 */
typedef struct {
    StatusFade fades[SFX_COUNT];
    int        current;
    int        forced;
    int        seq_counter;
    float      now;
    float      blur_px;
    bool       inited;
} StatusFeedback;

void  ui_status_feedback_reset(StatusFeedback *sf);
void  ui_status_feedback_update(StatusFeedback *sf, int automatic, bool enabled, float now);
float ui_status_feedback_blur(const StatusFeedback *sf);
/* 单层强度：501 为模糊半径，其余为含呼吸的不透明度 */
float ui_status_feedback_layer(const StatusFeedback *sf, int id);
bool  ui_status_feedback_command(StatusFeedback *sf, const char *arg, char *reply, size_t cap);
void  ui_status_feedback_draw(UiContext *ui, const StatusFeedback *sf);

#endif /* QLCG_UI_STATUS_FEEDBACK_H */
