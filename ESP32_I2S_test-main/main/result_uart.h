/**
 * @file result_uart.h
 * @brief UART output for speech recognition results
 *
 * Sends recognized text via UART1 TX (GPIO17) at 115200 baud.
 */

#ifndef __RESULT_UART_H__
#define __RESULT_UART_H__

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RESULT_UART_NUM     UART_NUM_1
#define RESULT_UART_TX_PIN  GPIO_NUM_17
#define RESULT_UART_BAUD    115200

/**
 * @brief Initialize UART1 for result output
 * @return esp_err_t ESP_OK on success
 */
esp_err_t result_uart_init(void);

/**
 * @brief Send recognition result text via UART1
 *
 * Sends the text followed by \r\n.
 *
 * @param text UTF-8 text string to send
 */
void result_uart_send(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* __RESULT_UART_H__ */
