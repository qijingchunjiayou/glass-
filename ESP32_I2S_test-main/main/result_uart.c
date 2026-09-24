/**
 * @file result_uart.c
 * @brief UART output for speech recognition results
 */

#include "result_uart.h"
#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"

#define UART_TX_BUF_SIZE 1024

esp_err_t result_uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = RESULT_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(RESULT_UART_NUM, 256, UART_TX_BUF_SIZE, 0, NULL, 0);
    if (ret != ESP_OK) return ret;

    ret = uart_param_config(RESULT_UART_NUM, &uart_config);
    if (ret != ESP_OK) return ret;

    ret = uart_set_pin(RESULT_UART_NUM, RESULT_UART_TX_PIN, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    return ret;
}

void result_uart_send(const char *text)
{
    if (!text || text[0] == '\0') return;

    size_t len = strlen(text);
    uart_write_bytes(RESULT_UART_NUM, text, len);
    uart_write_bytes(RESULT_UART_NUM, "\r\n", 2);
}
