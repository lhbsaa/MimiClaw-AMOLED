#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// CST816S 触控芯片配置
#define CST816S_ADDR            0x15    // I2C 地址
#define CST816S_SDA_PIN         3       // SDA 引脚
#define CST816S_SCL_PIN         2       // SCL 引脚
#define CST816S_IRQ_PIN         21      // 中断引脚
#define CST816S_RST_PIN         -1      // 复位引脚（未使用）

// 触控事件类型
typedef enum {
    CST816S_EVT_NONE = 0x00,
    CST816S_EVT_DOWN = 0x01,
    CST816S_EVT_UP = 0x02,
    CST816S_EVT_CONTACT = 0x03,
} cst816s_event_t;

// 初始化
esp_err_t touch_cst816s_init(void);
esp_err_t touch_cst816s_deinit(void);

// 读取触控数据
bool touch_cst816s_read(int16_t *x, int16_t *y, cst816s_event_t *event);

// 检查是否有触控
bool touch_cst816s_is_pressed(void);

// 设置触控回调
void touch_cst816s_set_callback(void (*callback)(int16_t x, int16_t y, cst816s_event_t event));

// 休眠/唤醒
void touch_cst816s_sleep(void);
void touch_cst816s_wakeup(void);

// 兼容层函数（供 LVGL 使用）
bool touch_read(int16_t *x, int16_t *y);

#ifdef __cplusplus
}
#endif
