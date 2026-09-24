#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include "inmp441.h"
#include "wifi.h"
#include "sntp_sync.h"
#include "xfyun_iat.h"
#include "xfyun_spark.h"
#include "result_uart.h"
#include "xiaozhi_activation.h"

/* ---------- Pin & bus assignments ---------- */
#define OLED_I2C_PORT  I2C_NUM_0
#define OLED_SDA_GPIO  8
#define OLED_SCL_GPIO  9

#define MPU_I2C_PORT   I2C_NUM_1
#define MPU_SDA_GPIO   4
#define MPU_SCL_GPIO   5

#define I2C_FREQ_HZ    100000

#define OLED_ADDR_A    0x3C
#define OLED_ADDR_B    0x3D
#define OLED_WIDTH     128
#define OLED_BUF_SIZE  1024

#define MPU_ADDR_LOW   0x68
#define MPU_ADDR_HIGH  0x69
#define MPU_PWR1       0x6B
#define MPU_SAMPLE     0x19
#define MPU_CONFIG     0x1A
#define MPU_GYRO_CFG   0x1B
#define MPU_ACCEL_CFG  0x1C
#define MPU_DATA       0x3B

static const char *TAG = "OLED_IMU";
static uint8_t oled_addr;
static uint8_t mpu_addr;
static uint8_t screen[OLED_BUF_SIZE];
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static char speech_status[22] = "STT STARTING";
static char speech_text[1024];
static uint32_t speech_generation;
static volatile int mic_db10 = -2000;
static volatile int mic_state; // 0: checking, 1: signal, 2: no signal, 3: read error
static volatile bool speech_has_result;
static volatile bool speech_switch_command;
static bool question_mode;
static char activation_status[22] = "XZ WAIT WIFI";
static char activation_code[32];
static bool activation_bound;

extern const uint8_t oled_font16_bin_start[] asm("_binary_oled_font16_bin_start");
extern const uint8_t oled_font16_bin_end[] asm("_binary_oled_font16_bin_end");

#define VAD_RMS_THRESHOLD 500
#define VAD_CONFIRM_FRAMES 3

static void speech_status_set(const char *status)
{
    char next[sizeof(speech_status)] = {0};
    snprintf(next, sizeof(next), "%s", status);
    portENTER_CRITICAL(&status_lock);
    memcpy(speech_status, next, sizeof(next));
    portEXIT_CRITICAL(&status_lock);
}

static void activation_status_set(const char *code, const char *status)
{
    portENTER_CRITICAL(&status_lock);
    snprintf(activation_status, sizeof(activation_status), "%s", status);
    snprintf(activation_code, sizeof(activation_code), "%s", code ? code : "");
    if (strcmp(status, "XZ BOUND") == 0) activation_bound = true;
    portEXIT_CRITICAL(&status_lock);
}

static void activation_task(void *arg)
{
    esp_err_t err = xiaozhi_activation_run(activation_status_set);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Xiaozhi activation stopped: %s", esp_err_to_name(err));
        activation_status_set(NULL, "XZ LOCAL ERROR");
    }
    vTaskDelete(NULL);
}

void speech_result_received(const char *text)
{
    if (!text) return;
    bool switch_to_question = strstr(text, "问答模式") != NULL;
    bool switch_to_text = strstr(text, "转写模式") != NULL ||
                          strstr(text, "文字模式") != NULL;
    portENTER_CRITICAL(&status_lock);
    if (switch_to_question || switch_to_text) {
        question_mode = switch_to_question;
        speech_switch_command = true;
        snprintf(speech_text, sizeof(speech_text), "%s",
                 switch_to_question ? "已切换到问答模式" : "已切换到转写模式");
    } else {
        snprintf(speech_text, sizeof(speech_text), "%s", text);
    }
    ++speech_generation;
    speech_has_result = true;
    portEXIT_CRITICAL(&status_lock);
    speech_status_set(switch_to_question ? "QA MODE" :
                      switch_to_text ? "TEXT MODE" : "STT RESULT");
}

void speech_recording_started(void)
{
    speech_status_set("STT SPEAK NOW");
}

static bool wait_for_speech(void)
{
    const size_t count = IAT_FRAME_SIZE / sizeof(int16_t);
    int32_t *samples = malloc(count * sizeof(*samples));
    if (!samples) return false;

    int consecutive = 0;
    int report_count = 0;
    int signal_frames = 0;
    int failed_frames = 0;
    while (true) {
        size_t bytes_read = 0;
        esp_err_t ret = inmp441_read(samples, count * sizeof(*samples), &bytes_read, 1000);
        if (ret != ESP_OK || bytes_read < sizeof(*samples)) {
            ESP_LOGW(TAG, "Mic read: %s", esp_err_to_name(ret));
            if (++failed_frames >= 3) mic_state = 3;
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        failed_frames = 0;

        int64_t sum_sq = 0;
        size_t n = bytes_read / sizeof(*samples);
        for (size_t i = 0; i < n; ++i) {
            int32_t sample = samples[i] >> 16;
            sum_sq += (int64_t)sample * sample;
        }
        float rms = sqrtf((float)sum_sq / (float)n);
        float dbfs = rms > 0 ? 20.0f * log10f(rms / 32768.0f) : -120.0f;
        mic_db10 = (int)(10.0f * fmaxf(dbfs, -120.0f));
        // A nonzero RMS is evidence of audio data, not a hardware identity check.
        signal_frames = rms >= 4.0f ? signal_frames + 1 : 0;
        ++report_count;
        if (signal_frames >= 3) mic_state = 1;
        else if (report_count >= 25) mic_state = 2;
        if (report_count % 25 == 0) ESP_LOGI(TAG, "Mic RMS: %.0f", rms);

        consecutive = rms >= VAD_RMS_THRESHOLD ? consecutive + 1 : 0;
        if (consecutive >= VAD_CONFIRM_FRAMES) break;
        vTaskDelay(pdMS_TO_TICKS(IAT_SEND_INTERVAL));
    }

    free(samples);
    return true;
}

static void speech_task(void *arg)
{
    // Probe I2S before network setup so the microphone state appears immediately.
    const size_t probe_samples = IAT_FRAME_SIZE / sizeof(int16_t);
    int32_t *probe = malloc(probe_samples * sizeof(*probe));
    if (probe) {
        int good = 0;
        for (int i = 0; i < 25; ++i) {
            size_t bytes = 0;
            if (inmp441_read(probe, probe_samples * sizeof(*probe), &bytes, 1000) == ESP_OK && bytes > 0) {
                int32_t peak = 0;
                for (size_t j = 0; j < bytes / sizeof(*probe); ++j) {
                    int32_t v = probe[j] >> 16;
                    if (v > peak) peak = v;
                    if (-v > peak) peak = -v;
                }
                if (peak >= 16) ++good;
            }
            if (good >= 3) break;
        }
        mic_state = good >= 3 ? 1 : 2;
        free(probe);
    } else {
        mic_state = 3;
    }
    speech_status_set("STT WIFI...");
    if (wifi_init_sta() != ESP_OK) {
        speech_status_set("STT WIFI FAIL");
        activation_status_set(NULL, "XZ WIFI FAIL");
        vTaskDelete(NULL);
    }
    speech_status_set("STT TIME...");
    if (sntp_sync_init() != ESP_OK) {
        speech_status_set("STT TIME FAIL");
        activation_status_set(NULL, "XZ TIME FAIL");
        vTaskDelete(NULL);
    }
    if (result_uart_init() != ESP_OK) ESP_LOGW(TAG, "UART1 unavailable; USB log still works");
    if (xTaskCreatePinnedToCore(activation_task, "xz_activate", 8192,
                                NULL, 2, NULL, 0) != pdPASS) {
        activation_status_set(NULL, "XZ TASK FAIL");
        ESP_LOGE(TAG, "Xiaozhi activation task creation failed");
    }

    while (true) {
        speech_status_set(question_mode ? "QA LISTENING" : "STT LISTENING");
        if (!wait_for_speech()) {
            speech_status_set("STT NO MEMORY");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        speech_has_result = false;
        speech_switch_command = false;
        speech_status_set("STT CONNECTING");
        esp_err_t ret = xfyun_iat_recognize(IAT_RECORD_SECONDS);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Speech recognition: %s", esp_err_to_name(ret));
            speech_status_set("STT ERROR SEE USB");
        } else if (!speech_has_result) {
            speech_status_set("STT NO WORDS");
        } else if (question_mode && !speech_switch_command) {
            char question[sizeof(speech_text)];
            char answer[sizeof(speech_text)];
            portENTER_CRITICAL(&status_lock);
            memcpy(question, speech_text, sizeof(question));
            portEXIT_CRITICAL(&status_lock);
            if (!xfyun_spark_configured()) {
                speech_status_set("QA CONFIG MISSING");
            } else {
                speech_status_set("QA THINKING");
                ret = xfyun_spark_ask(question, answer, sizeof(answer));
                if (ret == ESP_OK) {
                    portENTER_CRITICAL(&status_lock);
                    snprintf(speech_text, sizeof(speech_text), "%s", answer);
                    ++speech_generation;
                    portEXIT_CRITICAL(&status_lock);
                    speech_status_set("QA ANSWER");
                } else {
                    ESP_LOGE(TAG, "Spark request failed: %s", esp_err_to_name(ret));
                    speech_status_set("QA ERROR SEE USB");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

/* 5x7 ASCII font, characters 0x20 through 0x5A. */
static const uint8_t font[][5] = {
    {0,0,0,0,0},{0,0,0x5F,0,0},{0,7,0,7,0},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,8,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0,5,3,0,0},
    {0,0x1C,0x22,0x41,0},{0,0x41,0x22,0x1C,0},{0x14,8,0x3E,8,0x14},{8,8,0x3E,8,8},
    {0,0x50,0x30,0,0},{8,8,8,8,8},{0,0x60,0x60,0,0},{0x20,0x10,8,4,2},
    {0x3E,0x51,0x49,0x45,0x3E},{0,0x42,0x7F,0x40,0},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{1,0x71,9,5,3},
    {0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1E},{0,0x36,0x36,0,0},{0,0x56,0x36,0,0},
    {8,0x14,0x22,0x41,0},{0x14,0x14,0x14,0x14,0x14},{0,0x41,0x22,0x14,8},{2,1,0x51,9,6},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,9,9,9,1},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,8,8,8,0x7F},{0,0x41,0x7F,0x41,0},{0x20,0x40,0x41,0x3F,1},{0x7F,8,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,2,0x0C,2,0x7F},{0x7F,4,8,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,9,9,9,6},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,9,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {1,1,0x7F,1,1},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,8,0x14,0x63},{7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43}
};

/* ---------- I2C helpers ---------- */
static esp_err_t probe(i2c_port_t port, uint8_t address)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, address << 1, true);
    i2c_master_stop(cmd);
    esp_err_t result = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return result;
}

static esp_err_t reg_write(i2c_port_t port, uint8_t address, uint8_t reg, uint8_t value)
{
    uint8_t data[] = {reg, value};
    return i2c_master_write_to_device(port, address, data, 2, pdMS_TO_TICKS(100));
}

static esp_err_t regs_read(i2c_port_t port, uint8_t address, uint8_t reg,
                           uint8_t *data, size_t size)
{
    return i2c_master_write_read_device(port, address, &reg, 1, data, size,
                                        pdMS_TO_TICKS(100));
}

/* ---------- OLED SSD1306 driver ---------- */
static esp_err_t oled_cmd(const uint8_t *commands, size_t count)
{
    uint8_t packet[32];
    if (count > 31) return ESP_ERR_INVALID_SIZE;
    packet[0] = 0;
    memcpy(packet + 1, commands, count);
    return i2c_master_write_to_device(OLED_I2C_PORT, oled_addr, packet, count + 1,
                                      pdMS_TO_TICKS(100));
}

static void oled_clear(void)
{
    memset(screen, 0, sizeof(screen));
}

static void oled_text(uint8_t x, uint8_t page, const char *text)
{
    if (page > 7) return;
    while (*text && x + 5 < OLED_WIDTH) {
        char c = *text++;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < ' ' || c > 'Z') c = '?';
        for (int i = 0; i < 5; ++i) screen[page * OLED_WIDTH + x++] = font[c - ' '][i];
        screen[page * OLED_WIDTH + x++] = 0;
    }
}

static uint16_t utf8_next(const char **cursor)
{
    const unsigned char *p = (const unsigned char *)*cursor;
    if (!*p) return 0;
    if (*p < 0x80) {
        *cursor += 1;
        return *p;
    }
    if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        *cursor += 2;
        return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    }
    if ((*p & 0xF0) == 0xE0 && p[1] && p[2] &&
        (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *cursor += 3;
        return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    *cursor += 1;
    return '?';
}

static void oled_hanzi(int x, int page, uint16_t codepoint)
{
    size_t count = (size_t)(oled_font16_bin_end - oled_font16_bin_start) / 34;
    size_t high = count;
    size_t low = 0;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const uint8_t *entry = oled_font16_bin_start + mid * 34;
        uint16_t value = (uint16_t)entry[0] | ((uint16_t)entry[1] << 8);
        if (value < codepoint) low = mid + 1;
        else high = mid;
    }
    if (low == count) {
        oled_text(x, page, "?");
        return;
    }
    const uint8_t *entry = oled_font16_bin_start + low * 34;
    if (((uint16_t)entry[0] | ((uint16_t)entry[1] << 8)) != codepoint) {
        oled_text(x, page, "?");
        return;
    }
    memcpy(screen + page * OLED_WIDTH + x, entry + 2, 16);
    memcpy(screen + (page + 1) * OLED_WIDTH + x, entry + 18, 16);
}

// Three 16-pixel rows; returns the byte offset at which the next page starts.
static size_t oled_result_page(const char *text, size_t offset)
{
    const char *cursor = text + offset;
    int row = 0;
    int x = 0;
    while (*cursor) {
        const char *start = cursor;
        uint16_t ch = utf8_next(&cursor);
        if (ch == '\n') { row++; x = 0; }
        else {
            int width = ch < 0x80 ? 6 : 16;
            if (x + width > OLED_WIDTH) { row++; x = 0; }
            if (row >= 3) return (size_t)(start - text);
            if (ch < 0x80) {
                char ascii[] = {(char)ch, 0};
                oled_text(x, 2 + row * 2, ascii);
            } else {
                oled_hanzi(x, 2 + row * 2, ch);
            }
            x += width;
        }
        if (row >= 3) return (size_t)(cursor - text);
    }
    return 0;
}

static esp_err_t oled_show(void)
{
    const uint8_t position[] = {0x21, 0, 127, 0x22, 0, 7};
    ESP_RETURN_ON_ERROR(oled_cmd(position, sizeof(position)), TAG, "OLED position");
    for (size_t offset = 0; offset < sizeof(screen); offset += 16) {
        uint8_t packet[17] = {0x40};
        memcpy(packet + 1, screen + offset, 16);
        ESP_RETURN_ON_ERROR(i2c_master_write_to_device(OLED_I2C_PORT, oled_addr, packet,
                            sizeof(packet), pdMS_TO_TICKS(100)), TAG, "OLED data");
    }
    return ESP_OK;
}

static esp_err_t oled_init(void)
{
    if (probe(OLED_I2C_PORT, OLED_ADDR_A) == ESP_OK) oled_addr = OLED_ADDR_A;
    else if (probe(OLED_I2C_PORT, OLED_ADDR_B) == ESP_OK) oled_addr = OLED_ADDR_B;
    else return ESP_ERR_NOT_FOUND;

    const uint8_t init[] = {
        0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0,0x40,0x8D,0x14,0x20,0,
        0xA1,0xC8,0xDA,0x12,0x81,0x7F,0xD9,0xF1,0xDB,0x40,0xA4,0xA6,0x2E,0xAF
    };
    ESP_RETURN_ON_ERROR(oled_cmd(init, sizeof(init)), TAG, "OLED init");
    oled_clear();
    return oled_show();
}

/* ---------- MPU6050 driver ---------- */
static esp_err_t mpu_init(void)
{
    esp_err_t probe_68 = probe(MPU_I2C_PORT, MPU_ADDR_LOW);
    esp_err_t probe_69 = probe(MPU_I2C_PORT, MPU_ADDR_HIGH);
    ESP_LOGI(TAG, "MPU probe: 0x68=%s, 0x69=%s",
             esp_err_to_name(probe_68), esp_err_to_name(probe_69));

    if (probe_68 == ESP_OK) mpu_addr = MPU_ADDR_LOW;
    else if (probe_69 == ESP_OK) mpu_addr = MPU_ADDR_HIGH;
    else return ESP_ERR_NOT_FOUND;

    ESP_LOGI(TAG, "MPU6050 found at 0x%02X", mpu_addr);
    ESP_RETURN_ON_ERROR(reg_write(MPU_I2C_PORT, mpu_addr, MPU_PWR1, 0x80), TAG, "MPU reset");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(reg_write(MPU_I2C_PORT, mpu_addr, MPU_PWR1, 0x01), TAG, "MPU clock");
    ESP_RETURN_ON_ERROR(reg_write(MPU_I2C_PORT, mpu_addr, MPU_SAMPLE, 9), TAG, "MPU sample rate");
    ESP_RETURN_ON_ERROR(reg_write(MPU_I2C_PORT, mpu_addr, MPU_CONFIG, 4), TAG, "MPU filter");
    ESP_RETURN_ON_ERROR(reg_write(MPU_I2C_PORT, mpu_addr, MPU_GYRO_CFG, 8), TAG, "MPU gyro range");
    return reg_write(MPU_I2C_PORT, mpu_addr, MPU_ACCEL_CFG, 8);
}

static int16_t signed_be16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/* ---------- Fatal error display ---------- */
static void stop_with_error(const char *message)
{
    ESP_LOGE(TAG, "%s", message);
    if (oled_addr) {
        oled_clear();
        oled_text(0, 2, "ERROR");
        oled_text(0, 4, message);
        oled_show();
    }
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

/* ---------- I2C bus bring-up ---------- */
static esp_err_t i2c_bus_install(i2c_port_t port, int sda, int scl)
{
    const i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
        .clk_flags = 0,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(port, &cfg), TAG, "i2c_param_config");
    return i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0);
}

/* ---------- Sensor task: read MPU + INMP441, refresh OLED ---------- */
static void sensor_loop(void *arg)
{
    char    line[22];
    char    status[22];
    char    xz_status[22];
    char    xz_code[sizeof(activation_code)];
    bool    xz_bound;
    char    result[sizeof(speech_text)];
    uint32_t seen_generation = 0;
    size_t page_offset = 0;
    size_t next_offset = 0;
    int page_ticks = 0;
    int log_ticks = 0;

    int16_t ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
    int16_t temp_raw = 0;

    while (true) {
        /* --- MPU6050: 14-byte burst from 0x3B (accel+temp+gyro) --- */
        uint8_t raw[14] = {0};
        esp_err_t mpu_err = regs_read(MPU_I2C_PORT, mpu_addr, MPU_DATA, raw, 14);
        if (mpu_err == ESP_OK) {
            ax = signed_be16(raw + 0);
            ay = signed_be16(raw + 2);
            az = signed_be16(raw + 4);
            temp_raw = signed_be16(raw + 6);
            gx = signed_be16(raw + 8);
            gy = signed_be16(raw + 10);
            gz = signed_be16(raw + 12);
        } else {
            ESP_LOGW(TAG, "MPU read failed: %s", esp_err_to_name(mpu_err));
        }

        portENTER_CRITICAL(&status_lock);
        memcpy(status, speech_status, sizeof(status));
        memcpy(xz_status, activation_status, sizeof(xz_status));
        memcpy(xz_code, activation_code, sizeof(xz_code));
        xz_bound = activation_bound;
        memcpy(result, speech_text, sizeof(result));
        uint32_t generation = speech_generation;
        portEXIT_CRITICAL(&status_lock);
        float accel_x = (float)ax * 9.80665f / 8192.0f;
        float accel_y = (float)ay * 9.80665f / 8192.0f;
        float accel_z = (float)az * 9.80665f / 8192.0f;
        float gyro_x = (float)gx / 65.5f;
        float gyro_y = (float)gy / 65.5f;
        float gyro_z = (float)gz / 65.5f;
        float temp_c = (float)temp_raw / 340.0f + 36.53f;

        if (generation != seen_generation) {
            seen_generation = generation;
            page_offset = 0;
            page_ticks = 0;
        } else if (++page_ticks >= 15) {
            page_offset = next_offset;
            page_ticks = 0;
        }

        /* --- OLED render: mic state always precedes recognition text. --- */
        oled_clear();
        const char *mic_label = mic_state == 1 ? "MIC SIGNAL" :
                                mic_state == 2 ? "MIC NO SIGNAL" :
                                mic_state == 3 ? "MIC READ ERROR" : "MIC CHECKING";
        oled_text(0, 0, mic_label);
        if (xz_bound) {
            oled_text(90, 0, "XZ OK");
        } else if (mic_db10 != -2000) {
            snprintf(line, sizeof(line), "%4.0fDB", mic_db10 / 10.0f);
            oled_text(90, 0, line);
        }
        if (xz_bound) {
            snprintf(line, sizeof(line), "XF %.18s", status);
            oled_text(0, 1, line);
        } else {
            oled_text(0, 1, xz_status);
        }
        if (!xz_bound) {
            oled_text(0, 2, "XIAOZHI.ME");
            if (xz_code[0]) {
                oled_text(0, 4, "CODE:");
                oled_text(0, 5, xz_code);
            } else {
                oled_text(0, 4, "WAIT FOR CODE");
            }
            next_offset = 0;
        } else if (result[0]) next_offset = oled_result_page(result, page_offset);
        else {
            oled_text(0, 3, "WAITING FOR SPEECH");
            next_offset = 0;
        }
        oled_show();

        if (++log_ticks >= 25) {
            ESP_LOGI(TAG, "MPU accel %.2f %.2f %.2f m/s2 gyro %.1f %.1f %.1f dps temp %.1f C",
                     accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z, temp_c);
            log_ticks = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* ---------- Entry point ---------- */
void app_main(void)
{
    ESP_ERROR_CHECK(i2c_bus_install(OLED_I2C_PORT, OLED_SDA_GPIO, OLED_SCL_GPIO));
    ESP_LOGI(TAG, "OLED I2C0: SDA=GPIO%d SCL=GPIO%d @ %d Hz",
             OLED_SDA_GPIO, OLED_SCL_GPIO, I2C_FREQ_HZ);

    ESP_ERROR_CHECK(i2c_bus_install(MPU_I2C_PORT, MPU_SDA_GPIO, MPU_SCL_GPIO));
    ESP_LOGI(TAG, "MPU  I2C1: SDA=GPIO%d SCL=GPIO%d @ %d Hz (addr 0x%02X)",
             MPU_SDA_GPIO, MPU_SCL_GPIO, I2C_FREQ_HZ, MPU_ADDR_LOW);

    if (oled_init() != ESP_OK) {
        ESP_LOGE(TAG, "OLED not found at 0x3C or 0x3D");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "OLED ready at 0x%02X", oled_addr);

    if (mpu_init() != ESP_OK) {
        stop_with_error("MPU I2C FAIL 68/69");
    }
    ESP_LOGI(TAG, "MPU6050 ready");

    /* INMP441 uses I2S_NUM_0 with default SCK=2 WS=15 SD=16 (see inmp441.h) */
    if (inmp441_init(NULL) != ESP_OK) {
        stop_with_error("INMP441 init failed");
    }
    ESP_LOGI(TAG, "INMP441 ready (I2S0 SCK=GPIO%d WS=GPIO%d SD=GPIO%d, %lu Hz, 24-bit mono)",
             INMP441_SCK_IO, INMP441_WS_IO, INMP441_SD_IO,
             (unsigned long)INMP441_SAMPLE_RATE);

    /* Probe audio before claiming that a microphone is connected. */
    oled_clear();
    oled_text(20, 1, "OLED_IMU");
    oled_text(14, 3, "MPU6050 OK");
    oled_text(14, 5, "MIC CHECKING");
    oled_show();
    vTaskDelay(pdMS_TO_TICKS(800));

    BaseType_t task_created = xTaskCreatePinnedToCore(sensor_loop, "sensor", 8192,
                                                       NULL, 4, NULL, 0);
    if (task_created != pdPASS) {
        stop_with_error("SENSOR TASK FAIL");
    }
    if (xTaskCreatePinnedToCore(speech_task, "speech", 10240, NULL, 3, NULL, 1) != pdPASS) {
        speech_status_set("STT TASK FAIL");
        ESP_LOGE(TAG, "Speech task creation failed");
    }
    vTaskDelete(NULL);
}
