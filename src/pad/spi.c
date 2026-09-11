#include "spi.h"
#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * SPI XML generation (ETSI TS 102 818 Basic Profile).
 *
 * We generate a schedule document with programme entries from the EPG.
 * The XML is written to a temporary file, then loaded as a MOT object
 * with content type = Application (0x07, subtype 0x00).
 *
 * Qt-dab's epg-compiler.cpp expects binary EPG tokens, not raw XML.
 * However, the MOT transport with content type 0x0700 (Application)
 * triggers the EPG handler which can process both binary and XML SPI.
 *
 * For maximum compatibility, we generate the SPI binary format per
 * ETSI TS 102 371 (binary encoding of SPI). But as a first step,
 * we use the XML text format which some receivers support.
 */

struct spi_enc {
    char      xml[SPI_MAX_XML];
    int       xml_len;
    char      tmp_path[256];
    mot_enc_t *mot;
};

/* Build ISO 8601 date string for today. */
static void iso_date_today(char *buf, size_t cap) {
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    if (lt)
        snprintf(buf, cap, "%04d-%02d-%02d",
                 lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    else
        snprintf(buf, cap, "2026-01-01");
}

/* Build SPI XML from EPG entries. */
static int build_xml(spi_enc_t *e, const epg_t *epg,
                     uint16_t service_id, uint16_t ensemble_id) {
    char date[16];
    iso_date_today(date, sizeof(date));

    int pos = 0;
    pos += snprintf(e->xml + pos, SPI_MAX_XML - (size_t)pos,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<epg xmlns=\"http://www.worlddab.org/schemas/spi/31\">\n"
        "  <schedule version=\"1\" creationTime=\"%sT00:00:00+02:00\"\n"
        "            originator=\"dabtx\">\n"
        "    <scope startTime=\"%sT00:00:00+02:00\"\n"
        "           stopTime=\"%sT23:59:59+02:00\">\n"
        "      <serviceScope id=\"e1.%04x\"/>\n"
        "    </scope>\n",
        date, date, date, service_id);

    for (int i = 0; i < epg->count && pos < SPI_MAX_XML - 200; ++i) {
        const epg_entry_t *entry = &epg->entries[i];
        /* Duration in ISO 8601: PT<hours>H<minutes>M */
        int dur_min = (entry->end_hour * 60 + entry->end_min) -
                      (entry->hour * 60 + entry->minute);
        if (dur_min <= 0) dur_min = 60;
        int dur_h = dur_min / 60;
        int dur_m = dur_min % 60;

        pos += snprintf(e->xml + pos, SPI_MAX_XML - (size_t)pos,
            "    <programme shortId=\"%d\" id=\"crid://dabtx.local/%d\">\n"
            "      <mediumName>%s</mediumName>\n"
            "      <location>\n"
            "        <time time=\"%sT%02d:%02d:00+02:00\" duration=\"PT",
            i + 1, i + 1, entry->title, date, entry->hour, entry->minute);

        if (dur_h > 0)
            pos += snprintf(e->xml + pos, SPI_MAX_XML - (size_t)pos, "%dH", dur_h);
        if (dur_m > 0)
            pos += snprintf(e->xml + pos, SPI_MAX_XML - (size_t)pos, "%dM", dur_m);

        pos += snprintf(e->xml + pos, SPI_MAX_XML - (size_t)pos,
            "\"/>\n"
            "      </location>\n"
            "    </programme>\n");
    }

    pos += snprintf(e->xml + pos, SPI_MAX_XML - (size_t)pos,
        "  </schedule>\n"
        "</epg>\n");

    e->xml_len = pos;
    return 0;
}

spi_enc_t *spi_enc_new(const epg_t *epg, uint16_t service_id,
                       uint16_t ensemble_id) {
    if (!epg || epg->count == 0) return NULL;

    spi_enc_t *e = (spi_enc_t *)calloc(1, sizeof(*e));
    if (!e) return NULL;

    if (build_xml(e, epg, service_id, ensemble_id) != 0) {
        free(e);
        return NULL;
    }

    LOGI("spi: generated %d bytes XML, %d programmes", e->xml_len, epg->count);

    /* Create MOT encoder with SPI content type.
     * Content type = 0x07 (Application), subtype = 0x00.
     * Qt-dab recognizes MOTCTApplication = 0x0700. */
    e->mot = mot_enc_new_raw((const uint8_t *)e->xml, (size_t)e->xml_len,
                             "epg.xml", 0x07, 0x00, SPI_TRANSPORT_ID);
    if (!e->mot) {
        LOGE("spi: MOT encoder creation failed");
        free(e);
        return NULL;
    }

    return e;
}

void spi_enc_free(spi_enc_t *e) {
    if (!e) return;
    if (e->mot) mot_enc_free(e->mot);
    free(e);
}

mot_enc_t *spi_enc_get_mot(spi_enc_t *e) {
    return e ? e->mot : NULL;
}

const char *spi_enc_get_xml(const spi_enc_t *e) {
    return e ? e->xml : NULL;
}
