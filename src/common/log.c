#include "log.h"
#include <stdarg.h>
#ifdef _WIN32
#include <io.h>
#endif

static log_level_t g_level = LOG_INFO;

void log_set_level(log_level_t level) { g_level = level; }

void log_msg(log_level_t level, const char *fmt, ...) {
    if (level > g_level) return;
#ifdef _WIN32
    /* stderr is redirected by the GUI to WolfDAB-TX.log.  Keep long-running
       transmitters from growing that file without limit. */
    const __int64 max_log_bytes = 5LL * 1024LL * 1024LL;
    __int64 pos = _ftelli64(stderr);
    if (pos >= max_log_bytes) {
        fflush(stderr);
        int fd = _fileno(stderr);
        if (fd >= 0 && _chsize_s(fd, 0) == 0) {
            _fseeki64(stderr, 0, SEEK_SET);
            fputs("[I] WolfDAB TX log rotated at 5 MB\n", stderr);
        }
    }
#endif
    static const char *tag[] = { "E", "W", "I", "D" };
    fprintf(stderr, "[%s] ", tag[level]);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
