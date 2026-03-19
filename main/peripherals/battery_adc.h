#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize battery ADC reader (GPIO4, voltage divider)
 * @return ESP_OK on success
 */
esp_err_t battery_adc_init(void);

/**
 * @brief Read battery voltage in millivolts
 * @return Battery voltage (mV), or 0 if not initialized
 */
uint32_t battery_get_voltage_mv(void);

/**
 * @brief Get battery percentage (0-100)
 *        Uses a simple linear mapping from voltage range
 *        3300mV = 0%, 4200mV = 100%
 * @return Battery percentage, clamped to 0-100
 */
int battery_get_percent(void);

/**
 * @brief Check if USB power is likely connected
 *        Heuristic: voltage > 4250mV suggests charging
 */
bool battery_is_charging(void);

/**
 * @brief Check if battery ADC is initialized
 */
bool battery_adc_is_ready(void);

#ifdef __cplusplus
}
#endif
