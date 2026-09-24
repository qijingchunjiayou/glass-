/* Copy this file to wifi.h in the same directory and set your own network. */
#ifndef __WIFI_H__
#define __WIFI_H__

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define WIFI_MAX_RETRY 10

esp_err_t wifi_init_sta(void);

#ifdef __cplusplus
}
#endif

#endif /* __WIFI_H__ */
