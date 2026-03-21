#pragma once

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// UI 初始化
esp_err_t ui_main_init(void);
void ui_main_deinit(void);

// 状态更新
void ui_set_wifi_status(bool connected);
void ui_set_telegram_status(bool connected);
void ui_set_battery_level(uint8_t percentage);
void ui_set_time(const char *time_str);

// AI 状态
void ui_set_ai_busy(bool busy);
bool ui_is_ai_busy(void);

// 消息计数
int ui_get_message_count(void);

// 兼容性函数（旧代码使用）
void ui_update_wifi_status(bool connected);
void ui_update_telegram_status(bool connected);
void ui_update_battery_level(uint8_t percentage);
void ui_update_time(const char *time_str);

// 消息显示
void ui_add_message(const char *sender, const char *msg, bool is_me);
void ui_add_system_message(const char *msg);
void ui_clear_messages(void);

// 输入区域
void ui_set_input_text(const char *text);
const char *ui_get_input_text(void);
void ui_clear_input(void);

// 显示/隐藏键盘
void ui_show_keyboard(bool show);

// 屏幕导航
void ui_show_main_screen(void);
void ui_show_settings_screen(void);

// AOD 常亮显示模式
void ui_aod_enable(void);
void ui_aod_disable(void);
bool ui_aod_is_enabled(void);

#ifdef __cplusplus
}
#endif
