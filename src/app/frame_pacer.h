#ifndef QLCG_FRAME_PACER_H
#define QLCG_FRAME_PACER_H

#include <stddef.h>

typedef struct {
    void *timer;
} FramePacer;

void qlcg_frame_pacer_init(FramePacer *pacer);
void qlcg_frame_pacer_wait(FramePacer *pacer, double frame_start_ms, int fps_cap);
void qlcg_frame_pacer_shutdown(FramePacer *pacer);

#endif
