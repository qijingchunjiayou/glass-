#include "result_uart.h"

#include <stdbool.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/uart.h"

void speech_result_received(const char *text);
static bool uart_ready;

esp_err_t result_uart_init(void)
{
    const uart_config_t cfg = {
        .baud_rate = RESULT_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(RESULT_UART_NUM, 256, 1024, 0, NULL, 0);
    if (ret != ESP_OK) return ret;
    ret = uart_param_config(RESULT_UART_NUM, &cfg);
    if (ret != ESP_OK) return ret;
    ret = uart_set_pin(RESULT_UART_NUM, RESULT_UART_TX_PIN, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_ready = ret == ESP_OK;
    return ret;
}

void result_uart_send(const char *text)
{
    if (!text || !*text) return;
    speech_result_received(text);
    if (!uart_ready) return;
    uart_write_bytes(RESULT_UART_NUM, text, strlen(text));
    uart_write_bytes(RESULT_UART_NUM, "\r\n", 2);
}
