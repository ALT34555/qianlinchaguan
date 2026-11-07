/* 输入绑定：动作表、默认键位、键名标签 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "input_bind.h"
#include <stdio.h>
#include <string.h>
#include "i18n.h"

static const char *const ACTION_KEYS[ACT_COUNT] = {
    "action.0", "action.1", "action.2", "action.3", "action.4",
    "action.5", "action.6", "action.7", "action.8", "action.9",
    "action.10"
};

const char *qlcg_action_name(int act) {
    if (act < 0 || act >= ACT_COUNT) return "?";
    return tr(ACTION_KEYS[act]);
}

/* 未绑定的哨兵 */
#define NO_KEY  ((uint16_t)0)
#define NO_IDX  ((int8_t)-1)

void qlcg_binds_default(QlBind *b) {
    if (!b) return;
    memset(b, 0, sizeof(QlBind) * ACT_COUNT);
    for (int i = 0; i < ACT_COUNT; i++) {
        b[i].key   = NO_KEY;
        b[i].mods  = 0;
        b[i].mouse = NO_IDX;
        b[i].pad   = NO_IDX;
        b[i].touch = NO_IDX;
    }
    b[ACT_FORWARD].key     = 'W';
    b[ACT_BACK].key        = 'S';
    b[ACT_LEFT].key        = 'A';
    b[ACT_RIGHT].key       = 'D';
    b[ACT_JUMP].key        = VK_SPACE;
    b[ACT_SPRINT].key      = VK_SHIFT;
    b[ACT_CROUCH].key      = VK_CONTROL;
    b[ACT_VIEW_TOGGLE].key = 'M';
    b[ACT_DEBUG].key       = VK_F12;
    b[ACT_UNDO].key        = 'Z';
    b[ACT_UNDO].mods       = QL_MOD_CTRL;
    b[ACT_QUICKSAVE].key   = 'P';
    b[ACT_QUICKSAVE].mods  = QL_MOD_CTRL;
}

const char *qlcg_bind_key_label(uint16_t vk, uint8_t mods, char *buf, int cap) {
    if (!buf || cap <= 0) return "";
    buf[0] = '\0';
    if (vk == NO_KEY) {
        snprintf(buf, cap, "%s", tr("key.unset"));
        return buf;
    }
    char pre[24] = "";
    if (mods & QL_MOD_CTRL)  strncat(pre, "Ctrl+", sizeof(pre) - strlen(pre) - 1);
    if (mods & QL_MOD_SHIFT) strncat(pre, "Shift+", sizeof(pre) - strlen(pre) - 1);
    if (mods & QL_MOD_ALT)   strncat(pre, "Alt+", sizeof(pre) - strlen(pre) - 1);

    char key[32];
    if (vk >= 'A' && vk <= 'Z')            snprintf(key, sizeof(key), "%c", (char)vk);
    else if (vk >= '0' && vk <= '9')       snprintf(key, sizeof(key), "%c", (char)vk);
    else if (vk >= VK_F1 && vk <= VK_F12)  snprintf(key, sizeof(key), "F%d", (int)(vk - VK_F1) + 1);
    else if (vk == VK_SPACE)               snprintf(key, sizeof(key), "%s", tr("key.space"));
    else if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) snprintf(key, sizeof(key), "Shift");
    else if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) snprintf(key, sizeof(key), "Ctrl");
    else if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) snprintf(key, sizeof(key), "Alt");
    else if (vk == VK_RETURN)              snprintf(key, sizeof(key), "%s", tr("key.enter"));
    else if (vk == VK_ESCAPE)              snprintf(key, sizeof(key), "Esc");
    else if (vk == VK_TAB)                 snprintf(key, sizeof(key), "Tab");
    else if (vk == VK_BACK)                snprintf(key, sizeof(key), "%s", tr("key.backspace"));
    else if (vk == VK_UP)                  snprintf(key, sizeof(key), "↑");
    else if (vk == VK_DOWN)                snprintf(key, sizeof(key), "↓");
    else if (vk == VK_LEFT)                snprintf(key, sizeof(key), "←");
    else if (vk == VK_RIGHT)               snprintf(key, sizeof(key), "→");
    else if (vk == VK_OEM_2)               snprintf(key, sizeof(key), "/");
    else if (vk == VK_OEM_3)               snprintf(key, sizeof(key), "`");
    else                                   snprintf(key, sizeof(key), "VK%02X", vk);

    snprintf(buf, cap, "%s%s", pre, key);
    return buf;
}

/* 归一化：左右修饰键视为同一枚 */
uint16_t qlcg_norm_key(uint16_t vk) {
    switch (vk) {
        case VK_LSHIFT: case VK_RSHIFT:     return VK_SHIFT;
        case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
        case VK_LMENU: case VK_RMENU:       return VK_MENU;
        default: return vk;
    }
}

int qlcg_bind_conflict(const QlBind *b, int act, uint16_t vk, uint8_t mods) {
    if (!b || vk == NO_KEY) return -1;
    uint16_t k = qlcg_norm_key(vk);
    for (int i = 0; i < ACT_COUNT; i++) {
        if (i == act) continue;
        if (b[i].key == NO_KEY) continue;
        if (qlcg_norm_key(b[i].key) == k && b[i].mods == mods) return i;
    }
    return -1;
}

void qlcg_bind_swap_key(QlBind *binds, int act_a, int act_b) {
    if (!binds) return;
    if (act_a < 0 || act_a >= ACT_COUNT || act_b < 0 || act_b >= ACT_COUNT) return;
    if (act_a == act_b) return;
    uint16_t tk = binds[act_a].key;
    uint8_t  tm = binds[act_a].mods;
    binds[act_a].key  = binds[act_b].key;
    binds[act_a].mods = binds[act_b].mods;
    binds[act_b].key  = tk;
    binds[act_b].mods = tm;
}
