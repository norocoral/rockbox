#ifndef Y1_LOG_H
#define Y1_LOG_H

#include "settings.h"
#include "liby1.h"

#define y1_log(msg) do { \
    y1_log_set_enabled(global_settings.y1_logging_enabled); \
    if (global_settings.y1_logging_enabled) (y1_log)(msg); \
} while (0)

#define y1_logf(...) do { \
    y1_log_set_enabled(global_settings.y1_logging_enabled); \
    if (global_settings.y1_logging_enabled) (y1_logf)(__VA_ARGS__); \
} while (0)

#endif
