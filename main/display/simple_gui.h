#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

// 屏幕尺寸
#define SCREEN_WIDTH  536
#define SCREEN_HEIGHT 240

// 颜色定义（RGB565 字节交换后 - 与屏幕匹配）
#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_RED         0x00F8   // rgb565(255,0,0) swapped
#define COLOR_GREEN       0xE007   // rgb565(0,255,0) swapped
#define COLOR_BLUE        0x1F00   // rgb565(0,0,255) swapped
#define COLOR_YELLOW      0xE0FF   // rgb565(255,255,0) swapped
#define COLOR_CYAN        0xFF07   // rgb565(0,255,255) swapped
#define COLOR_MAGENTA     0xF81F   // rgb565(255,0,255) swapped
#define COLOR_GRAY        0x1084   // rgb565(132,132,132) swapped
#define COLOR_DARK_GRAY   0x0842   // rgb565(66,66,66) swapped
#define COLOR_LIGHT_GRAY  0x18C6   // rgb565(198,198,198) swapped
#define COLOR_ORANGE      0x20FD   // rgb565(255,165,0) swapped
#define COLOR_NAVY        0x0010   // rgb565(0,0,128) swapped

// 字体大小
typedef enum {
    FONT_8x8 = 0,
    FONT_16x16,
    FONT_24x24
} font_size_t;

// 初始化简单 GUI
esp_err_t simple_gui_init(void);

// 刷新帧缓冲到屏幕
void gui_flush(void);

// 刷新指定区域到屏幕
void gui_flush_region(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

// 清屏
void gui_clear_screen(uint16_t color);

// 绘制像素点
void gui_draw_pixel(int16_t x, int16_t y, uint16_t color);

// 绘制水平线
void gui_draw_hline(int16_t x0, int16_t x1, int16_t y, uint16_t color);

// 绘制垂直线
void gui_draw_vline(int16_t x, int16_t y0, int16_t y1, uint16_t color);

// 绘制矩形（空心）
void gui_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

// 绘制填充矩形
void gui_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

// 绘制圆形（空心）
void gui_draw_circle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

// 绘制填充圆形
void gui_fill_circle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

// 绘制字符
void gui_draw_char(int16_t x, int16_t y, char c, uint16_t color, font_size_t size);

// 绘制字符串
void gui_draw_string(int16_t x, int16_t y, const char *str, uint16_t color, font_size_t size);

// 获取字符串宽度
int16_t gui_string_width(const char *str, font_size_t size);

// ============================================================================
// 页面 1: 启动画面
// ============================================================================
void gui_show_boot_screen(void);

// ============================================================================
// 页面 2: 主控制器/状态栏
// ============================================================================
void gui_show_status_bar(const char *time_str, bool wifi_connected, bool telegram_connected, uint8_t battery_pct, const char *page_title);

// Home页面（时间、日期、问候语、AI状态）
void gui_show_home_page(const char *time_str, const char *date_str, const char *greeting, 
                         bool ai_busy, uint8_t battery_pct);

// ============================================================================
// 页面 3: 系统信息
// ============================================================================
void gui_show_system_info(const char *wifi_ip, const char *llm_provider, 
                           uint32_t free_heap, uint32_t free_psram,
                           uint8_t battery_pct, uint32_t uptime_sec);

// 显示单条系统消息（用于ui_add_system_message）
void gui_show_system_message(const char *msg);

// ============================================================================
// 页面 4: 消息气泡
// ============================================================================
void gui_show_message(const char *sender, const char *msg, bool is_me, int16_t y_pos);

// ============================================================================
// 页面 5: 日志
// ============================================================================
void gui_add_log(const char *tag, const char *message);
void gui_show_log(void);
void gui_clear_log(void);

#ifdef __cplusplus
}
#endif
