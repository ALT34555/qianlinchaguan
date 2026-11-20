#ifndef QLCG_WIN32_DISPLAY_SETTINGS_H
#define QLCG_WIN32_DISPLAY_SETTINGS_H

#include "core/game_settings.h"

RenderDevice win32_read_render_device(void);
bool win32_set_render_device(RenderDevice device);
bool win32_set_window_mode(WindowMode mode);
void win32_release_window_mode(void);

#endif
