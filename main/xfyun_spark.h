#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

bool xfyun_spark_configured(void);
esp_err_t xfyun_spark_ask(const char *question, char *answer, size_t answer_size);
