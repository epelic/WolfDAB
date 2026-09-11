#pragma once
#include "common/config.h"
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal EPG for DAB+ — programme schedule delivered via DLS text.
 *
 * Programme entries are loaded from the config file:
 *   epg.1 = 14:00 | 15:00 | QSO Runde 145.500 MHz
 *   epg.2 = 15:00 | 16:30 | Contest CQ auf 432.200
 *   epg.3 = 16:30 | 17:00 | Fieldday Abbau
 *
 * Format: start_time | end_time | title
 *
 * The EPG module generates a DLS string showing the current and next
 * programme item based on local system time.  This works on every DAB
 * receiver without SPI support.
 */

#define EPG_MAX_ENTRIES  32
#define EPG_DLS_MAX     128

typedef struct {
    int      hour, minute;      /* start time (local) */
    int      end_hour, end_min; /* end time (local) */
    char     title[80];
} epg_entry_t;

typedef struct {
    epg_entry_t entries[EPG_MAX_ENTRIES];
    int         count;
    char        dls_buf[EPG_DLS_MAX];
} epg_t;

/* Load programme entries from config keys "epg.1" .. "epg.N".
 * cfg may be NULL (no config → no EPG). */
epg_t *epg_load(const dabtx_cfg_t *cfg);
void   epg_free(epg_t *epg);

/* Generate a DLS string for the current time.
 * Returns pointer to internal buffer (valid until next call).
 * Returns NULL if no EPG data or no current/next programme. */
const char *epg_get_dls(epg_t *epg, time_t now);

#ifdef __cplusplus
}
#endif
