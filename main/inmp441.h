/**
 * @file inmp441.h
 * @brief INMP441 I2S MEMS Microphone Driver for ESP32-S3
 */

#ifndef __INMP441_H__
#define __INMP441_H__

#include "driver/i2s_std.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * INMP441 Pin Definitions for ESP32-S3
 *
 * INMP441 Module Pins:
 *   - VDD: 3.3V
 *   - GND: Ground
 *   - SD:  Serial Data Output (connect to ESP32 DIN)
 *   - WS:  Word Select / LRCLK (Left/Right Clock)
 *   - SCK: Serial Clock / BCLK (Bit Clock)
 *   - L/R: Left/Right Channel Select (GND=Left, VDD=Right)
 */

/* Default GPIO Configuration - Modify according to your wiring */
#define INMP441_SCK_IO      GPIO_NUM_2    // I2S Bit Clock (BCLK)
#define INMP441_WS_IO       GPIO_NUM_15   // I2S Word Select (LRCLK)
#define INMP441_SD_IO       GPIO_NUM_16   // I2S Data In (from microphone)

/* I2S Configuration */
#define INMP441_SAMPLE_RATE     16000    // Sample rate in Hz (16kHz recommended for voice)
#define INMP441_BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_32BIT  // INMP441 outputs 24-bit in 32-bit frame
#define INMP441_DMA_BUF_COUNT   4        // Number of DMA buffers
#define INMP441_DMA_BUF_LEN     1024     // Samples per DMA buffer

/**
 * @brief INMP441 configuration structure
 */
typedef struct {
    gpio_num_t sck_io;          // Bit Clock pin
    gpio_num_t ws_io;           // Word Select pin
    gpio_num_t sd_io;           // Data pin
    uint32_t sample_rate;       // Sample rate in Hz
    i2s_data_bit_width_t bits;  // Bits per sample
} inmp441_config_t;

/**
 * @brief Default INMP441 configuration
 */
#define INMP441_DEFAULT_CONFIG() { \
    .sck_io = INMP441_SCK_IO, \
    .ws_io = INMP441_WS_IO, \
    .sd_io = INMP441_SD_IO, \
    .sample_rate = INMP441_SAMPLE_RATE, \
    .bits = INMP441_BITS_PER_SAMPLE, \
}

/**
 * @brief Initialize INMP441 microphone
 *
 * @param config Pointer to configuration structure, NULL for default config
 * @return esp_err_t ESP_OK on success
 */
esp_err_t inmp441_init(const inmp441_config_t *config);

/**
 * @brief Deinitialize INMP441 microphone
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t inmp441_deinit(void);

/**
 * @brief Read audio samples from INMP441
 *
 * @param buffer Buffer to store samples
 * @param buffer_size Size of buffer in bytes
 * @param bytes_read Pointer to store actual bytes read
 * @param timeout_ms Read timeout in milliseconds
 * @return esp_err_t ESP_OK on success
 */
esp_err_t inmp441_read(void *buffer, size_t buffer_size, size_t *bytes_read, uint32_t timeout_ms);

/**
 * @brief Get the I2S channel handle (for advanced operations)
 *
 * @return i2s_chan_handle_t Channel handle, NULL if not initialized
 */
i2s_chan_handle_t inmp441_get_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* __INMP441_H__ */