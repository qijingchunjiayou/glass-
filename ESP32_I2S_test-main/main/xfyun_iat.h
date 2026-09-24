/**
 * @file xfyun_iat.h
 * @brief iFlytek IAT (Intelligent Audio Transcription) Module
 *
 * Implements iFlytek's streaming speech-to-text WebSocket API.
 * Captures audio from INMP441 microphone, sends to iFlytek cloud,
 * and receives transcription results.
 */

#ifndef __XFYUN_IAT_H__
#define __XFYUN_IAT_H__

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Credentials for this IAT application are kept in an untracked local header. */
#if __has_include("xfyun_iat_local.h")
#include "xfyun_iat_local.h"
#else
#define XFYUN_APPID        ""
#define XFYUN_API_KEY      ""
#define XFYUN_API_SECRET   ""
#endif

/* IAT API endpoint */
#define XFYUN_IAT_HOST     "iat-api.xfyun.cn"
#define XFYUN_IAT_PATH     "/v2/iat"

/* Audio frame configuration for iFlytek */
#define IAT_FRAME_SIZE      1280    /* Bytes per frame (40ms at 16kHz 16bit mono) */
#define IAT_SEND_INTERVAL   40      /* ms between frames */
#define IAT_RECORD_SECONDS  5       /* Default recording duration */

/* Frame status values */
#define IAT_STATUS_FIRST    0
#define IAT_STATUS_CONTINUE 1
#define IAT_STATUS_LAST     2

/**
 * @brief Start a speech recognition session
 *
 * Connects to iFlytek WebSocket, captures audio from INMP441,
 * sends audio frames, and prints recognized text.
 *
 * @param record_seconds Duration to record (1-60 seconds)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t xfyun_iat_recognize(int record_seconds);

#ifdef __cplusplus
}
#endif

#endif /* __XFYUN_IAT_H__ */
