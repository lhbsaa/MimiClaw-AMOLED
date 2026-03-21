/**
 * @file boot_button.c
 * @brief Boot 按钮驱动 - 支持单击、双击、长按检测
 *
 * GPIO 0 连接到 Boot 按钮，低电平有效（按下为低）
 */

#include "boot_button.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "boot_btn";

/* 按键引脚 */
#define BUTTON_BOOT_PIN  0

/* 按键消抖和时序参数 */
#define DEBOUNCE_MS         50      /* 按下消抖时间 */
#define DEBOUNCE_RELEASE_MS 80      /* 释放消抖时间 */
#define CLICK_MAX_MS        500     /* 单击最大持续时间 */
#define MULTI_GAP_MS        400     /* 多击间隔最大时间 */
#define LONG_PRESS_MS       700     /* 长按触发时间 */
#define VERY_LONG_PRESS_MS  3000    /* 超长按触发时间 */
#define HOLDING_INTERVAL_MS 200     /* 持续按住回调间隔 */
#define POLL_INTERVAL_MS    20      /* 轮询间隔 */

/* 按键状态 */
typedef enum {
    BTN_STATE_IDLE,
    BTN_STATE_DEBOUNCE_PRESS,
    BTN_STATE_PRESSED,
    BTN_STATE_DEBOUNCE_RELEASE,
    BTN_STATE_WAIT_MULTI,       /* 等待多击（双击/三击） */
} btn_state_t;

static btn_state_t s_state = BTN_STATE_IDLE;
static uint64_t s_press_time = 0;
static uint64_t s_release_time = 0;
static int s_click_count = 0;
static int s_last_stable_level = 1;
static int s_debounce_count = 0;

/* 回调函数 */
static boot_button_cb_t s_single_cb = NULL;
static boot_button_cb_t s_double_cb = NULL;
static boot_button_cb_t s_triple_cb = NULL;
static boot_button_cb_t s_long_cb = NULL;
static boot_button_cb_t s_very_long_cb = NULL;
static boot_button_cb_t s_holding_cb = NULL;

/* 定时器句柄 */
static esp_timer_handle_t s_timer = NULL;

/* 前向声明 */
static void timer_callback(void *arg);
static void process_button_event(void);

/* GPIO ISR 处理函数 */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    esp_timer_stop(s_timer);
    esp_timer_start_once(s_timer, DEBOUNCE_MS * 1000);
}

esp_err_t boot_button_init(void)
{
    esp_log_level_set("gpio", ESP_LOG_WARN);

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_BOOT_PIN),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_BOOT_PIN, gpio_isr_handler, NULL));

    const esp_timer_create_args_t timer_args = {
        .callback = &timer_callback,
        .name = "boot_btn_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_timer));

    esp_log_level_set(TAG, ESP_LOG_WARN);

    ESP_LOGI(TAG, "Boot button initialized on GPIO %d", BUTTON_BOOT_PIN);
    return ESP_OK;
}

void boot_button_register_callback(boot_button_event_t event, boot_button_cb_t cb)
{
    switch (event) {
        case BOOT_BTN_SINGLE_CLICK:    s_single_cb = cb; break;
        case BOOT_BTN_DOUBLE_CLICK:    s_double_cb = cb; break;
        case BOOT_BTN_TRIPLE_CLICK:    s_triple_cb = cb; break;
        case BOOT_BTN_LONG_PRESS:      s_long_cb = cb; break;
        case BOOT_BTN_VERY_LONG_PRESS: s_very_long_cb = cb; break;
        case BOOT_BTN_HOLDING:         s_holding_cb = cb; break;
    }
}

static void timer_callback(void *arg)
{
    process_button_event();
}

/* 读取稳定电平 */
static int read_stable_level(void)
{
    int level = gpio_get_level(BUTTON_BOOT_PIN);
    if (level == s_last_stable_level) {
        s_debounce_count = 0;
        return level;
    }

    s_debounce_count++;
    if (s_debounce_count >= 3) {
        s_last_stable_level = level;
        s_debounce_count = 0;
        return level;
    }

    esp_timer_start_once(s_timer, POLL_INTERVAL_MS * 1000);
    return -1;
}

static void process_button_event(void)
{
    int level = read_stable_level();
    if (level < 0) return;

    uint64_t now = esp_timer_get_time() / 1000;

    switch (s_state) {
        case BTN_STATE_IDLE:
            if (level == 0) {
                s_press_time = now;
                s_state = BTN_STATE_DEBOUNCE_PRESS;
                esp_timer_start_once(s_timer, DEBOUNCE_MS * 1000);
            }
            break;

        case BTN_STATE_DEBOUNCE_PRESS:
            if (level == 0) {
                s_press_time = now;
                s_state = BTN_STATE_PRESSED;
                esp_timer_start_once(s_timer, LONG_PRESS_MS * 1000);
            } else {
                s_state = BTN_STATE_IDLE;
            }
            break;

        case BTN_STATE_PRESSED:
            if (level == 1) {
                /* 按钮释放 */
                s_state = BTN_STATE_DEBOUNCE_RELEASE;
                esp_timer_start_once(s_timer, DEBOUNCE_RELEASE_MS * 1000);
            } else {
                /* 按钮仍被按住 */
                uint32_t press_duration = now - s_press_time;
                if (press_duration >= VERY_LONG_PRESS_MS) {
                    if (s_very_long_cb) s_very_long_cb();
                    s_state = BTN_STATE_IDLE;
                    s_click_count = 0;
                } else if (press_duration >= LONG_PRESS_MS) {
                    if (s_long_cb) s_long_cb();
                    /* 继续检测超长按 */
                    esp_timer_start_once(s_timer, (VERY_LONG_PRESS_MS - LONG_PRESS_MS) * 1000);
                } else {
                    /* 持续按住回调（用于亮度调节等） */
                    if (press_duration >= LONG_PRESS_MS - 100) {
                        if (s_holding_cb) s_holding_cb();
                        esp_timer_start_once(s_timer, HOLDING_INTERVAL_MS * 1000);
                    } else {
                        esp_timer_start_once(s_timer, POLL_INTERVAL_MS * 1000);
                    }
                }
            }
            break;

        case BTN_STATE_DEBOUNCE_RELEASE:
            if (level == 1) {
                uint32_t press_duration = now - s_press_time;

                if (press_duration >= LONG_PRESS_MS) {
                    /* 长按后释放，不计数 */
                    s_state = BTN_STATE_IDLE;
                    s_click_count = 0;
                } else {
                    /* 短按释放，开始等待多击 */
                    s_release_time = now;
                    s_click_count++;
                    s_state = BTN_STATE_WAIT_MULTI;
                    esp_timer_start_once(s_timer, MULTI_GAP_MS * 1000);
                }
            } else {
                /* 仍在按下，回到 PRESSED 状态 */
                s_state = BTN_STATE_PRESSED;
                esp_timer_start_once(s_timer, POLL_INTERVAL_MS * 1000);
            }
            break;

        case BTN_STATE_WAIT_MULTI:
            if (level == 0) {
                /* 在等待期间又按下了 */
                s_press_time = now;
                s_state = BTN_STATE_DEBOUNCE_PRESS;
                esp_timer_start_once(s_timer, DEBOUNCE_MS * 1000);
            } else {
                /* 等待超时，根据点击次数触发回调 */
                if (now - s_release_time >= MULTI_GAP_MS) {
                    switch (s_click_count) {
                        case 1:
                            if (s_single_cb) s_single_cb();
                            break;
                        case 2:
                            if (s_double_cb) s_double_cb();
                            break;
                        case 3:
                        default:
                            if (s_triple_cb) s_triple_cb();
                            break;
                    }
                    s_click_count = 0;
                    s_state = BTN_STATE_IDLE;
                } else {
                    esp_timer_start_once(s_timer, POLL_INTERVAL_MS * 1000);
                }
            }
            break;

        default:
            s_state = BTN_STATE_IDLE;
            s_click_count = 0;
            break;
    }
}
