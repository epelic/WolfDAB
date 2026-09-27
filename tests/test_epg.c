#include "pad/epg.h"
#include "common/config.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

int main(void) {
    /* Write a test config with EPG entries. */
    {
        FILE *f = fopen("test_epg.cfg", "w");
        CHECK(f != NULL, "cannot create test_epg.cfg\n");
        fprintf(f, "epg.1 = 08:00 | 09:00 | Morgenrunde 145.500 MHz\n");
        fprintf(f, "epg.2 = 14:00 | 15:30 | Contest CQ 432.200 MHz\n");
        fprintf(f, "epg.3 = 15:30 | 17:00 | Fieldday Abbau\n");
        fclose(f);
    }

    dabtx_cfg_t *cfg = dabtx_cfg_load("test_epg.cfg");
    CHECK(cfg != NULL, "config load failed\n");

    epg_t *epg = epg_load(cfg);
    CHECK(epg != NULL, "epg_load returned NULL\n");
    CHECK(epg->count == 3, "expected 3 entries, got %d\n", epg->count);

    /* Check sorting. */
    CHECK(epg->entries[0].hour == 8,  "entry 0 should be 08:00\n");
    CHECK(epg->entries[1].hour == 14, "entry 1 should be 14:00\n");
    CHECK(epg->entries[2].hour == 15 && epg->entries[2].minute == 30,
          "entry 2 should be 15:30\n");

    /* Test DLS generation at 14:15 → should show Contest CQ as current,
     * Fieldday Abbau as next. */
    struct tm t14 = {0};
    t14.tm_year = 126;  /* 2026 */
    t14.tm_mon  = 3;    /* April */
    t14.tm_mday = 13;
    t14.tm_hour = 14;
    t14.tm_min  = 15;
    time_t ts14 = mktime(&t14);

    const char *dls = epg_get_dls(epg, ts14);
    CHECK(dls != NULL, "epg_get_dls returned NULL at 14:15\n");
    printf("DLS at 14:15: %s\n", dls);
    CHECK(strstr(dls, "JETZT") != NULL, "should contain JETZT\n");
    CHECK(strstr(dls, "Contest") != NULL, "should contain Contest\n");
    CHECK(strstr(dls, "DANACH") != NULL, "should contain DANACH\n");
    CHECK(strstr(dls, "Fieldday") != NULL, "should contain Fieldday\n");

    /* Test at 07:00 → before all entries, should show next = Morgenrunde. */
    struct tm t07 = t14;
    t07.tm_hour = 7;
    t07.tm_min  = 0;
    time_t ts07 = mktime(&t07);

    dls = epg_get_dls(epg, ts07);
    CHECK(dls != NULL, "epg_get_dls at 07:00 should show next\n");
    printf("DLS at 07:00: %s\n", dls);
    CHECK(strstr(dls, "DANACH") != NULL, "should contain DANACH\n");
    CHECK(strstr(dls, "Morgenrunde") != NULL, "should contain Morgenrunde\n");

    /* Test at 20:00 → after all entries, should return NULL. */
    struct tm t20 = t14;
    t20.tm_hour = 20;
    t20.tm_min  = 0;
    time_t ts20 = mktime(&t20);

    dls = epg_get_dls(epg, ts20);
    CHECK(dls == NULL, "epg_get_dls at 20:00 should return NULL (no programme)\n");

    epg_free(epg);
    dabtx_cfg_free(cfg);
    remove("test_epg.cfg");

    printf("test_epg: PASS\n");
    return 0;
}
