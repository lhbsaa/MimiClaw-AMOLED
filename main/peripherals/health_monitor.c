/**
 * @file health_monitor.c
 * @brief 系统健康监控实现
 */

#include "health_monitor.h"
#include "mimi_config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/temperature_sensor.h"
#include "esp_task_wdt.h"

static const char *TAG = "health";

// 内存阈值配置
#define MIN_PSRAM_FREE           (2 * 1024 * 1024)   // 最少保留 2MB PSRAM
#define MIN_INTERNAL_FREE        (8 * 1024)           // 最少保留 8KB 内部 RAM
#define WARN_PSRAM_FRAGMENT_SIZE (512 * 1024)         // PSRAM 碎片警告阈值
#define INTERNAL_LOW_RESTART_COUNT  3                 // 连续 N 次低于阈值才重启

// 温度阈值
#define CRITICAL_TEMP_C           75  // 超过 75 度重启
#define WARNING_TEMP_C            65  // 超过 65 度告警

// 温度错误哨兵值（低于绝对零度，明确表示读取失败）
#define TEMP_READ_ERROR  (-274.0f)

// 健康统计
static uint32_t s_uptime_seconds = 0;
static float s_last_temp_c = TEMP_READ_ERROR;
static uint32_t s_last_free_psram = 0;
static uint32_t s_last_free_internal = 0;

// 温度传感器句柄
static temperature_sensor_handle_t s_temp_sensor = NULL;
static bool s_temp_sensor_inited = false;

/**
 * @brief 初始化温度传感器
 */
static esp_err_t init_temp_sensor(void)
{
    if (s_temp_sensor_inited) {
        return ESP_OK;
    }

    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
    esp_err_t ret = temperature_sensor_install(&temp_sensor_config, &s_temp_sensor);
    if (ret == ESP_OK) {
        s_temp_sensor_inited = true;
        ESP_LOGI(TAG, "Temperature sensor initialized");
    } else {
        ESP_LOGW(TAG, "Temperature sensor init failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief 读取芯片温度
 */
static float read_temperature(void)
{
    if (!s_temp_sensor_inited) {
        return TEMP_READ_ERROR;
    }

    float temp_c = TEMP_READ_ERROR;
    esp_err_t ret = temperature_sensor_enable(s_temp_sensor);
    if (ret == ESP_OK) {
        ret = temperature_sensor_get_celsius(s_temp_sensor, &temp_c);
        temperature_sensor_disable(s_temp_sensor);
        if (ret != ESP_OK) {
            temp_c = TEMP_READ_ERROR;
        }
    }
    return temp_c;
}

/**
 * @brief 健康监控主任务
 */
static void health_monitor_task(void *arg)
{
    ESP_LOGI(TAG, "Health monitor task started");

    // 初始化温度传感器
    init_temp_sensor();

    uint32_t check_count = 0;
    uint32_t low_internal_count = 0;  // 连续低内存计数

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));  // 每分钟检查一次
        check_count++;
        s_uptime_seconds += 60;

        // ========== 1. 内存检查 ==========
        uint32_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        uint32_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        uint32_t largest_psram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        uint32_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

        s_last_free_psram = free_psram;
        s_last_free_internal = free_internal;

        ESP_LOGD(TAG, "Health check #%lu: PSRAM=%luKB (%luKB largest), Internal=%luKB (%luKB largest)",
                 (unsigned long)check_count,
                 (unsigned long)(free_psram / 1024), (unsigned long)(largest_psram / 1024),
                 (unsigned long)(free_internal / 1024), (unsigned long)(largest_internal / 1024));

        // PSRAM 严重不足 - 告警
        if (free_psram < MIN_PSRAM_FREE) {
            ESP_LOGW(TAG, "WARNING: Low PSRAM! Free=%luKB (threshold=%luKB)",
                     (unsigned long)(free_psram / 1024), (unsigned long)(MIN_PSRAM_FREE / 1024));
        }

        // 内部 RAM 严重不足 - 连续多次低于阈值才重启
        if (free_internal < MIN_INTERNAL_FREE) {
            low_internal_count++;
            ESP_LOGW(TAG, "WARNING: Low internal RAM! Free=%luKB (threshold=%luKB) [%lu/%d]",
                     (unsigned long)(free_internal / 1024), (unsigned long)(MIN_INTERNAL_FREE / 1024),
                     (unsigned long)low_internal_count, INTERNAL_LOW_RESTART_COUNT);
            if (low_internal_count >= INTERNAL_LOW_RESTART_COUNT) {
                ESP_LOGE(TAG, "CRITICAL: Internal RAM persistently low for %d checks. Restarting...",
                         INTERNAL_LOW_RESTART_COUNT);
                esp_restart();
            }
        } else {
            if (low_internal_count > 0) {
                ESP_LOGI(TAG, "Internal RAM recovered, reset low count");
            }
            low_internal_count = 0;
        }

        // PSRAM 碎片检查
        if (largest_psram < WARN_PSRAM_FRAGMENT_SIZE) {
            ESP_LOGW(TAG, "WARNING: PSRAM fragmentation! Largest block=%luKB (threshold=%luKB)",
                     (unsigned long)(largest_psram / 1024), (unsigned long)(WARN_PSRAM_FRAGMENT_SIZE / 1024));
        }

        // ========== 2. 温度检查 ==========
        if (s_temp_sensor_inited) {
            float temp_c = read_temperature();
            s_last_temp_c = temp_c;

            if (temp_c > TEMP_READ_ERROR) {
                if (temp_c > CRITICAL_TEMP_C) {
                    ESP_LOGE(TAG, "CRITICAL: Overtemperature! %.1f°C > %d°C. Restarting...",
                             temp_c, CRITICAL_TEMP_C);
                    esp_restart();
                } else if (temp_c > WARNING_TEMP_C) {
                    ESP_LOGW(TAG, "WARNING: High temperature! %.1f°C > %d°C",
                             temp_c, WARNING_TEMP_C);
                }
            }
        }
    }
}

/**
 * @brief 获取温度传感器句柄（供其他模块复用）
 */
temperature_sensor_handle_t health_monitor_get_temp_sensor(void)
{
    return s_temp_sensor;
}

/**
 * @brief 获取系统健康状态摘要
 */
void health_monitor_get_summary(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return;

    int hours = s_uptime_seconds / 3600;
    int mins = (s_uptime_seconds % 3600) / 60;

    snprintf(buf, buf_size,
             "Uptime: %dh %dm | Temp: %.1fC | PSRAM: %luKB | IRAM: %luKB",
             hours, mins,
             s_last_temp_c,
             (unsigned long)(s_last_free_psram / 1024),
             (unsigned long)(s_last_free_internal / 1024));
}

/**
 * @brief 初始化健康监控
 */
esp_err_t health_monitor_init(void)
{
    ESP_LOGI(TAG, "Initializing health monitor...");

    BaseType_t ret = xTaskCreate(health_monitor_task, "health",
                                  4096, NULL, 3, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create health monitor task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Health monitor initialized successfully");
    return ESP_OK;
}
