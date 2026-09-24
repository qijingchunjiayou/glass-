#pragma once

#include "esp_err.h"

typedef void (*xiaozhi_activation_status_cb)(const char *code, const char *status);

// Blocks until activation succeeds; reports transient failures through status.
esp_err_t xiaozhi_activation_run(xiaozhi_activation_status_cb callback);
