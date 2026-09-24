/**
 * @file sntp_sync.c
 * @brief SNTP Time Synchronization Implementation
 */

#include "sntp_sync.h"
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "esp_sntp.h"

static const char *TAG = "SNTP";

esp_err_t sntp_sync_init(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_init();

    /* Wait for time to be synchronized */
    int retry = 0;
    const int max_retry = 30;
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && retry < max_retry) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    if (retry >= max_retry) {
        ESP_LOGE(TAG, "Time sync failed after %d retries", max_retry);
        return ESP_FAIL;
    }

    /* Set timezone to China Standard Time (UTC+8) */
    setenv("TZ", "CST-8", 1);
    tzset();

    return ESP_OK;
}
