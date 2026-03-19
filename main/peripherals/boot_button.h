/**
 * @file boot_button.h
 * @brief Boot 按钮驱动 - 支持单击、双击、长按检测
 */

#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 按键事件类型 */
typedef enum {
    BOOT_BTN_SINGLE_CLICK,
    BOOT_BTN_DOUBLE_CLICK,
    BOOT_BTN_LONG_PRESS,
    BOOT_BTN_VERY_LONG_PRESS,
} boot_button_event_t;

/* 回调函数类型 */
typedef void (*boot_button_cb_t)(void);

/* 初始化 Boot 按钮 */
esp_err_t boot_button_init(void);

/* 注册回调函数 */
void boot_button_register_callback(boot_button_event_t event, boot_button_cb_t cb);

#ifdef __cplusplus
}
#endif
