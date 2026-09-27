#include "pad/spi.h"
#include "pad/epg.h"
#include "pad/mot.h"
#include "common/config.h"
#include "mux/fib.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

int main(void) {
    /* Create EPG config. */
    {
        FILE *f = fopen("test_spi.cfg", "w");
        CHECK(f != NULL, "cannot create test_spi.cfg\n");
        fprintf(f, "epg.1 = 14:00 | 15:00 | QSO Runde 145.5\n");
        fprintf(f, "epg.2 = 15:00 | 16:00 | Contest CQ 432.2\n");
        fclose(f);
    }

    dabtx_cfg_t *cfg = dabtx_cfg_load("test_spi.cfg");
    CHECK(cfg != NULL, "config load failed\n");

    epg_t *epg = epg_load(cfg);
    CHECK(epg != NULL, "epg_load failed\n");

    /* --- Test SPI XML generation --- */
    spi_enc_t *spi = spi_enc_new(epg, 0xE001, 0xE001);
    CHECK(spi != NULL, "spi_enc_new returned NULL\n");

    const char *xml = spi_enc_get_xml(spi);
    CHECK(xml != NULL, "spi_enc_get_xml returned NULL\n");
    CHECK(strlen(xml) > 100, "XML too short: %zu\n", strlen(xml));

    /* Check XML structure. */
    CHECK(strstr(xml, "<?xml") != NULL, "missing XML declaration\n");
    CHECK(strstr(xml, "<epg") != NULL, "missing <epg> element\n");
    CHECK(strstr(xml, "<schedule") != NULL, "missing <schedule>\n");
    CHECK(strstr(xml, "<programme") != NULL, "missing <programme>\n");
    CHECK(strstr(xml, "QSO Runde") != NULL, "missing programme title\n");
    CHECK(strstr(xml, "Contest CQ") != NULL, "missing second programme\n");
    CHECK(strstr(xml, "14:00:00") != NULL, "missing start time\n");
    CHECK(strstr(xml, "duration=\"PT1H\"") != NULL, "missing duration\n");
    CHECK(strstr(xml, "e1.e001") != NULL, "missing serviceScope\n");
    CHECK(strstr(xml, "</epg>") != NULL, "missing </epg>\n");

    printf("SPI XML (%zu bytes):\n%s\n", strlen(xml), xml);

    /* --- Test SPI MOT encoder --- */
    mot_enc_t *mot = spi_enc_get_mot(spi);
    CHECK(mot != NULL, "spi_enc_get_mot returned NULL\n");

    /* Generate X-PAD fields until complete. */
    uint8_t xpad[MOT_XPAD_MAX];
    uint8_t fpad0, fpad1;
    int count = 0;
    while (!mot_enc_complete(mot)) {
        int r = mot_enc_get_xpad(mot, xpad, sizeof(xpad), &fpad0, &fpad1);
        CHECK(r > 0, "SPI MOT xpad returned 0 before complete\n");
        ++count;
        CHECK(count < 2000, "too many SPI MOT fields\n");
    }
    printf("SPI MOT: %d X-PAD fields\n", count);
    CHECK(count > 5, "expected more than 5 SPI MOT fields\n");

    /* --- Test FIG 0/13 --- */
    fib_t fib;
    fib_reset(&fib);
    fig0_13_app_t f13 = {
        .service_id  = 0xE001,
        .sc_ids      = 0,
        .ua_type     = 0x007,
        .ua_data     = { 0x01 },
        .ua_data_len = 1,
    };
    CHECK(fig0_13_write(&fib, &f13) == 0, "fig0_13_write failed\n");
    CHECK(fib.used == 8, "FIG 0/13 size should be 8, got %zu\n", fib.used);

    /* Verify FIG 0/13 bytes. */
    uint8_t *p = fib.bytes;
    CHECK((p[0] >> 5) == 0, "FIG type should be 0\n");
    CHECK((p[0] & 0x1F) == 7, "FIG length should be 7, got %d\n", p[0] & 0x1F);
    CHECK((p[1] & 0x1F) == 13, "FIG ext should be 13\n");
    CHECK(p[2] == 0xE0 && p[3] == 0x01, "SId should be E001\n");
    CHECK((p[4] >> 4) == 0, "SCIdS should be 0\n");
    CHECK((p[4] & 0x0F) == 1, "No should be 1\n");
    /* UAType = 0x007 → high 8 = 0x00, low 3 = 0x07 → byte[5]=0x00, byte[6]=(7<<5)|1=0xE1 */
    CHECK(p[5] == 0x00, "UAType high should be 0x00, got 0x%02x\n", p[5]);
    CHECK(p[6] == 0xE1, "UAType_lo|len should be 0xE1, got 0x%02x\n", p[6]);
    CHECK(p[7] == 0x01, "SPI profile byte should be 0x01\n");

    fib_reset(&fib);
    fig0_13_app_t slideshow = {
        .service_id = 0x43E7, .sc_ids = 7, .ua_type = 0x002,
        .ua_data = { 0x0C, 0x3C }, .ua_data_len = 2,
    };
    CHECK(fig0_13_write(&fib, &slideshow) == 0, "SlideShow FIG 0/13 failed\n");
    CHECK(fib.used == 9, "SlideShow FIG 0/13 size should be 9, got %zu\n", fib.used);
    CHECK(fib.bytes[2] == 0x43 && fib.bytes[3] == 0xE7, "SlideShow SId mismatch\n");
    CHECK(fib.bytes[4] == 0x71, "SlideShow SCIdS/No mismatch\n");
    CHECK(fib.bytes[5] == 0x00 && fib.bytes[6] == 0x42, "SlideShow UA type/length mismatch\n");
    CHECK(fib.bytes[7] == 0x0C && fib.bytes[8] == 0x3C, "SlideShow X-PAD signalling mismatch\n");

    printf("FIG 0/13: ");
    for (int i = 0; i < 8; ++i) printf("%02x ", p[i]);
    printf("\n");

    spi_enc_free(spi);
    epg_free(epg);
    dabtx_cfg_free(cfg);
    remove("test_spi.cfg");

    printf("test_spi: PASS\n");
    return 0;
}
