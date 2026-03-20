/**
 * @file ui_main.c
 * @brief UI 主控制器 - 页面导航和显示管理
 */

#include "ui_main.h"
#include "simple_gui.h"
#include "display_manager.h"
#include "../peripherals/boot_button.h"
#include "../peripherals/battery_adc.h"
#include "../wifi/wifi_manager.h"
#include "../llm/llm_proxy.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

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
static int16_t message_y_pos = 40;
static ui_page_t s_current_page = PAGE_BOOT;
static int s_nav_index = 0;
static bool s_nav_initialized = false;
static bool s_splash_done = false;  // 启动画面是否结束

// 启动页面定时器
static esp_timer_handle_t s_splash_timer = NULL;

// 时间和电池更新任务
static TaskHandle_t s_status_task = NULL;

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
            // Home页面 - 时间、日期、问候语、AI状态
            gui_draw_hline(12, SCREEN_WIDTH - 12, 45, COLOR_DARK_GRAY);
            {
                // 获取日期信息
                char date_str[32] = "";
                char greeting[32] = "";
                
                time_t now = time(NULL);
                if (now > 1700000000) {
                    struct tm tm_info;
                    localtime_r(&now, &tm_info);
                    
                    // 格式化日期
                    const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
                    snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d %s", 
                             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
                             weekdays[tm_info.tm_wday]);
                    
                    // 根据时间段生成问候语
                    int hour = tm_info.tm_hour;
                    if (hour >= 5 && hour < 12) {
                        strcpy(greeting, "Good Morning!");
                    } else if (hour >= 12 && hour < 18) {
                        strcpy(greeting, "Good Afternoon!");
                    } else if (hour >= 18 && hour < 22) {
                        strcpy(greeting, "Good Evening!");
                    } else {
                        strcpy(greeting, "Good Night!");
                    }
                } else {
                    strcpy(date_str, "----/--/-- ---");
                    strcpy(greeting, "Hello!");
                }
                
                uint8_t bat_pct = battery_adc_is_ready() ? battery_get_percent() : 0;
                gui_show_home_page(current_time, date_str, greeting, false, bat_pct);
            }
            break;
            
        case PAGE_SYSTEM_MSG:
            // 系统信息页面
            gui_draw_hline(12, SCREEN_WIDTH - 12, 45, COLOR_DARK_GRAY);
            {
                // 获取系统信息
                const char *wifi_ip = wifi_manager_get_ip();
                const char *llm_provider = llm_get_provider_name();
                uint32_t free_heap = esp_get_free_heap_size();
                uint32_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
                uint8_t bat_pct = battery_adc_is_ready() ? battery_get_percent() : 0;
                uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000000);
                
                gui_show_system_info(wifi_ip, llm_provider, free_heap, free_psram, bat_pct, uptime);
            }
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

/**
 * @brief 更新状态栏
 * 
 * 注意：此函数会调用 gui_flush() 刷新屏幕
 * - gui_show_status_bar() 只绘制状态栏区域（顶部 35 像素）
 * - gui_flush() 会传输整个帧缓冲到屏幕（257KB）
 * 
 * 性能考虑：每秒调用一次可能影响性能，后续可优化为只刷新变化区域
 */
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
/**
 * @brief 启动画面定时器回调
 * 
 * 设计说明：
 *   - 此函数在 ESP Timer 任务上下文中执行
 *   - ESP Timer 任务栈空间有限（默认 4KB），不适合执行 SPI 操作
 *   - 因此只设置标志 s_splash_done，实际页面切换在 status_update_task 中执行
 * 
 * 为什么不在定时器中直接切换页面？
 *   1. SPI 操作需要较大栈空间
 *   2. gui_flush() 需要传输 257KB 数据
 *   3. 定时器上下文可能因栈不足导致崩溃或功能异常
 */
static void splash_timer_callback(void *arg)
{
    (void)arg;
    // 10秒后设置标志，让 status_update_task 执行页面切换
    // 不在定时器上下文中直接执行 SPI 操作，避免栈溢出
    if (s_current_page == PAGE_BOOT) {
        ESP_LOGI(TAG, "Splash timeout - switch to Status page");
        s_splash_done = true;
    }
}

// ============================================================================
// 时间和电池更新任务 (替代定时器，避免栈溢出)
// ============================================================================

/**
 * @brief 状态更新任务
 * 
 * 此任务在 FreeRTOS 任务上下文中运行，栈空间充足（4096 字节），
 * 可以安全执行 SPI 操作。
 * 
 * 主要功能：
 *   1. 检测启动画面超时标志，执行页面切换
 *   2. 更新系统时间显示
 *   3. 定期更新电池电量
 *   4. 更新状态栏
 * 
 * 设计说明：
 *   - 页面切换需要调用 ui_show_page() -> gui_flush() -> SPI 操作
 *   - 这些操作需要足够的栈空间，不适合在定时器回调中执行
 *   - 因此使用 "定时器设置标志 + 任务检测执行" 的模式
 */
static void status_update_task(void *arg)
{
    (void)arg;
    int battery_counter = 0;
    bool splash_switched = false;  // 确保只切换一次
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // 每秒检查一次
        
        // 检查是否需要切换启动页面（在任务上下文中执行，有足够栈空间）
        if (s_splash_done && !splash_switched && s_current_page == PAGE_BOOT) {
            splash_switched = true;
            s_nav_initialized = true;
            s_current_page = PAGE_STATUS_BAR;
            s_nav_index = 0;
            ui_show_page(PAGE_STATUS_BAR);
            
            // 显示启动期间缓存的消息
            for (int i = 0; i < s_pending_count; i++) {
                gui_add_log("SYS", s_pending_msgs[i]);
            }
            s_pending_count = 0;
            continue;  // 跳过本次循环，下一秒再更新状态栏
        }
        
        // 获取系统时间
        time_t now = time(NULL);
        if (now > 1700000000) {  // 时间已同步
            struct tm tm_info;
            localtime_r(&now, &tm_info);
            snprintf(current_time, sizeof(current_time), "%02d:%02d", tm_info.tm_hour, tm_info.tm_min);
        } else {
            strncpy(current_time, "--:--", sizeof(current_time) - 1);
        }
        
        // 每10秒更新电池电量
        battery_counter++;
        if (battery_counter >= 10) {
            battery_counter = 0;
            if (battery_adc_is_ready()) {
                int pct = battery_get_percent();
                battery_level = (uint8_t)pct;  // 静默更新，不输出日志
            }
        }
        
        // 更新状态栏
        if (s_splash_done && s_current_page != PAGE_BOOT) {
            update_status_bar();
        }
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
    if (s_status_task) {
        vTaskDelete(s_status_task);
        s_status_task = NULL;
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
    
    // 创建时间和电池更新任务 (替代定时器，避免栈溢出)
    xTaskCreate(status_update_task, "status_upd", 4096, NULL, 5, &s_status_task);
    if (!s_status_task) {
        ESP_LOGE(TAG, "Failed to create status update task");
    }
    
    // 显示启动画面
    gui_show_boot_screen();
    gui_flush();
    
    // 启动启动画面定时器
    if (s_splash_timer) {
        esp_timer_start_once(s_splash_timer, 10000000); // 10秒
    }
    
    ESP_LOGI(TAG, "UI initialized successfully");
    return ESP_OK;
}

// 兼容性函数（旧代码使用）
void ui_update_wifi_status(bool connected) { ui_set_wifi_status(connected); }
void ui_update_telegram_status(bool connected) { ui_set_telegram_status(connected); }
void ui_update_battery_level(uint8_t percentage) { ui_set_battery_level(percentage); }
void ui_update_time(const char *time_str) { ui_set_time(time_str); }