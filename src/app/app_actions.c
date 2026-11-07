/* 顶部提示与聊天日志 */
#include "app/app_state.h"
#include "ui/ui_scroll.h"

/* 推送通知；不带类型时按信息处理 */
void app_push_toast(const char *msg) {
    app_push_toast_k(UI_TOAST_INFO, NULL, msg);
}

/* 带语义类型的通知推送 */
void app_push_toast_k(int kind, const char *title, const char *msg) {
    ui_toast_push(&g_app.toasts, kind, title, msg, 3.6f);
}

/* 推送聊天与指令日志 */
void app_push_chat(const char *line) {
    g_app.chat_last_message_ms = qlcg_now_ms();
    if (!line) line = "";
    if (g_app.chat_log_count < MAX_CHAT_MSGS) {
        ui_u8_copy(g_app.chat_logs[g_app.chat_log_count++],
                   sizeof(g_app.chat_logs[0]), line);
    } else {
        for (int i = 0; i < MAX_CHAT_MSGS - 1; i++) {
            memcpy(g_app.chat_logs[i], g_app.chat_logs[i + 1], sizeof(g_app.chat_logs[0]));
        }
        ui_u8_copy(g_app.chat_logs[MAX_CHAT_MSGS - 1],
                   sizeof(g_app.chat_logs[0]), line);
    }
    ui_scroll_reset(&g_app.ui, UIS_CHAT_LIST);
}
