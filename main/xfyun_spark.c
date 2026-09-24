#include "xfyun_spark.h"
#include "xfyun_spark_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"

#define CONNECTED_BIT BIT0
#define DONE_BIT BIT1
#define ERROR_BIT BIT2
#define MAX_RESPONSE_BYTES 8192

static const char *TAG = "XFYUN_SPARK";

typedef struct {
    EventGroupHandle_t events;
    char *answer;
    size_t answer_size;
    char *frame;
    size_t frame_size;
} spark_context_t;

bool xfyun_spark_configured(void)
{
    return XFYUN_SPARK_APPID[0] && XFYUN_SPARK_API_KEY[0] &&
           XFYUN_SPARK_API_SECRET[0] && XFYUN_SPARK_HOST[0] &&
           XFYUN_SPARK_PATH[0] == '/' && XFYUN_SPARK_DOMAIN[0];
}

static char *base64(const unsigned char *data, size_t size)
{
    size_t needed = 0;
    mbedtls_base64_encode(NULL, 0, &needed, data, size);
    char *encoded = malloc(needed + 1);
    if (!encoded) return NULL;
    if (mbedtls_base64_encode((unsigned char *)encoded, needed + 1,
                              &needed, data, size) != 0) {
        free(encoded);
        return NULL;
    }
    encoded[needed] = 0;
    return encoded;
}

static char *url_encode(const char *source)
{
    char *encoded = malloc(strlen(source) * 3 + 1);
    if (!encoded) return NULL;
    char *out = encoded;
    for (const unsigned char *p = (const unsigned char *)source; *p; ++p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' ||
            *p == '.' || *p == '~') *out++ = *p;
        else out += sprintf(out, "%%%02X", *p);
    }
    *out = 0;
    return encoded;
}

static char *auth_url(void)
{
    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    char date[64];
    strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", &utc);

    size_t size = strlen(XFYUN_SPARK_HOST) + strlen(XFYUN_SPARK_PATH) + sizeof(date) + 32;
    char *origin = malloc(size);
    if (!origin) return NULL;
    snprintf(origin, size, "host: %s\ndate: %s\nGET %s HTTP/1.1",
             XFYUN_SPARK_HOST, date, XFYUN_SPARK_PATH);
    unsigned char digest[32];
    int result = mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                                 (const unsigned char *)XFYUN_SPARK_API_SECRET,
                                 strlen(XFYUN_SPARK_API_SECRET),
                                 (const unsigned char *)origin, strlen(origin), digest);
    free(origin);
    if (result != 0) return NULL;
    char *signature = base64(digest, sizeof(digest));
    if (!signature) return NULL;

    size = strlen(XFYUN_SPARK_API_KEY) + strlen(signature) + 100;
    char *auth = malloc(size);
    if (auth) snprintf(auth, size,
                       "api_key=\"%s\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"%s\"",
                       XFYUN_SPARK_API_KEY, signature);
    free(signature);
    if (!auth) return NULL;
    char *auth_b64 = base64((const unsigned char *)auth, strlen(auth));
    free(auth);
    if (!auth_b64) return NULL;
    char *encoded_auth = url_encode(auth_b64);
    free(auth_b64);
    char *encoded_date = url_encode(date);
    char *encoded_host = url_encode(XFYUN_SPARK_HOST);
    if (!encoded_auth || !encoded_date || !encoded_host) {
        free(encoded_auth);
        free(encoded_date);
        free(encoded_host);
        return NULL;
    }
    size = strlen(XFYUN_SPARK_HOST) + strlen(XFYUN_SPARK_PATH) +
           strlen(encoded_auth) + strlen(encoded_date) + strlen(encoded_host) + 64;
    char *url = malloc(size);
    if (url) snprintf(url, size, "wss://%s%s?authorization=%s&date=%s&host=%s",
                      XFYUN_SPARK_HOST, XFYUN_SPARK_PATH,
                      encoded_auth, encoded_date, encoded_host);
    free(encoded_auth);
    free(encoded_date);
    free(encoded_host);
    return url;
}

static char *request_json(const char *question)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON *header = cJSON_AddObjectToObject(root, "header");
    cJSON *parameter = cJSON_AddObjectToObject(root, "parameter");
    cJSON *chat = parameter ? cJSON_AddObjectToObject(parameter, "chat") : NULL;
    cJSON *payload = cJSON_AddObjectToObject(root, "payload");
    cJSON *message = payload ? cJSON_AddObjectToObject(payload, "message") : NULL;
    cJSON *items = message ? cJSON_AddArrayToObject(message, "text") : NULL;
    cJSON *item = items ? cJSON_CreateObject() : NULL;
    if (!header || !chat || !item ||
        !cJSON_AddStringToObject(header, "app_id", XFYUN_SPARK_APPID) ||
        !cJSON_AddStringToObject(chat, "domain", XFYUN_SPARK_DOMAIN) ||
        !cJSON_AddNumberToObject(chat, "temperature", 0.5) ||
        !cJSON_AddNumberToObject(chat, "max_tokens", 512) ||
        !cJSON_AddStringToObject(item, "role", "user") ||
        !cJSON_AddStringToObject(item, "content", question)) {
        cJSON_Delete(item);
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToArray(items, item);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static void parse_response(spark_context_t *ctx, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        xEventGroupSetBits(ctx->events, ERROR_BIT);
        return;
    }
    cJSON *header = cJSON_GetObjectItemCaseSensitive(root, "header");
    cJSON *code = cJSON_GetObjectItemCaseSensitive(header, "code");
    if (!cJSON_IsNumber(code) || code->valueint != 0) {
        ESP_LOGE(TAG, "Spark response code: %d", cJSON_IsNumber(code) ? code->valueint : -1);
        xEventGroupSetBits(ctx->events, ERROR_BIT);
        cJSON_Delete(root);
        return;
    }
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    cJSON *choices = cJSON_GetObjectItemCaseSensitive(payload, "choices");
    cJSON *texts = cJSON_GetObjectItemCaseSensitive(choices, "text");
    if (cJSON_IsArray(texts)) {
        cJSON *part;
        cJSON_ArrayForEach(part, texts) {
            cJSON *content = cJSON_GetObjectItemCaseSensitive(part, "content");
            if (cJSON_IsString(content) && content->valuestring) {
                size_t used = strlen(ctx->answer);
                size_t available = ctx->answer_size - used - 1;
                size_t bytes = strlen(content->valuestring);
                if (bytes > available) {
                    bytes = available;
                    while (bytes && ((unsigned char)content->valuestring[bytes] & 0xc0) == 0x80) --bytes;
                }
                memcpy(ctx->answer + used, content->valuestring, bytes);
                ctx->answer[used + bytes] = 0;
            }
        }
    }
    cJSON *status = cJSON_GetObjectItemCaseSensitive(choices, "status");
    if (cJSON_IsNumber(status) && status->valueint == 2)
        xEventGroupSetBits(ctx->events, DONE_BIT);
    cJSON_Delete(root);
}

static void websocket_event(void *handler_args, esp_event_base_t base,
                            int32_t event_id, void *event_data)
{
    spark_context_t *ctx = handler_args;
    esp_websocket_event_data_t *data = event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        xEventGroupSetBits(ctx->events, CONNECTED_BIT);
        break;
    case WEBSOCKET_EVENT_DATA:
        if (data->op_code != 0x01 && data->payload_offset == 0) break;
        if (data->payload_len <= 0 || data->payload_len > MAX_RESPONSE_BYTES ||
            data->payload_offset < 0 || data->data_len < 0 ||
            data->payload_offset + data->data_len > data->payload_len) {
            xEventGroupSetBits(ctx->events, ERROR_BIT);
            break;
        }
        if (data->payload_offset == 0) {
            free(ctx->frame);
            ctx->frame = malloc(data->payload_len + 1);
            ctx->frame_size = ctx->frame ? data->payload_len : 0;
        }
        if (!ctx->frame || ctx->frame_size != data->payload_len) {
            xEventGroupSetBits(ctx->events, ERROR_BIT);
            break;
        }
        memcpy(ctx->frame + data->payload_offset, data->data_ptr, data->data_len);
        if (data->payload_offset + data->data_len == data->payload_len) {
            ctx->frame[data->payload_len] = 0;
            parse_response(ctx, ctx->frame);
            free(ctx->frame);
            ctx->frame = NULL;
            ctx->frame_size = 0;
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        xEventGroupSetBits(ctx->events, ERROR_BIT);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        if (!(xEventGroupGetBits(ctx->events) & DONE_BIT))
            xEventGroupSetBits(ctx->events, ERROR_BIT);
        break;
    default:
        break;
    }
}

esp_err_t xfyun_spark_ask(const char *question, char *answer, size_t answer_size)
{
    if (!question || !*question || !answer || answer_size < 2) return ESP_ERR_INVALID_ARG;
    answer[0] = 0;
    if (!xfyun_spark_configured()) return ESP_ERR_INVALID_STATE;

    char *url = auth_url();
    char *json = request_json(question);
    if (!url || !json) {
        free(url);
        free(json);
        return ESP_ERR_NO_MEM;
    }
    spark_context_t ctx = { .events = xEventGroupCreate(),
                            .answer = answer, .answer_size = answer_size };
    if (!ctx.events) {
        free(url);
        free(json);
        return ESP_ERR_NO_MEM;
    }
    esp_websocket_client_config_t config = {
        .uri = url,
        .buffer_size = 4096,
        .task_stack = 6144,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .network_timeout_ms = 10000,
    };
    esp_websocket_client_handle_t client = esp_websocket_client_init(&config);
    esp_err_t ret = ESP_FAIL;
    if (client) {
        esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event, &ctx);
        if (esp_websocket_client_start(client) == ESP_OK) {
            EventBits_t bits = xEventGroupWaitBits(ctx.events, CONNECTED_BIT | ERROR_BIT,
                                                    pdFALSE, pdFALSE, pdMS_TO_TICKS(12000));
            if (bits & CONNECTED_BIT && !(bits & ERROR_BIT) &&
                esp_websocket_client_send_text(client, json, strlen(json),
                                               pdMS_TO_TICKS(5000)) >= 0) {
                bits = xEventGroupWaitBits(ctx.events, DONE_BIT | ERROR_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(60000));
                if ((bits & DONE_BIT) && !(bits & ERROR_BIT) && answer[0]) ret = ESP_OK;
                else if (!bits) ret = ESP_ERR_TIMEOUT;
            } else if (!bits) ret = ESP_ERR_TIMEOUT;
            esp_websocket_client_stop(client);
        }
        esp_websocket_client_destroy(client);
    }
    free(ctx.frame);
    vEventGroupDelete(ctx.events);
    free(json);
    free(url);
    return ret;
}
