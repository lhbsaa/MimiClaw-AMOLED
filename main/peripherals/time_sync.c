/**
 * @file time_sync.c
 * @brief Time synchronization via SNTP
 * 
 * Uses ESP-IDF SNTP component to synchronize system clock
 * with NTP servers after WiFi connection.
 */

#include "time_sync.h"
#include "mimi_config.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_event.h"

static const char *TAG = "time_sync";

static bool s_sntp_inited = false;
static bool s_time_synced = false;

static void time_sync_notification_cb(struct timeval *tv)
{
    s_time_synced = true;
    
    time_t now = tv->tv_sec;
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &tm_info);
    ESP_LOGI(TAG, "Time synchronized: %s", buf);
}

esp_err_t time_sync_init(void)
{
    if (s_sntp_inited) {
        ESP_LOGW(TAG, "SNTP already initialized");
        return ESP_OK;
    }

    /* Set timezone */
    setenv("TZ", MIMI_TIMEZONE, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to: %s", MIMI_TIMEZONE);

    /* Check current system time before sync */
    time_t now_before = time(NULL);
    ESP_LOGI(TAG, "System time before sync: %ld", (long)now_before);

    /* Configure SNTP operating mode */
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    /* Use multiple NTP servers for reliability */
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.cloudflare.com");
    esp_sntp_setservername(2, "time.google.com");
    
    /* Set sync mode to immediate update */
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    
    /* Register notification callback */
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    /* Start SNTP - returns void in ESP-IDF v5.3+ */
    esp_sntp_init();
    s_sntp_inited = true;
    ESP_LOGI(TAG, "SNTP service started, waiting for sync...");

    return ESP_OK;
}

bool time_sync_is_synchronized(void)
{
    if (s_time_synced) {
        return true;
    }
    
    /* Check if system time is valid (after year 2024) */
    time_t now = time(NULL);
    return (now > 1700000000);  /* 2023-11-14 */
}

esp_err_t time_sync_wait(uint32_t timeout_ms)
{
    if (time_sync_is_synchronized()) {
        return ESP_OK;
    }

    if (!s_sntp_inited) {
        ESP_LOGW(TAG, "SNTP not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Wait for sync notification */
    int retry = 0;
    int max_retries = timeout_ms / 1000;
    
    while (!s_time_synced && retry < max_retries) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
        ESP_LOGD(TAG, "Waiting for time sync... (%d/%d)", retry, max_retries);
    }

    if (s_time_synced) {
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

void time_sync_request_now(void)
{
    if (s_sntp_inited) {
        ESP_LOGI(TAG, "Requesting immediate time sync");
        /* SNTP will automatically retry - just wait */
    }
}
