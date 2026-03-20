/**
 * @file time_sync.h
 * @brief Time synchronization via SNTP
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start SNTP service
 * 
 * Call this after WiFi is connected. SNTP will automatically
 * synchronize the system clock with NTP servers.
 */
esp_err_t time_sync_init(void);

/**
 * @brief Check if time has been synchronized
 * @return true if system clock is valid (after year 2024)
 */
bool time_sync_is_synchronized(void);

/**
 * @brief Wait for time synchronization with timeout
 * @param timeout_ms Maximum time to wait (0 = no wait, UINT32_MAX = forever)
 * @return ESP_OK if synchronized, ESP_ERR_TIMEOUT if timeout
 */
esp_err_t time_sync_wait(uint32_t timeout_ms);

/**
 * @brief Force immediate time sync (non-blocking)
 */
void time_sync_request_now(void);

#ifdef __cplusplus
}
#endif
