#include "xiaozhi_activation.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_crt_bundle.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#define XIAOZHI_OTA_URL "https://api.xiaozhi.me/ota/"
#define MAX_HTTP_BODY 4096

static const char *TAG = "XIAOZHI";

typedef struct {
    char body[MAX_HTTP_BODY + 1];
    size_t used;
    bool overflow;
} http_body_t;

static esp_err_t on_http_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0)
        return ESP_OK;
    http_body_t *result = event->user_data;
    if (result->used + (size_t)event->data_len > MAX_HTTP_BODY) {
        result->overflow = true;
        return ESP_FAIL;
    }
    memcpy(result->body + result->used, event->data, event->data_len);
    result->used += event->data_len;
    result->body[result->used] = 0;
    return ESP_OK;
}

static void get_device_id(char device_id[18])
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static esp_err_t get_client_id(char client_id[37])
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("xiaozhi", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    size_t length = 37;
    err = nvs_get_str(nvs, "client_id", client_id, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        uint8_t uuid[16];
        esp_fill_random(uuid, sizeof(uuid));
        uuid[6] = (uuid[6] & 0x0f) | 0x40;
        uuid[8] = (uuid[8] & 0x3f) | 0x80;
        snprintf(client_id, 37,
                 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 uuid[0], uuid[1], uuid[2], uuid[3], uuid[4],
                 uuid[5], uuid[6], uuid[7], uuid[8], uuid[9],
                 uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
        err = nvs_set_str(nvs, "client_id", client_id);
        if (err == ESP_OK) err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static char *device_info(const char *device_id, const char *client_id)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const esp_app_desc_t *app = esp_app_get_description();

    cJSON_AddNumberToObject(root, "version", 2);
    cJSON_AddStringToObject(root, "language", "zh-CN");
    cJSON_AddNumberToObject(root, "flash_size", flash_size);
    cJSON_AddNumberToObject(root, "minimum_free_heap_size", esp_get_minimum_free_heap_size());
    cJSON_AddStringToObject(root, "mac_address", device_id);
    cJSON_AddStringToObject(root, "uuid", client_id);
    cJSON_AddStringToObject(root, "chip_model_name", "esp32s3");
    cJSON *chip_json = cJSON_AddObjectToObject(root, "chip_info");
    cJSON_AddNumberToObject(chip_json, "model", chip.model);
    cJSON_AddNumberToObject(chip_json, "cores", chip.cores);
    cJSON_AddNumberToObject(chip_json, "revision", chip.revision);
    cJSON_AddNumberToObject(chip_json, "features", chip.features);
    cJSON *application = cJSON_AddObjectToObject(root, "application");
    cJSON_AddStringToObject(application, "name", app->project_name);
    cJSON_AddStringToObject(application, "version", app->version);
    cJSON_AddStringToObject(application, "idf_version", app->idf_ver);
    cJSON *display = cJSON_AddObjectToObject(root, "display");
    cJSON_AddBoolToObject(display, "monochrome", true);
    cJSON_AddNumberToObject(display, "width", 128);
    cJSON_AddNumberToObject(display, "height", 64);
    cJSON_AddItemToObject(root, "board", cJSON_CreateObject());
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

static esp_err_t post_json(const char *url, const char *json,
                           const char *device_id, const char *client_id,
                           http_body_t *response, int *status_code)
{
    memset(response, 0, sizeof(*response));
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = on_http_event,
        .user_data = response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Activation-Version", "1");
    esp_http_client_set_header(client, "Device-Id", device_id);
    esp_http_client_set_header(client, "Client-Id", client_id);
    esp_http_client_set_header(client, "User-Agent", "oled-imu/1.0.0");
    esp_http_client_set_header(client, "Accept-Language", "zh-CN");
    esp_http_client_set_post_field(client, json, strlen(json));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK && response->overflow) err = ESP_ERR_INVALID_SIZE;
    if (err == ESP_OK) *status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err;
}

esp_err_t xiaozhi_activation_run(xiaozhi_activation_status_cb callback)
{
    if (!callback) return ESP_ERR_INVALID_ARG;
    char device_id[18], client_id[37];
    get_device_id(device_id);
    esp_err_t err = get_client_id(client_id);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "Device ID: %s", device_id);
    char *body = device_info(device_id, client_id);
    http_body_t *response = calloc(1, sizeof(*response));
    if (!body || !response) {
        cJSON_free(body);
        free(response);
        return ESP_ERR_NO_MEM;
    }

    // Do not install a firmware update advertised by the activation endpoint.
    while (true) {
        int status = 0;
        callback(NULL, "XZ CONNECTING");
        err = post_json(XIAOZHI_OTA_URL, body, device_id, client_id, response, &status);
        if (err != ESP_OK || status != 200) {
            ESP_LOGW(TAG, "OTA check failed: %s, HTTP %d", esp_err_to_name(err), status);
            callback(NULL, "XZ SERVER ERROR");
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
        cJSON *root = cJSON_Parse(response->body);
        if (!cJSON_IsObject(root)) {
            callback(NULL, "XZ RESPONSE ERROR");
            ESP_LOGW(TAG, "Invalid OTA JSON response");
            cJSON_Delete(root);
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
        cJSON *activation = cJSON_GetObjectItemCaseSensitive(root, "activation");
        cJSON *code = cJSON_GetObjectItemCaseSensitive(activation, "code");
        cJSON *challenge = cJSON_GetObjectItemCaseSensitive(activation, "challenge");
        if (!cJSON_IsObject(activation)) {
            callback(NULL, "XZ BOUND");
            ESP_LOGI(TAG, "Official Xiaozhi reports no activation required");
            cJSON_Delete(root);
            err = ESP_OK;
            break;
        }
        if (!cJSON_IsString(code) || !code->valuestring ||
            !cJSON_IsString(challenge) || !challenge->valuestring) {
            callback(NULL, "XZ RESPONSE ERROR");
            ESP_LOGW(TAG, "Activation response is missing code or challenge");
            cJSON_Delete(root);
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
        // The official version-1 activation request has an empty JSON body.
        const char *activate_url = XIAOZHI_OTA_URL "activate";
        cJSON *timeout = cJSON_GetObjectItemCaseSensitive(activation, "timeout_ms");
        int timeout_ms = cJSON_IsNumber(timeout) ? timeout->valueint : 30000;
        if (timeout_ms < 3000 || timeout_ms > 300000) timeout_ms = 30000;
        callback(code->valuestring, "XZ ENTER CODE");
        ESP_LOGI(TAG, "Enter Xiaozhi activation code: %s", code->valuestring);
        cJSON_Delete(root);
        bool activated = false;
        for (int elapsed = 0; elapsed < timeout_ms; elapsed += 3000) {
            int activate_status = 0;
            err = post_json(activate_url, "{}", device_id, client_id,
                            response, &activate_status);
            if (err == ESP_OK && activate_status == 200) {
                callback(NULL, "XZ BOUND");
                ESP_LOGI(TAG, "Xiaozhi activation successful");
                activated = true;
                break;
            }
            if (err != ESP_OK || activate_status != 202) {
                ESP_LOGW(TAG, "Activation poll failed: %s, HTTP %d",
                         esp_err_to_name(err), activate_status);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
        if (activated) break;
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    free(response);
    cJSON_free(body);
    return err;
}
