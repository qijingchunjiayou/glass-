/**
 * @file sntp_sync.h
 * @brief SNTP Time Synchronization Module
 *
 * iFlytek IAT API requires accurate timestamps for authentication.
 */

#ifndef __SNTP_SYNC_H__
#define __SNTP_SYNC_H__

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SNTP and synchronize system time
 *
 * Blocks until time is synchronized or timeout.
 * Must be called after WiFi is connected.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sntp_sync_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __SNTP_SYNC_H__ */
