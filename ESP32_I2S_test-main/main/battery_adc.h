/**
 * @file battery_adc.h
 * @brief Battery voltage monitor via ADC
 *
 * Uses GPIO7 (ADC1_CH6) with 10kΩ + 10kΩ voltage divider.
 * Actual battery voltage = ADC reading × 2.
 */

#ifndef __BATTERY_ADC_H__
#define __BATTERY_ADC_H__

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ADC configuration */
#define BAT_ADC_CHANNEL     ADC_CHANNEL_6   /* GPIO7 = ADC1_CH6 */
#define BAT_ADC_ATTEN       ADC_ATTEN_DB_11 /* 0~2450mV range, enough for 4.2V/2=2.1V */
#define BAT_ADC_UNIT        ADC_UNIT_1

/* Voltage divider ratio: R9/(R9+R10) = 10k/(10k+10k) = 0.5
 * So battery_voltage = adc_voltage * 2 */
#define BAT_DIVIDER_RATIO   2

/**
 * @brief Initialize ADC for battery voltage reading
 * @return esp_err_t ESP_OK on success
 */
esp_err_t battery_adc_init(void);

/**
 * @brief Read battery voltage in millivolts
 * @return Battery voltage in mV (e.g. 4200 = 4.2V), or -1 on error
 */
int battery_adc_read_mv(void);

/**
 * @brief Deinitialize ADC
 */
void battery_adc_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __BATTERY_ADC_H__ */
