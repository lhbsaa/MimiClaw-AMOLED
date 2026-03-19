/**
 * Battery ADC Reader for T-Display-S3 AMOLED 1.91"
 *
 * GPIO4 is connected to battery voltage via a voltage divider (2:1).
 * ADC reading * 2 = actual battery voltage.
 *
 * Typical Li-Po voltage range:
 *   3.3V = empty (0%)
 *   4.2V = full (100%)
 *   > 4.25V = likely charging via USB
 */
#include "battery_adc.h"
#include "mimi_config.h"

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "battery";

/* Smoothing: simple moving average */
#define BATT_AVG_SAMPLES 8

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;
static bool s_initialized = false;

/* ADC channel for GPIO4 on ESP32-S3 = ADC1_CHANNEL_3 */
#define BATTERY_ADC_CHANNEL  ADC_CHANNEL_3
#define BATTERY_ADC_UNIT     ADC_UNIT_1
#define BATTERY_ADC_ATTEN    ADC_ATTEN_DB_12   /* 0-3.3V range */
#define BATTERY_ADC_BITWIDTH ADC_BITWIDTH_12

esp_err_t battery_adc_init(void)
{
    /* Initialize ADC oneshot unit */
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Configure channel */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    err = adc_oneshot_config_channel(s_adc_handle, BATTERY_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Try to create calibration handle (curve fitting) */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
        .chan = BATTERY_ADC_CHANNEL,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration: curve fitting");
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    err = adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration: line fitting");
    }
#endif

    if (!s_cali_handle) {
        ESP_LOGW(TAG, "ADC calibration not available, raw readings only");
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Battery ADC initialized on GPIO%d (channel %d)",
             BATTERY_ADC_PIN, BATTERY_ADC_CHANNEL);
    return ESP_OK;
}

uint32_t battery_get_voltage_mv(void)
{
    if (!s_initialized) return 0;

    /* Average multiple samples for stability */
    int32_t sum = 0;
    int valid = 0;

    for (int i = 0; i < BATT_AVG_SAMPLES; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc_handle, BATTERY_ADC_CHANNEL, &raw) == ESP_OK) {
            if (s_cali_handle) {
                int voltage_mv = 0;
                if (adc_cali_raw_to_voltage(s_cali_handle, raw, &voltage_mv) == ESP_OK) {
                    sum += voltage_mv;
                    valid++;
                }
            } else {
                /* Fallback: approximate conversion without calibration
                 * 12-bit ADC, 3.3V reference with 12dB attenuation */
                sum += (raw * MIMI_BATT_MV_FULL) / 4095;
                valid++;
            }
        }
    }

    if (valid == 0) return 0;

    uint32_t adc_mv = (uint32_t)(sum / valid);

    /* Apply voltage divider ratio: actual voltage = adc reading * 2 */
    return adc_mv * MIMI_BATT_DIVIDER_RATIO;
}

int battery_get_percent(void)
{
    uint32_t mv = battery_get_voltage_mv();
    if (mv == 0) return 0;

    if (mv <= MIMI_BATT_MV_EMPTY) return 0;
    if (mv >= MIMI_BATT_MV_FULL)  return 100;

    /* Linear interpolation */
    int pct = (int)((mv - MIMI_BATT_MV_EMPTY) * 100 / (MIMI_BATT_MV_FULL - MIMI_BATT_MV_EMPTY));
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

bool battery_is_charging(void)
{
    uint32_t mv = battery_get_voltage_mv();
    return (mv > MIMI_BATT_MV_CHARGING);
}

bool battery_adc_is_ready(void)
{
    return s_initialized;
}
