/**
 * @file main.c
 * @brief ESP32-S3 Speech-to-Text Demo using INMP441 + iFlytek IAT
 *
 * Flow:
 *   1. Connect to WiFi
 *   2. Synchronize time via SNTP (needed for iFlytek auth)
 *   3. Initialize INMP441 microphone
 *   4. Loop: record audio and send to iFlytek for recognition
 *
 * Hardware Connection (ESP32-S3):
 *   INMP441    ESP32-S3
 *   -------    --------
 *   VDD    ->  3.3V
 *   GND    ->  GND
 *   SD     ->  GPIO6 (Data)
 *   WS     ->  GPIO5 (Word Select / LRCLK)
 *   SCK    ->  GPIO4 (Bit Clock / BCLK)
 *   L/R    ->  GND (Left channel)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "inmp441.h"
#include "wifi.h"
#include "sntp_sync.h"
#include "xfyun_iat.h"
#include "result_uart.h"
#include "battery_adc.h"

static const char *TAG = "MAIN";

/* Voice activity detection threshold (16-bit PCM RMS value).
 * Normal speech typically produces RMS > 500.
 * Adjust based on your environment and microphone gain. */
#define VAD_RMS_THRESHOLD   2500

/* Number of consecutive frames above threshold to confirm speech */
#define VAD_CONFIRM_FRAMES  3

/**
 * @brief Compute RMS amplitude of 32-bit I2S samples
 *
 * INMP441 outputs 24-bit data in 32-bit frame, MSB-aligned.
 * We shift right by 16 to get the effective 16-bit range,
 * then compute RMS over those values.
 */
static int32_t compute_rms(const int32_t *samples, size_t count)
{
    if (count == 0) return 0;

    int64_t sum_sq = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t val = samples[i] >> 16;  /* Convert to 16-bit range */
        sum_sq += (int64_t)val * val;
    }
    return (int32_t)sqrtf((float)(sum_sq / (int64_t)count));
}

/**
 * @brief Wait for voice activity before starting recognition
 *
 * Continuously reads mic data and computes RMS.
 * Returns true once VAD_CONFIRM_FRAMES consecutive frames
 * exceed VAD_RMS_THRESHOLD.
 */
static void wait_for_speech(void)
{
    const size_t samples_per_frame = IAT_FRAME_SIZE / sizeof(int16_t);  /* 640 */
    const size_t i2s_read_size = samples_per_frame * sizeof(int32_t);   /* 2560 */

    int32_t *buf = malloc(i2s_read_size);
    if (!buf) return;

    int confirm_count = 0;

    while (1) {
        size_t bytes_read = 0;
        esp_err_t ret = inmp441_read(buf, i2s_read_size, &bytes_read, 1000);
        if (ret != ESP_OK || bytes_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        size_t count = bytes_read / sizeof(int32_t);
        int32_t rms = compute_rms(buf, count);

        /* Debug: print RMS every ~1 second to help calibrate threshold */
        static int dbg_cnt = 0;
        if (++dbg_cnt % 25 == 0) {
            printf("RMS: %ld\n", (long)rms);
        }

        if (rms >= VAD_RMS_THRESHOLD) {
            confirm_count++;
            if (confirm_count >= VAD_CONFIRM_FRAMES) {
                break;  /* Speech confirmed */
            }
        } else {
            confirm_count = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(IAT_SEND_INTERVAL));
    }

    free(buf);
}

/**
 * @brief Battery voltage monitor task
 *
 * Reads battery voltage every 30 seconds and prints it.
 */
static void battery_task(void *arg)
{
    while (1) {
        int mv = battery_adc_read_mv();
        if (mv >= 0) {
            printf("Battery: %d.%02dV\n", mv / 1000, (mv % 1000) / 10);
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/**
 * @brief Speech recognition task
 *
 * Waits for voice activity, then performs 5-second recording sessions.
 */
static void speech_task(void *arg)
{
    int session = 0;

    while (1) {
        printf("Listening...\n");
        wait_for_speech();

        session++;
        printf("--- Session %d: Recording ---\n", session);

        esp_err_t ret = xfyun_iat_recognize(5);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Recognition failed: %s", esp_err_to_name(ret));
        }
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    /* Step 1: Connect to WiFi */
    esp_err_t ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi failed");
        return;
    }

    /* Step 2: Synchronize time via SNTP */
    ret = sntp_sync_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SNTP failed");
        return;
    }

    /* Step 3: Initialize INMP441 microphone */
    inmp441_config_t config = INMP441_DEFAULT_CONFIG();
    ret = inmp441_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mic init failed");
        return;
    }

    /* Step 4: Initialize battery ADC (GPIO7) */
    ret = battery_adc_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Battery ADC init failed");
        return;
    }

    /* Step 5: Initialize result UART output (GPIO17) */
    ret = result_uart_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART init failed");
        return;
    }

    /* Step 6: Start tasks */
    xTaskCreate(speech_task, "speech_task", 8192, NULL, 5, NULL);
    xTaskCreate(battery_task, "battery_task", 2048, NULL, 3, NULL);
}
