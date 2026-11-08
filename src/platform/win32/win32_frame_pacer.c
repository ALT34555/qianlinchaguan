#ifdef _WIN32
#include "platform/win32/native.h"
#include "platform/platform.h"
#include "app/frame_pacer.h"
#include "core/math_util.h"

void qlcg_frame_pacer_init(FramePacer *pacer) {
    pacer->timer = CreateWaitableTimerExW(NULL, NULL,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
}

/* 高精度休眠，末段校准帧截止时间 */
void qlcg_frame_pacer_wait(FramePacer *pacer, double frame_start_ms, int fps_cap) {
    if (fps_cap <= 0) return;
    double deadline = frame_start_ms + 1000.0 / (double)fps_cap;
    double remaining = deadline - qlcg_now_ms();
    if (remaining <= 0.0) return;
    if (pacer->timer && remaining > 0.75) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)((remaining - 0.75) * 10000.0);
        if (SetWaitableTimer(pacer->timer, &due, 0, NULL, NULL, FALSE))
            WaitForSingleObject(pacer->timer, INFINITE);
    }
    while (qlcg_now_ms() < deadline) {
        if (pacer->timer) YieldProcessor();
        else SwitchToThread();
    }
}

void qlcg_frame_pacer_shutdown(FramePacer *pacer) {
    if (pacer->timer) CloseHandle(pacer->timer);
    pacer->timer = NULL;
}

#endif
