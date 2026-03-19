#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include "esp_lcd_panel_io.h"

#ifdef __cplusplus
extern "C" {
#endif

// 屏幕配置（横屏模式 536x240）
#define AMOLED_WIDTH            536
#define AMOLED_HEIGHT           240
#define AMOLED_DEFAULT_BRIGHTNESS 175
#define SEND_BUF_SIZE           (0x4000)

// 引脚定义 - T-Display-S3 AMOLED
#define AMOLED_PIN_DATA0        18
#define AMOLED_PIN_DATA1        7
#define AMOLED_PIN_DATA2        48
#define AMOLED_PIN_DATA3        5
#define AMOLED_PIN_SCK          47
#define AMOLED_PIN_CS           6
#define AMOLED_PIN_RST          17
#define AMOLED_PIN_TE           9
#define AMOLED_PIN_POWER        38  // AMOLED 电源控制引脚

// T-Display-S3 v1.0 无触摸功能

// 初始化
esp_err_t display_manager_init(void);
esp_err_t display_manager_deinit(void);

// 基本控制
void display_manager_clear(uint16_t color);
void display_manager_set_brightness(uint8_t brightness);
void display_manager_sleep(void);
void display_manager_wakeup(void);

// 状态显示
void display_manager_show_boot_screen(void);
void display_manager_show_wifi_status(bool connected);
void display_manager_show_telegram_status(bool connected);
void display_manager_show_system_message(const char *msg);

// 获取 SPI 句柄（用于直接显示操作）
void *display_manager_get_spi_handle(void);

// SPI 互斥锁（用于多线程安全访问）
void display_manager_spi_lock(void);
void display_manager_spi_unlock(void);

// 获取面板配置
void display_manager_get_panel_config(uint16_t *width, uint16_t *height);

#ifdef __cplusplus
}
#endif
