#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"

#include "mimi_config.h"
#include "bus/message_bus.h"
#include "wifi/wifi_manager.h"
#include "channels/telegram/telegram_bot.h"
#include "channels/feishu/feishu_bot.h"
#include "llm/llm_proxy.h"
#include "agent/agent_loop.h"
#include "memory/memory_store.h"
#include "memory/session_mgr.h"
#include "gateway/ws_server.h"
#include "cli/serial_cli.h"
#include "proxy/http_proxy.h"
#include "tools/tool_registry.h"
#include "cron/cron_service.h"
#include "heartbeat/heartbeat.h"
#include "skills/skill_loader.h"
#include "onboard/wifi_onboard.h"

// AMOLED Display includes
#include "display/display_manager.h"
#include "display/ui_main.h"
#include "peripherals/battery_adc.h"

static const char *TAG = "mimi";

// Display status flags
static volatile bool display_initialized = false;

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = MIMI_SPIFFS_BASE,
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: total=%d, used=%d", (int)total, (int)used);

    return ESP_OK;
}

/* Outbound dispatch task: reads from outbound queue and routes to channels */
static void outbound_dispatch_task(void *arg)
{
    ESP_LOGI(TAG, "Outbound dispatch started");

    while (1) {
        mimi_msg_t msg;
        if (message_bus_pop_outbound(&msg, UINT32_MAX) != ESP_OK) continue;

        // 减少ESP_LOG频率，仅在关键事件时输出
        ESP_LOGD(TAG, "Processing outbound message to %s:%s", msg.channel, msg.chat_id);

        if (strcmp(msg.channel, MIMI_CHAN_TELEGRAM) == 0) {
            esp_err_t send_err = telegram_send_message(msg.chat_id, msg.content);
            if (send_err != ESP_OK) {
                ESP_LOGE(TAG, "Telegram send failed for %s: %s", msg.chat_id, esp_err_to_name(send_err));
            } else {
                ESP_LOGI(TAG, "Telegram send success for %s (%d bytes)", msg.chat_id, (int)strlen(msg.content));
                // Update UI with sent message
                if (display_initialized) {
                    ui_add_message("Mimi", msg.content, false);
                }
            }
        } else if (strcmp(msg.channel, MIMI_CHAN_FEISHU) == 0) {
            esp_err_t send_err = feishu_send_message(msg.chat_id, msg.content);
            if (send_err != ESP_OK) {
                ESP_LOGE(TAG, "Feishu send failed for %s: %s", msg.chat_id, esp_err_to_name(send_err));
            } else {
                ESP_LOGI(TAG, "Feishu send success for %s (%d bytes)", msg.chat_id, (int)strlen(msg.content));
            }
        } else if (strcmp(msg.channel, MIMI_CHAN_WEBSOCKET) == 0) {
            esp_err_t ws_err = ws_server_send(msg.chat_id, msg.content);
            if (ws_err != ESP_OK) {
                ESP_LOGW(TAG, "WS send failed for %s: %s", msg.chat_id, esp_err_to_name(ws_err));
            }
        } else if (strcmp(msg.channel, MIMI_CHAN_SYSTEM) == 0) {
            ESP_LOGI(TAG, "System message [%s]: %.128s", msg.chat_id, msg.content);
            // Update UI with system message
            if (display_initialized) {
                ui_add_system_message(msg.content);
            }
        } else if (strcmp(msg.channel, MIMI_CHAN_CLI) == 0) {
            // CLI channel - show full response for debugging
            ESP_LOGI(TAG, "CLI response: %s", msg.content);
            // Also update UI with brief summary
            if (display_initialized) {
                ui_add_message("Mimi", msg.content, false);
            }
        } else {
            ESP_LOGW(TAG, "Unknown channel: %s", msg.channel);
        }

        free(msg.content);
    }
}

// WiFi status callback for UI
static void wifi_status_callback(bool connected)
{
    if (display_initialized) {
        ui_update_wifi_status(connected);
    }
    display_manager_show_wifi_status(connected);
}

// Telegram message callback for UI
// static void telegram_message_callback(const char *chat_id, const char *message)
// {
//     (void)chat_id;
//     if (display_initialized && message) {
//         ui_add_message("User", message, true);
//     }
// }

// Telegram status callback for UI
// static void telegram_status_callback(bool connected)
// {
//     (void)connected;
//     if (display_initialized) {
//         ui_update_telegram_status(connected);
//     }
//     display_manager_show_telegram_status(connected);
// }

void app_main(void)
{
    /* Silence noisy components */
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  MimiClaw-AMOLED - ESP32-S3 AI Agent");
    ESP_LOGI(TAG, "  With T-Display-S3 AMOLED Display");
    ESP_LOGI(TAG, "========================================");

    /* Print memory info */
    ESP_LOGI(TAG, "Internal free: %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "PSRAM free:    %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* Phase 1: Core infrastructure */
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(init_spiffs());

    /* Phase 2: Initialize subsystems (before display and CLI) */
    ESP_ERROR_CHECK(message_bus_init());
    ESP_ERROR_CHECK(memory_store_init());
    ESP_ERROR_CHECK(skill_loader_init());
    ESP_ERROR_CHECK(session_mgr_init());
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(http_proxy_init());
    ESP_ERROR_CHECK(telegram_bot_init());
    ESP_ERROR_CHECK(feishu_bot_init());
    ESP_ERROR_CHECK(llm_proxy_init());
    ESP_ERROR_CHECK(tool_registry_init());
    ESP_ERROR_CHECK(cron_service_init());
    ESP_ERROR_CHECK(heartbeat_init());
    ESP_ERROR_CHECK(agent_loop_init());

    /* Phase 3: Initialize AMOLED Display (after subsystems) */
    ESP_LOGI(TAG, "Initializing AMOLED Display...");
    esp_err_t display_err = ui_main_init();
    if (display_err == ESP_OK) {
        display_initialized = true;
        ESP_LOGI(TAG, "AMOLED Display initialized successfully");
    } else {
        ESP_LOGW(TAG, "Display initialization failed: %s", esp_err_to_name(display_err));
    }

    /* Initialize Battery ADC */
    ESP_LOGI(TAG, "Initializing Battery ADC...");
    esp_err_t bat_err = battery_adc_init();
    if (bat_err == ESP_OK) {
        ESP_LOGI(TAG, "Battery ADC initialized successfully");
    } else {
        ESP_LOGW(TAG, "Battery ADC init failed: %s", esp_err_to_name(bat_err));
    }

    /* Start Serial CLI after Display initialization */
    ESP_ERROR_CHECK(serial_cli_init());

    /* Phase 4: Start WiFi */
    esp_err_t wifi_err = wifi_manager_start();
    bool wifi_ok = false;
    if (wifi_err == ESP_OK) {
        ESP_LOGI(TAG, "Scanning nearby APs on boot...");
        wifi_manager_scan_and_print();
        ESP_LOGI(TAG, "Waiting for WiFi connection...");
        if (wifi_manager_wait_connected(30000) == ESP_OK) {
            wifi_ok = true;
            ESP_LOGI(TAG, "WiFi connected: %s", wifi_manager_get_ip());
            wifi_status_callback(true);
        } else {
            ESP_LOGW(TAG, "WiFi connection timeout");
            wifi_status_callback(false);
        }
    } else {
        ESP_LOGW(TAG, "No WiFi credentials configured");
        wifi_status_callback(false);
    }

    if (!wifi_ok) {
        ESP_LOGW(TAG, "Entering WiFi onboarding mode...");
        if (display_initialized) {
            ui_add_system_message("WiFi not configured.");
            ui_add_system_message("Use serial CLI to configure WiFi.");
        }
        wifi_onboard_start(WIFI_ONBOARD_MODE_CAPTIVE);  /* blocks, restarts on success */
        return;  /* unreachable */
    }

    if (wifi_onboard_start(WIFI_ONBOARD_MODE_ADMIN) != ESP_OK) {
        ESP_LOGW(TAG, "Local admin portal unavailable; continuing without config hotspot");
    }

    {
        /* Outbound dispatch task should start first to avoid dropping early replies. */
        ESP_ERROR_CHECK((xTaskCreatePinnedToCore(
            outbound_dispatch_task, "outbound",
            MIMI_OUTBOUND_STACK, NULL,
            MIMI_OUTBOUND_PRIO, NULL, MIMI_OUTBOUND_CORE) == pdPASS)
            ? ESP_OK : ESP_FAIL);

        /* Start network-dependent services */
        ESP_ERROR_CHECK(agent_loop_start());
        ESP_ERROR_CHECK(telegram_bot_start());
        ESP_ERROR_CHECK(feishu_bot_start());
        cron_service_start();
        heartbeat_start();
        ESP_ERROR_CHECK(ws_server_start());

        ESP_LOGI(TAG, "All services started!");
    }

    // Update UI with ready status
    if (display_initialized) {
        ui_add_system_message("All services started!");
        ui_update_wifi_status(true);
        ui_update_telegram_status(true);
    }

    ESP_LOGI(TAG, "MimiClaw-AMOLED services started successfully!");
}
