#pragma once
#include "epg.h"
#include "mot.h"
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SPI (Service and Programme Information) encoder for DAB+.
 *
 * Generates an SPI XML document (ETSI TS 102 818 "Basic Profile") from
 * EPG programme entries, wraps it as a MOT object with content type
 * Application (0x07), and delivers it via X-PAD using the same mechanism
 * as MOT SlideShow.
 *
 * The SPI MOT object uses transport_id=2 (SlideShow uses 1).
 * Content type = 0x07 (Application), subtype = 0x00.
 *
 * Qt-dab recognizes MOTCTApplication = 0x0700 and routes it to
 * the EPG compiler (epg-compiler.cpp).
 */

#define SPI_TRANSPORT_ID     2
#define SPI_MAX_XML       4096

typedef struct spi_enc spi_enc_t;

/* Build SPI from EPG entries.
 * service_id: 16-bit DAB service ID (e.g. 0xE001)
 * ensemble_id: 16-bit ensemble ID
 * Returns NULL if epg is NULL or has no entries. */
spi_enc_t *spi_enc_new(const epg_t *epg, uint16_t service_id,
                       uint16_t ensemble_id);
void       spi_enc_free(spi_enc_t *e);

/* Get the underlying MOT encoder (for X-PAD delivery).
 * The caller should call mot_enc_get_xpad() on the returned encoder. */
mot_enc_t *spi_enc_get_mot(spi_enc_t *e);

/* Get the generated XML (for debugging). */
const char *spi_enc_get_xml(const spi_enc_t *e);

#ifdef __cplusplus
}
#endif
