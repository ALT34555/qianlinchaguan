#include "app_state.h"

void app_graphics_release(bool context_lost) {
    if(context_lost) qlcg_gl_context_reset();
    app_lowres_release();
    qlcg_terrain_gpu_release(&g_app.terrain);
    qlcg_atlas_shutdown(&g_app.atlas);
    ui_gpu_release(&g_app.ui);
}

bool app_graphics_restore(void) {
    if(!ui_gpu_restore(&g_app.ui) || !qlcg_atlas_gpu_restore(&g_app.atlas) ||
       !qlcg_terrain_gpu_restore(&g_app.terrain)) {
        app_graphics_release(false);
        return false;
    }
    if(g_app.world) qlcg_atlas_upload_world(&g_app.atlas,g_app.world);
    if(g_app.region) qlcg_terrain_upload_region(&g_app.terrain,g_app.region,&g_app.settings);
    return true;
}
