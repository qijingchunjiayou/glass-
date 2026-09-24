idf_component_register(SRCS "battery_adc.c" "result_uart.c" "main.c" "inmp441.c" "wifi.c" "sntp_sync.c" "xfyun_iat.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_wifi esp_netif esp_event nvs_flash
                             esp_websocket_client mbedtls json esp_http_client
                             esp-tls esp_adc)
