#include "log.h"
#include <stdarg.h>

static log_level_t g_level = LOG_INFO;

void log_set_level(log_level_t level) { g_level = level; }

void log_msg(log_level_t level, const char *fmt, ...) {
    if (level > g_level) return;
    static const char *tag[] = { "E", "W", "I", "D" };
    fprintf(stderr, "[%s] ", tag[level]);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
