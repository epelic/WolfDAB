#pragma once
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { LOG_ERROR = 0, LOG_WARN, LOG_INFO, LOG_DEBUG } log_level_t;

void log_set_level(log_level_t level);
void log_msg(log_level_t level, const char *fmt, ...);

#define LOGE(...) log_msg(LOG_ERROR, __VA_ARGS__)
#define LOGW(...) log_msg(LOG_WARN,  __VA_ARGS__)
#define LOGI(...) log_msg(LOG_INFO,  __VA_ARGS__)
#define LOGD(...) log_msg(LOG_DEBUG, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
