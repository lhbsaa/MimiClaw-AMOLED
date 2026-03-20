/**
 * @file tool_hardware.c
 * @brief Hardware control tools for MimiClaw-AMOLED
 * 
 * Provides LLM-callable tools for:
 * - battery_status: Read battery voltage, percentage, charging state
 * - chip_temperature: Read ESP32-S3 internal temperature
 * - led_control: Control onboard LED (GPIO38)
 */

#include "tools/tool_hardware.h"
#include "mimi_config.h"
#include "peripherals/battery_adc.h"

#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "esp_log.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "tool_hw";

static temperature_sensor_handle_t s_temp_sensor = NULL;
static bool s_temp_sensor_inited = false;

/* LED pin configuration */
#define LED_PIN         38
#define LED_ACTIVE_LOW  1   /* LED is active low on this board */

/*============================================================================
 * LED Control
 *============================================================================*/

static esp_err_t led_init(void)
{
    static bool inited = false;
    if (inited) return ESP_OK;
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret == ESP_OK) {
        inited = true;
        ESP_LOGI(TAG, "LED initialized on GPIO%d", LED_PIN);
    }
    return ret;
}

esp_err_t tool_led_control_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *state_obj = cJSON_GetObjectItem(root, "state");
    if (!cJSON_IsString(state_obj)) {
        snprintf(output, output_size, "Error: 'state' required ('on', 'off', or 'toggle')");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const char *state_str = state_obj->valuestring;
    
    esp_err_t ret = led_init();
    if (ret != ESP_OK) {
        snprintf(output, output_size, "Error: LED init failed");
        cJSON_Delete(root);
        return ret;
    }

    int current_level = gpio_get_level(LED_PIN);
    int new_level;
    
    if (strcmp(state_str, "on") == 0) {
        new_level = LED_ACTIVE_LOW ? 0 : 1;
    } else if (strcmp(state_str, "off") == 0) {
        new_level = LED_ACTIVE_LOW ? 1 : 0;
    } else if (strcmp(state_str, "toggle") == 0) {
        new_level = (current_level == 0) ? 1 : 0;
    } else {
        snprintf(output, output_size, "Error: invalid state '%s' (use 'on', 'off', or 'toggle')", state_str);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    gpio_set_level(LED_PIN, new_level);
    
    const char *display_state = (new_level == (LED_ACTIVE_LOW ? 0 : 1)) ? "ON" : "OFF";
    snprintf(output, output_size, "LED is now %s (GPIO%d)", display_state, LED_PIN);
    ESP_LOGI(TAG, "LED -> %s", display_state);

    cJSON_Delete(root);
    return ESP_OK;
}

/*============================================================================
 * Battery Status
 *============================================================================*/

esp_err_t tool_battery_status_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    if (!battery_adc_is_ready()) {
        snprintf(output, output_size, "Error: battery ADC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t voltage_mv = battery_get_voltage_mv();
    int percent = battery_get_percent();
    bool charging = battery_is_charging();

    /* Build JSON-like response for easy parsing */
    snprintf(output, output_size,
             "Battery: %lumV (%d%%), %s",
             voltage_mv, percent,
             charging ? "charging" : "discharging");

    ESP_LOGI(TAG, "Battery: %lumV, %d%%, %s",
             voltage_mv, percent, charging ? "charging" : "discharging");

    return ESP_OK;
}

/*============================================================================
 * Chip Temperature
 *============================================================================*/

static esp_err_t temp_sensor_init(void)
{
    if (s_temp_sensor_inited) return ESP_OK;

    temperature_sensor_config_t temp_sensor = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    esp_err_t ret = temperature_sensor_install(&temp_sensor, &s_temp_sensor);
    if (ret == ESP_OK) {
        s_temp_sensor_inited = true;
        ESP_LOGI(TAG, "Temperature sensor initialized");
    }
    return ret;
}

esp_err_t tool_chip_temperature_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    esp_err_t ret = temp_sensor_init();
    if (ret != ESP_OK) {
        snprintf(output, output_size, "Error: temperature sensor init failed");
        return ret;
    }

    ret = temperature_sensor_enable(s_temp_sensor);
    if (ret != ESP_OK) {
        snprintf(output, output_size, "Error: temperature sensor enable failed");
        return ret;
    }

    float temp_c;
    ret = temperature_sensor_get_celsius(s_temp_sensor, &temp_c);
    
    temperature_sensor_disable(s_temp_sensor);

    if (ret != ESP_OK) {
        snprintf(output, output_size, "Error: temperature read failed");
        return ret;
    }

    snprintf(output, output_size, "Chip temperature: %.1f°C", temp_c);
    ESP_LOGI(TAG, "Temperature: %.1f°C", temp_c);

    return ESP_OK;
}

/*============================================================================
 * Hardware Tools Initialization
 *============================================================================*/

esp_err_t tool_hardware_init(void)
{
    /* Pre-initialize LED to off state */
    led_init();
    gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 1 : 0);
    
    ESP_LOGI(TAG, "Hardware tools initialized");
    return ESP_OK;
}
