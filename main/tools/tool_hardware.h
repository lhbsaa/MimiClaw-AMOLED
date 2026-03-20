/**
 * @file tool_hardware.h
 * @brief Hardware control tools for LLM function calling
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize hardware tools (LED, temperature sensor)
 */
esp_err_t tool_hardware_init(void);

/**
 * @brief Control onboard LED
 * Input JSON: {"state": "on"|"off"|"toggle"}
 * Output: LED status message
 */
esp_err_t tool_led_control_execute(const char *input_json, char *output, size_t output_size);

/**
 * @brief Get battery status
 * Input JSON: {} (no parameters)
 * Output: "Battery: XXXXmV (XX%), charging|discharging"
 */
esp_err_t tool_battery_status_execute(const char *input_json, char *output, size_t output_size);

/**
 * @brief Read ESP32-S3 internal chip temperature
 * Input JSON: {} (no parameters)
 * Output: "Chip temperature: XX.X°C"
 */
esp_err_t tool_chip_temperature_execute(const char *input_json, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif
