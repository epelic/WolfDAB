/* Release-safe: asserts would compile out with NDEBUG. */
#include "radio/channels.h"
#include <stdio.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

int main(void) {
    size_t n = 0;
    const dab_channel_t *ch = dab_channels(&n);
    CHECK(n >= 38, "expected >=38 channels, got %zu\n", n);
    CHECK(strcmp(ch[0].label, "5A") == 0, "first channel label\n");
    CHECK(ch[0].freq_hz == 174928000ULL, "5A freq\n");

    const dab_channel_t *c11d = dab_channel_find("11D");
    CHECK(c11d && c11d->freq_hz == 222064000ULL, "11D freq\n");
    CHECK(dab_channel_find("ZZ") == NULL, "ZZ should not resolve\n");

    const dab_channel_t *c13f = dab_channel_find("13F");
    CHECK(c13f && c13f->freq_hz == 239200000ULL, "13F freq\n");

    printf("OK: %zu channels\n", n);
    return 0;
}
