set(SPEECH_DEMO_DIR "${CMAKE_CURRENT_LIST_DIR}/../ESP32_I2S_test-main/main")
if("$ENV{IDF_COMPONENT_MANAGER}" STREQUAL "0")
    set(WEBSOCKET_COMPONENT espressif__esp_websocket_client)
else()
    set(WEBSOCKET_COMPONENT esp_websocket_client)
endif()

idf_component_register(
    SRCS "inmp441.c" "main.c" "result_uart.c" "xfyun_spark.c" "xiaozhi_activation.c"
         "${SPEECH_DEMO_DIR}/wifi.c"
         "${SPEECH_DEMO_DIR}/sntp_sync.c"
         "${SPEECH_DEMO_DIR}/xfyun_iat.c"
    INCLUDE_DIRS "." "${SPEECH_DEMO_DIR}"
    EMBED_FILES "oled_font16.bin"
    REQUIRES driver esp_wifi esp_netif esp_event nvs_flash esp_app_format spi_flash
             ${WEBSOCKET_COMPONENT} mbedtls json esp_http_client esp-tls
)
