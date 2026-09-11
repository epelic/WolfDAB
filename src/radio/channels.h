#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *label;      /* e.g. "5A", "11D", "13F" */
    uint64_t    freq_hz;    /* centre frequency */
} dab_channel_t;

/* DAB Band III channels 5A..13F, ETSI EN 300 401 Annex E. */
const dab_channel_t *dab_channels(size_t *count);
const dab_channel_t *dab_channel_find(const char *label);

#ifdef __cplusplus
}
#endif
