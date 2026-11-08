#include "ui/ui_input.h"
#include "platform/platform.h"
bool ui_clip_get_utf8(char *dst, size_t cap) { return qlcg_platform_clip_get(dst, cap); }
bool ui_clip_set_utf8(const char *text) { return qlcg_platform_clip_set(text); }
