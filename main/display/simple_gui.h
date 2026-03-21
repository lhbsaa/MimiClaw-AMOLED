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

// ============================================================================
// 动画效果
// ============================================================================

// AI 思考动画帧更新（返回 true 表示动画仍在进行）
bool gui_animate_ai_thinking(int frame);

// 启动画面动画帧更新
bool gui_animate_boot_screen(int frame);

// 消息滑入动画（返回当前 Y 位置，-1 表示动画结束）
int16_t gui_animate_message_slide(int16_t target_y, int *anim_state);

// ============================================================================
// 增强版页面显示
// ============================================================================

// 增强版 Home 页面 - 更紧凑的信息展示
void gui_show_home_page_v2(const char *time_str, const char *date_str, 
                            const char *greeting, bool ai_busy, uint8_t battery_pct,
                            bool wifi_ok, bool tg_ok, int msg_count);

// 紧凑状态栏
void gui_show_status_bar_v2(const char *time_str, bool wifi_ok, bool tg_ok, 
                              uint8_t battery_pct, const char *page_title);

// AI 状态区域（用于动画更新）
void gui_show_ai_status_area(bool busy, int anim_frame);

// ============================================================================
// 快捷菜单系统
// ============================================================================

// 菜单项定义
typedef struct {
    const char *label;      /* 菜单项文字 */
    const char *icon;       /* 图标 (ASCII 字符) */
    void (*action)(void);   /* 回调函数 */
} menu_item_t;

// 显示快捷菜单
void gui_show_quick_menu(const menu_item_t *items, int count, int selected);

// ============================================================================
// AOD 常亮显示模式
// ============================================================================

// 启用 AOD 模式
void gui_aod_enable(void);

// 禁用 AOD 模式
void gui_aod_disable(void);

// 更新 AOD 显示（每分钟调用）
void gui_aod_update(const char *time_str, uint8_t battery_pct, bool wifi_ok);

// 检查 AOD 是否启用
bool gui_aod_is_enabled(void);

// ============================================================================
// 按钮反馈效果
// ============================================================================

// 按钮按下视觉反馈（边框闪烁）
void gui_button_press_feedback(void);

// 操作确认动画
void gui_action_confirm_flash(void);

// ============================================================================
// 轻量级 Emoji 支持 (12x12 位图)
// ============================================================================

// Emoji ID 定义
typedef enum {
    EMOJI_NONE = 0,
    // 状态类
    EMOJI_OK,           // ✓
    EMOJI_FAIL,         // ✗
    EMOJI_WARN,         // ⚠
    EMOJI_INFO,         // ℹ
    EMOJI_QUESTION,     // ?
    
    // 设备类
    EMOJI_BATTERY,      // 🔋
    EMOJI_BATTERY_LOW,  // 🔋低电量
    EMOJI_WIFI,         // 📶
    EMOJI_WIFI_OFF,     // 📶断开
    
    // AI类
    EMOJI_ROBOT,        // 🤖
    EMOJI_BRAIN,        // 🧠
    EMOJI_LIGHT,        // 💡
    EMOJI_SPARK,        // ⚡
    
    // 通讯类
    EMOJI_MSG,          // 📨
    EMOJI_CHAT,         // 💬
    EMOJI_SEND,         // 📤
    
    // 情感类
    EMOJI_SMILE,        // 😊
    EMOJI_SLEEP,        // 😴
    EMOJI_THINK,        // 🤔
    
    // 时间类
    EMOJI_CLOCK,        // 🕐
    EMOJI_SUN,          // ☀
    EMOJI_MOON,         // 🌙
    
    // 其他
    EMOJI_HOME,         // 🏠
    EMOJI_SETTINGS,     // ⚙
    EMOJI_SEARCH,       // 🔍
    EMOJI_RESTART,      // 🔄
    
    EMOJI_COUNT
} emoji_id_t;

// 绘制单个 emoji
void gui_draw_emoji(int16_t x, int16_t y, emoji_id_t emoji, uint8_t scale);

// 绘制带 emoji 的字符串 (emoji 用 \x01\xNN 编码)
void gui_draw_string_with_emoji(int16_t x, int16_t y, const char *str, 
                                 uint16_t color, uint8_t scale);

// 获取 emoji 宽度
int16_t gui_emoji_width(uint8_t scale);

#ifdef __cplusplus
}
#endif
