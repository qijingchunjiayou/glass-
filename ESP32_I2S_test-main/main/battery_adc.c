/**
 * @file battery_adc.c
 * @brief Battery voltage monitor via ADC1_CH6 (GPIO7)
 *
 * Voltage divider: VOUT --[10kΩ]--+--[10kΩ]-- GND
 *                                  |
 *                               BAT_ADC (GPIO7)
 *                                  |
 *                               [100nF] GND
 */

#include "battery_adc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "BAT_ADC";

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;

esp_err_t battery_adc_init(void)
{
    /* Initialize ADC oneshot unit */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = BAT_ADC_UNIT,
    };
    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (ret != ESP_OK) return ret;

    /* Configure channel */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BAT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_oneshot_config_channel(s_adc_handle, BAT_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK) return ret;

    /* Initialize calibration for accurate voltage reading */
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BAT_ADC_UNIT,
        .chan = BAT_ADC_CHANNEL,
        .atten = BAT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Calibration not available, using raw values");
        s_cali_handle = NULL;
    }

    return ESP_OK;
}

int battery_adc_read_mv(void)
{
    if (!s_adc_handle) return -1;

    int raw = 0;
    esp_err_t ret = adc_oneshot_read(s_adc_handle, BAT_ADC_CHANNEL, &raw);
    if (ret != ESP_OK) return -1;

    int voltage_mv = 0;
    if (s_cali_handle) {
        adc_cali_raw_to_voltage(s_cali_handle, raw, &voltage_mv);
    } else {
        /* Rough conversion without calibration: 12-bit, 2450mV range */
        voltage_mv = raw * 2450 / 4095;
    }

    /* Apply voltage divider ratio to get actual battery voltage */
    return voltage_mv * BAT_DIVIDER_RATIO;
}

void battery_adc_deinit(void)
{
    if (s_cali_handle) {
        adc_cali_delete_scheme_curve_fitting(s_cali_handle);
        s_cali_handle = NULL;
    }
    if (s_adc_handle) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
    }
}
