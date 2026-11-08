#include "app/app_state.h"
#include "app/frame_pacer.h"
#include "core/dev_tools.h"

int main(int argc, char **argv) {
#ifdef QLCG_DEV_TOOLS
    if (qlcg_dev_help(argc, argv)) return 0;
#else
    (void)argc; (void)argv;
#endif
    if (!app_initialize()) return 1;
#ifdef QLCG_DEV_TOOLS
    int dev_exit = 0;
    if (qlcg_dev_start(argc, argv, &dev_exit)) { app_shutdown(); return dev_exit; }
#endif
    FramePacer pacer;
    qlcg_frame_pacer_init(&pacer);
    double last_ms = qlcg_time_ms();
    while (g_app.running) {
        double start_ms = qlcg_time_ms();
        qlcg_platform_poll();
        if (!g_app.running) break;
        double now_ms = qlcg_time_ms();
        float dt = (float)((now_ms - last_ms) * 0.001);
        last_ms = now_ms;
        if (dt > 0.0001f) g_app.fps_smooth = g_app.fps_smooth * 0.92f + (1.0f / dt) * 0.08f;
        dt = qlcg_clampf(dt, 0.001f, 0.05f);
        app_update(dt);
        app_render_frame(dt);
        qlcg_frame_pacer_wait(&pacer, start_ms, g_app.settings.frame_rate);
    }
    qlcg_frame_pacer_shutdown(&pacer);
    app_shutdown();
    return 0;
}
