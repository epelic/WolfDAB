#include "epg.h"
#include "common/config.h"
#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*
 * Parse one EPG entry: "HH:MM | HH:MM | Title text"
 * Returns 0 on success.
 */
static int parse_entry(const char *val, epg_entry_t *out) {
    /* Find first pipe. */
    const char *p1 = strchr(val, '|');
    if (!p1) return -1;
    /* Find second pipe. */
    const char *p2 = strchr(p1 + 1, '|');
    if (!p2) return -1;

    /* Parse start time. */
    if (sscanf(val, "%d:%d", &out->hour, &out->minute) != 2) return -1;
    if (out->hour < 0 || out->hour > 23 || out->minute < 0 || out->minute > 59)
        return -1;

    /* Parse end time. */
    if (sscanf(p1 + 1, "%d:%d", &out->end_hour, &out->end_min) != 2) return -1;
    if (out->end_hour < 0 || out->end_hour > 23 || out->end_min < 0 || out->end_min > 59)
        return -1;

    /* Title: skip whitespace after second pipe. */
    const char *t = p2 + 1;
    while (*t && isspace((unsigned char)*t)) ++t;
    size_t len = strlen(t);
    /* Trim trailing whitespace. */
    while (len > 0 && isspace((unsigned char)t[len - 1])) --len;
    if (len == 0 || len >= sizeof(out->title)) return -1;
    memcpy(out->title, t, len);
    out->title[len] = '\0';
    return 0;
}

/* Compare entries by start time for qsort. */
static int cmp_entry(const void *a, const void *b) {
    const epg_entry_t *ea = (const epg_entry_t *)a;
    const epg_entry_t *eb = (const epg_entry_t *)b;
    int ta = ea->hour * 60 + ea->minute;
    int tb = eb->hour * 60 + eb->minute;
    return ta - tb;
}

epg_t *epg_load(const struct dabtx_cfg *cfg) {
    if (!cfg) return NULL;

    epg_t *epg = (epg_t *)calloc(1, sizeof(*epg));
    if (!epg) return NULL;

    char key[16];
    for (int i = 1; i <= EPG_MAX_ENTRIES; ++i) {
        snprintf(key, sizeof(key), "epg.%d", i);
        const char *val = dabtx_cfg_get(cfg, key);
        if (!val) continue;

        epg_entry_t e;
        memset(&e, 0, sizeof(e));
        if (parse_entry(val, &e) != 0) {
            LOGE("epg: cannot parse '%s': %s", key, val);
            continue;
        }
        if (epg->count < EPG_MAX_ENTRIES)
            epg->entries[epg->count++] = e;
    }

    if (epg->count == 0) {
        free(epg);
        return NULL;
    }

    /* Sort by start time. */
    qsort(epg->entries, (size_t)epg->count, sizeof(epg_entry_t), cmp_entry);

    LOGI("epg: loaded %d programme entries", epg->count);
    for (int i = 0; i < epg->count; ++i)
        LOGI("  %02d:%02d-%02d:%02d %s",
             epg->entries[i].hour, epg->entries[i].minute,
             epg->entries[i].end_hour, epg->entries[i].end_min,
             epg->entries[i].title);

    return epg;
}

void epg_free(epg_t *epg) {
    free(epg);
}

const char *epg_get_dls(epg_t *epg, time_t now) {
    if (!epg || epg->count == 0) return NULL;

    struct tm *lt = localtime(&now);
    if (!lt) return NULL;
    int cur_min = lt->tm_hour * 60 + lt->tm_min;

    /* Find current programme (start <= now < end). */
    const epg_entry_t *current = NULL;
    const epg_entry_t *next = NULL;

    for (int i = 0; i < epg->count; ++i) {
        int start = epg->entries[i].hour * 60 + epg->entries[i].minute;
        int end   = epg->entries[i].end_hour * 60 + epg->entries[i].end_min;
        if (cur_min >= start && cur_min < end) {
            current = &epg->entries[i];
            /* Next is the following entry if it exists. */
            if (i + 1 < epg->count)
                next = &epg->entries[i + 1];
            break;
        }
    }

    /* If no current programme, find the next upcoming one. */
    if (!current) {
        for (int i = 0; i < epg->count; ++i) {
            int start = epg->entries[i].hour * 60 + epg->entries[i].minute;
            if (start > cur_min) {
                next = &epg->entries[i];
                break;
            }
        }
    }

    if (!current && !next) return NULL;

    /* Build DLS string. */
    int pos = 0;
    if (current) {
        pos += snprintf(epg->dls_buf + pos, EPG_DLS_MAX - (size_t)pos,
                        "JETZT %02d:%02d: %s",
                        current->hour, current->minute, current->title);
    }
    if (next && pos < EPG_DLS_MAX - 1) {
        if (pos > 0)
            pos += snprintf(epg->dls_buf + pos, EPG_DLS_MAX - (size_t)pos,
                            " /// ");
        pos += snprintf(epg->dls_buf + pos, EPG_DLS_MAX - (size_t)pos,
                        "DANACH %02d:%02d: %s",
                        next->hour, next->minute, next->title);
    }

    return epg->dls_buf;
}
