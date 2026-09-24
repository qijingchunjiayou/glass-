#pragma once

// Create xfyun_spark_local.h with the credentials and endpoint from your
// Spark console. This header intentionally contains no secrets.
#if __has_include("xfyun_spark_local.h")
#include "xfyun_spark_local.h"
#else
#define XFYUN_SPARK_APPID ""
#define XFYUN_SPARK_API_KEY ""
#define XFYUN_SPARK_API_SECRET ""
#define XFYUN_SPARK_HOST ""
#define XFYUN_SPARK_PATH ""
#define XFYUN_SPARK_DOMAIN ""
#endif
