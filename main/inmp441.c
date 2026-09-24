/**
 * @file inmp441.c
 * @brief INMP441 I2S MEMS Microphone Driver Implementation
 */

#include "inmp441.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "INMP441";

/* I2S RX channel handle */
static i2s_chan_handle_t s_rx_handle = NULL;

esp_err_t inmp441_init(const inmp441_config_t *config)
{
    esp_err_t ret;

    /* Use default config if not provided */
    inmp441_config_t cfg;
    if (config == NULL) {
        cfg = (inmp441_config_t)INMP441_DEFAULT_CONFIG();
        config = &cfg;
    }

    /* Check if already initialized */
    if (s_rx_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Allocate I2S RX channel */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = INMP441_DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = INMP441_DMA_BUF_LEN;

    ret = i2s_new_channel(&chan_cfg, NULL, &s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure I2S Standard RX mode
     * INMP441 uses Philips/I2S standard format:
     * - Data is valid on falling edge of BCLK
     * - WS low = Left channel, WS high = Right channel
     * - Data is MSB first, with 1 BCLK delay after WS transition
     */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config->sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(config->bits, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,    // INMP441 doesn't need MCLK
            .bclk = config->sck_io,
            .ws = config->ws_io,
            .dout = I2S_GPIO_UNUSED,    // We're only receiving
            .din = config->sd_io,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    /* For mono mode, we only want left channel (L/R pin connected to GND) */
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ret = i2s_channel_init_std_mode(s_rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S STD mode: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
        return ret;
    }

    /* Enable the I2S RX channel */
    ret = i2s_channel_enable(s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
        return ret;
    }

    return ESP_OK;
}

esp_err_t inmp441_deinit(void)
{
    if (s_rx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Disable and delete the channel */
    i2s_channel_disable(s_rx_handle);
    i2s_del_channel(s_rx_handle);
    s_rx_handle = NULL;

    return ESP_OK;
}

esp_err_t inmp441_read(void *buffer, size_t buffer_size, size_t *bytes_read, uint32_t timeout_ms)
{
    if (s_rx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (buffer == NULL || bytes_read == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2s_channel_read(s_rx_handle, buffer, buffer_size, bytes_read, timeout_ms);
}

i2s_chan_handle_t inmp441_get_handle(void)
{
    return s_rx_handle;
}