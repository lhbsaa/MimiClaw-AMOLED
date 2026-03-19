/**
 * @file ui_main.c
 * @brief UI 主控制器 - 页面导航和显示管理
 */

#include "ui_main.h"
#include "simple_gui.h"
#include "display_manager.h"
#include "../peripherals/boot_button.h"
#include "../peripherals/battery_adc.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_system.h"

static const char *TAG = "ui_main";

// ============================================================================
// 页面定义
// ============================================================================
typedef enum {
    PAGE_BOOT,          // 页面 1: 启动画面
    PAGE_STATUS_BAR,    // 页面 2: 状态栏/主控制器
    PAGE_SYSTEM_MSG,    // 页面 3: 系统消息
    PAGE_MESSAGE,       // 页面 4: 消息气泡
    PAGE_LOG,           // 页面 5: 日志
    PAGE_COUNT
} ui_page_t;

// 可导航的页面列表 (从状态栏开始)
static const ui_page_t s_nav_pages[] = {
    PAGE_STATUS_BAR,
    PAGE_SYSTEM_MSG,
    PAGE_MESSAGE,
    PAGE_LOG,
};
#define NAV_PAGE_COUNT  (sizeof(s_nav_pages) / sizeof(s_nav_pages[0]))

// 页面名称
static const char *s_page_names[] = {
    [PAGE_BOOT]       = "Boot",
    [PAGE_STATUS_BAR] = "Home",
    [PAGE_SYSTEM_MSG] = "System",
    [PAGE_MESSAGE]    = "Message",
    [PAGE_LOG]        = "Logs",
};

// ============================================================================
// UI 状态
// ============================================================================
static bool wifi_connected = false;
static bool telegram_connected = false;
static char current_time[32] = "";
static uint8_t battery_level = 0;   // 电池电量百分比
static uint8_t s_battery_counter = 0;  // 电池更新计数器
static int16_t message_y_pos = 40;
static ui_page_t s_current_page = PAGE_BOOT;
static int s_nav_index = 0;
static bool s_nav_initialized = false;
static bool s_splash_done = false;  // 启动画面是否结束

// 启动页面定时器
static esp_timer_handle_t s_splash_timer = NULL;

// 时间更新定时器
static esp_timer_handle_t s_time_timer = NULL;

// 启动期间缓存的消息
#define MAX_PENDING_MSGS  4
static char s_pending_msgs[MAX_PENDING_MSGS][128];
static int s_pending_count = 0;

// 消息历史缓存 (用于页面切换时重绘)
#define MAX_MSG_HISTORY  8
typedef struct {
    char sender[16];
    char msg[128];
    bool is_me;
} msg_history_t;
static msg_history_t s_msg_history[MAX_MSG_HISTORY];
static int s_msg_count = 0;

// ============================================================================
// 前向声明
// ============================================================================
static void update_status_bar(void);
static void on_single_click(void);
static void on_double_click(void);
static void on_long_press(void);
static void on_very_long_press(void);
static void splash_timer_callback(void *arg);
static void ui_show_page(ui_page_t page);

// ============================================================================
// 页面切换
// ============================================================================
static void ui_next_page(void)
{
    s_nav_index = (s_nav_index + 1) % NAV_PAGE_COUNT;
    s_current_page = s_nav_pages[s_nav_index];
    ESP_LOGI(TAG, "Next page: %s (%d/%d)", s_page_names[s_current_page], s_nav_index + 1, NAV_PAGE_COUNT);
    ui_show_page(s_current_page);
}

static void ui_show_page(ui_page_t page)
{
    // 清屏
    gui_clear_screen(COLOR_BLACK);
    
    // 页面标题
    const char *page_title = s_page_names[page];
    
    // 先显示状态栏（所有页面共用）
    gui_show_status_bar(current_time[0] ? current_time : "--:--", wifi_connected, telegram_connected, battery_level, page_title);
    
    switch (page) {
        case PAGE_STATUS_BAR:
            // 状态栏页面 - 显示欢迎信息
            gui_draw_string(200, 100, "MimiClaw", COLOR_WHITE, 4);
            gui_draw_string(230, 145, "Ready", COLOR_GREEN, 2);
            break;
            
        case PAGE_SYSTEM_MSG:
            // 系统消息页面
            gui_draw_hline(12, SCREEN_WIDTH - 12, 45, COLOR_DARK_GRAY);
            gui_show_system_message("Running");
            break;
            
        case PAGE_MESSAGE:
            // 消息页面 - 重绘历史消息
            gui_draw_hline(12, SCREEN_WIDTH - 12, 45, COLOR_DARK_GRAY);
            if (s_msg_count > 0) {
                int16_t y = 50;
                for (int i = 0; i < s_msg_count && y < SCREEN_HEIGHT - 60; i++) {
                    gui_show_message(s_msg_history[i].sender, s_msg_history[i].msg, 
                                     s_msg_history[i].is_me, y);
                    y += 80;
                }
            } else {
                gui_draw_string(15, 70, "(no messages)", COLOR_DARK_GRAY, 2);
            }
            break;
            
        case PAGE_LOG:
            // 日志页面 - 直接调用 gui_show_log
            gui_show_log();
            break;
            
        default:
            break;
    }
    
    // 底部操作提示
    gui_draw_string(12, SCREEN_HEIGHT - 20, "BOOT:Next", COLOR_DARK_GRAY, 1);
    
    gui_flush();
}

// ============================================================================
// 状态更新
// ============================================================================
static void update_status_bar(void)
{
    if (s_current_page != PAGE_BOOT) {
        gui_show_status_bar(current_time[0] ? current_time : "--:--", wifi_connected, telegram_connected, battery_level, s_page_names[s_current_page]);
        gui_flush();
    }
}

void ui_set_wifi_status(bool connected)
{
    wifi_connected = connected;
    ESP_LOGI(TAG, "WiFi status: %s", connected ? "connected" : "disconnected");
    update_status_bar();
}

void ui_set_telegram_status(bool connected)
{
    telegram_connected = connected;
    ESP_LOGI(TAG, "Telegram status: %s", connected ? "connected" : "disconnected");
    update_status_bar();
}

void ui_set_battery_level(uint8_t percentage)
{
    if (percentage > 100) percentage = 100;
    battery_level = percentage;
    ESP_LOGI(TAG, "Battery level: %d%%", percentage);
    update_status_bar();
}

void ui_set_time(const char *time_str)
{
    if (time_str) {
        strncpy(current_time, time_str, sizeof(current_time) - 1);
        current_time[sizeof(current_time) - 1] = '\0';
    } else {
        current_time[0] = '\0';
    }
    ESP_LOGI(TAG, "Time: %s", current_time);
    update_status_bar();
}

void ui_add_message(const char *sender, const char *msg, bool is_me)
{
    if (!sender || !msg) return;
    
    ESP_LOGI(TAG, "Adding message from %s (%d bytes)", sender, (int)strlen(msg));
    
    // 唤醒屏幕（用户可能在休眠状态）
    display_manager_wakeup();
    
    // 添加到日志（完整内容）
    gui_add_log(sender, msg);
    
    // 如果启动画面未结束，只记录日志不显示
    if (!s_splash_done) {
        return;
    }
    
    // 如果当前不在消息页面，切换到消息页面并清屏
    if (s_current_page != PAGE_MESSAGE && s_current_page != PAGE_LOG) {
        s_current_page = PAGE_MESSAGE;
        message_y_pos = 40;  // 重置消息位置
        gui_clear_screen(COLOR_BLACK);
        gui_show_status_bar(current_time[0] ? current_time : "--:--", wifi_connected, telegram_connected, battery_level, "Messages");
        gui_draw_hline(12, SCREEN_WIDTH - 12, 45, COLOR_DARK_GRAY);
    }
    
    // 提取简短摘要用于小屏显示（前100字符或第一段）
    char summary[128];
    int msg_len = strlen(msg);
    int summary_len = msg_len < 100 ? msg_len : 100;
    strncpy(summary, msg, summary_len);
    summary[summary_len] = '\0';
    
    // 截断到第一个换行或句号
    char *newline = strchr(summary, '\n');
    char *period = strchr(summary, '.');
    char *cut = NULL;
    if (newline && period) {
        cut = (newline < period) ? newline : period;
    } else if (newline) {
        cut = newline;
    } else if (period) {
        cut = period + 3; // 保留句号
    }
    if (cut) {
        *cut = '\0';
    }
    
    // 如果摘要太短但原文很长，添加省略号
    int final_len = strlen(summary);
    if (final_len < 60 && msg_len > 100) {
        strcat(summary, "...");
    }
    
    // 显示消息气泡（使用简短摘要）
    gui_show_message(sender, summary, is_me, message_y_pos);
    
    // 存入消息历史缓存
    if (s_msg_count < MAX_MSG_HISTORY) {
        strncpy(s_msg_history[s_msg_count].sender, sender, sizeof(s_msg_history[0].sender) - 1);
        s_msg_history[s_msg_count].sender[sizeof(s_msg_history[0].sender) - 1] = '\0';
        strncpy(s_msg_history[s_msg_count].msg, summary, sizeof(s_msg_history[0].msg) - 1);
        s_msg_history[s_msg_count].msg[sizeof(s_msg_history[0].msg) - 1] = '\0';
        s_msg_history[s_msg_count].is_me = is_me;
        s_msg_count++;
    } else {
        // 滚动：移除最旧的
        for (int i = 0; i < MAX_MSG_HISTORY - 1; i++) {
            s_msg_history[i] = s_msg_history[i + 1];
        }
        strncpy(s_msg_history[MAX_MSG_HISTORY - 1].sender, sender, sizeof(s_msg_history[0].sender) - 1);
        s_msg_history[MAX_MSG_HISTORY - 1].sender[sizeof(s_msg_history[0].sender) - 1] = '\0';
        strncpy(s_msg_history[MAX_MSG_HISTORY - 1].msg, summary, sizeof(s_msg_history[0].msg) - 1);
        s_msg_history[MAX_MSG_HISTORY - 1].msg[sizeof(s_msg_history[0].msg) - 1] = '\0';
        s_msg_history[MAX_MSG_HISTORY - 1].is_me = is_me;
    }
    
    message_y_pos += 80;  // 2号字体需要更大的间距
    
    if (message_y_pos > SCREEN_HEIGHT - 50) {
        // 消息超出屏幕，先显示当前内容
        gui_flush();
        vTaskDelay(pdMS_TO_TICKS(3000));
        // 清屏重置，同时清空历史缓存
        gui_clear_screen(COLOR_BLACK);
        message_y_pos = 40;
        s_msg_count = 0;  // 重置消息计数
        gui_show_status_bar(current_time[0] ? current_time : "--:--", wifi_connected, telegram_connected, battery_level, "Messages");
        gui_draw_hline(12, SCREEN_WIDTH - 12, 45, COLOR_DARK_GRAY);
        gui_flush();
    } else {
        gui_flush();
    }
}

void ui_add_system_message(const char *msg)
{
    if (!msg) return;
    
    ESP_LOGI(TAG, "System message: %s", msg);
    
    // 如果启动画面未结束，缓存消息
    if (!s_splash_done) {
        if (s_pending_count < MAX_PENDING_MSGS) {
            strncpy(s_pending_msgs[s_pending_count], msg, sizeof(s_pending_msgs[0]) - 1);
            s_pending_msgs[s_pending_count][sizeof(s_pending_msgs[0]) - 1] = '\0';
            s_pending_count++;
            ESP_LOGI(TAG, "Message cached (%d/%d)", s_pending_count, MAX_PENDING_MSGS);
        }
        return;
    }
    
    // 添加到日志
    gui_add_log("SYS", msg);
    
    // 如果当前在Home页面，切换到系统消息页面
    if (s_current_page == PAGE_STATUS_BAR) {
        s_current_page = PAGE_SYSTEM_MSG;
        gui_clear_screen(COLOR_BLACK);
        gui_show_status_bar(current_time[0] ? current_time : "--:--", wifi_connected, telegram_connected, battery_level, "System");
        gui_draw_hline(12, SCREEN_WIDTH - 12, 45, COLOR_DARK_GRAY);
    }
    
    // 显示系统消息
    gui_show_system_message(msg);
    gui_flush();
}

void ui_clear_messages(void)
{
    message_y_pos = 40;
    gui_clear_screen(COLOR_BLACK);
    update_status_bar();
    gui_flush();
}

// ============================================================================
// 按键回调
// ============================================================================
static void on_single_click(void)
{
    ESP_LOGI(TAG, "Single click - next page");
    
    // 唤醒屏幕
    display_manager_wakeup();
    
    // 如果还在启动页面，先切换到状态栏页面
    if (!s_nav_initialized) {
        s_nav_initialized = true;
        s_splash_done = true;
        s_current_page = PAGE_STATUS_BAR;
        s_nav_index = 0;
        ui_show_page(PAGE_STATUS_BAR);
        
        // 显示启动期间缓存的消息
        for (int i = 0; i < s_pending_count; i++) {
            gui_add_log("SYS", s_pending_msgs[i]);
        }
        s_pending_count = 0;
        return;
    }
    
    ui_next_page();
}

static void on_double_click(void)
{
    ESP_LOGI(TAG, "Double click - next page");
    display_manager_wakeup();
    ui_next_page();
}

static void on_long_press(void)
{
    ESP_LOGI(TAG, "Long press - restart");
    gui_clear_screen(COLOR_BLACK);
    gui_draw_string(180, 100, "Restarting...", COLOR_RED, 3);
    gui_flush();
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static void on_very_long_press(void)
{
    ESP_LOGI(TAG, "Very long press - factory reset (not implemented)");
}

// ============================================================================
// 启动页面定时器
// ============================================================================
static void splash_timer_callback(void *arg)
{
    (void)arg;
    // 10秒后从启动页面自动切换到状态栏页面
    if (s_current_page == PAGE_BOOT) {
        ESP_LOGI(TAG, "Splash timeout - switch to Status page");
        s_nav_initialized = true;
        s_splash_done = true;
        s_current_page = PAGE_STATUS_BAR;
        s_nav_index = 0;
        ui_show_page(PAGE_STATUS_BAR);
        
        // 显示启动期间缓存的消息
        for (int i = 0; i < s_pending_count; i++) {
            gui_add_log("SYS", s_pending_msgs[i]);
        }
        s_pending_count = 0;
    }
}

// ============================================================================
// 时间更新定时器
// ============================================================================
static void time_timer_callback(void *arg)
{
    (void)arg;
    
    // 获取系统时间
    time_t now = time(NULL);
    if (now > 1700000000) {  // 时间已同步
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        snprintf(current_time, sizeof(current_time), "%02d:%02d", tm_info.tm_hour, tm_info.tm_min);
    } else {
        strncpy(current_time, "--:--", sizeof(current_time) - 1);
    }
    
    // 每5秒更新电池电量
    s_battery_counter++;
    if (s_battery_counter >= 5) {
        s_battery_counter = 0;
        if (battery_adc_is_ready()) {
            int pct = battery_get_percent();
            if (pct != battery_level) {
                battery_level = (uint8_t)pct;
                ESP_LOGI(TAG, "Battery: %d%%", pct);
            }
        }
    }
    
    // 更新状态栏
    if (s_splash_done) {
        update_status_bar();
    }
}

// ============================================================================
// 初始化和反初始化
// ============================================================================
void ui_main_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing UI...");
    if (s_splash_timer) {
        esp_timer_delete(s_splash_timer);
        s_splash_timer = NULL;
    }
    if (s_time_timer) {
        esp_timer_delete(s_time_timer);
        s_time_timer = NULL;
    }
}

// 输入区域（空实现，简单 GUI 不支持）
void ui_set_input_text(const char *text) { (void)text; }
const char *ui_get_input_text(void) { return NULL; }
void ui_clear_input(void) { }
void ui_show_keyboard(bool show) { (void)show; }
void ui_show_main_screen(void) { }
void ui_show_settings_screen(void) { }

esp_err_t ui_main_init(void)
{
    ESP_LOGI(TAG, "Initializing UI...");
    
    // 初始化简单 GUI (帧缓冲模式)
    esp_err_t ret = simple_gui_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GUI");
        return ret;
    }
    
    // 初始化 Boot 按钮
    ret = boot_button_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Boot button init failed: %s", esp_err_to_name(ret));
    } else {
        // 注册按键回调
        boot_button_register_callback(BOOT_BTN_SINGLE_CLICK, on_single_click);
        boot_button_register_callback(BOOT_BTN_DOUBLE_CLICK, on_double_click);
        boot_button_register_callback(BOOT_BTN_LONG_PRESS, on_long_press);
        boot_button_register_callback(BOOT_BTN_VERY_LONG_PRESS, on_very_long_press);
    }
    
    // 创建启动页面定时器 (10秒后自动切换)
    const esp_timer_create_args_t splash_timer_args = {
        .callback = &splash_timer_callback,
        .name = "splash_timer"
    };
    ret = esp_timer_create(&splash_timer_args, &s_splash_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create splash timer: %s", esp_err_to_name(ret));
    }
    
    // 创建时间更新定时器 (每秒更新一次)
    const esp_timer_create_args_t time_timer_args = {
        .callback = &time_timer_callback,
        .name = "time_timer"
    };
    ret = esp_timer_create(&time_timer_args, &s_time_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create time timer: %s", esp_err_to_name(ret));
    }
    
    // 显示启动画面
    gui_show_boot_screen();
    gui_flush();
    
    // 启动定时器
    if (s_splash_timer) {
        esp_timer_start_once(s_splash_timer, 10000000); // 10秒
    }
    if (s_time_timer) {
        esp_timer_start_periodic(s_time_timer, 1000000); // 每秒更新
    }
    
    ESP_LOGI(TAG, "UI initialized successfully");
    return ESP_OK;
}

// 兼容性函数（旧代码使用）
void ui_update_wifi_status(bool connected) { ui_set_wifi_status(connected); }
void ui_update_telegram_status(bool connected) { ui_set_telegram_status(connected); }
void ui_update_battery_level(uint8_t percentage) { ui_set_battery_level(percentage); }
void ui_update_time(const char *time_str) { ui_set_time(time_str); }