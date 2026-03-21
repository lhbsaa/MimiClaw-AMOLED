/**
 * @file boot_button.h
 * @brief Boot 按钮驱动 - 支持单击、双击、三击、长按检测
 */

#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 按键事件类型 */
typedef enum {
    BOOT_BTN_SINGLE_CLICK,      /* 单击 - 上下文相关操作 */
    BOOT_BTN_DOUBLE_CLICK,      /* 双击 - 快捷操作 */
    BOOT_BTN_TRIPLE_CLICK,      /* 三击 - 返回首页 */
    BOOT_BTN_LONG_PRESS,        /* 长按 (500-2000ms) - 显示菜单 */
    BOOT_BTN_VERY_LONG_PRESS,   /* 超长按 (>3000ms) - 重启 */
    BOOT_BTN_HOLDING,           /* 持续按住 - 用于亮度调节等 */
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
