#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MOT SlideShow encoder for DAB+ X-PAD delivery.
 *
 * Reads a JPEG or PNG image file, builds MOT header + body segments,
 * wraps them in MSC Data Groups (ETSI EN 300 401 section 5.3.3.1),
 * and emits X-PAD fields one per call to mot_enc_get_xpad().
 *
 * Transmission order per data group:
 *   1. AppType 1  — data group length indicator (4-byte subfield)
 *   2. AppType 12 — first chunk of MSC data group
 *   3. AppType 13 — continuation chunks (repeated until DG complete)
 *
 * Data groups: DG 0 = MOT header (groupType 3), DG 1..N = body (groupType 4).
 *
 * Compatible with qt-dab's pad-handler.cpp and mot-object.cpp.
 */

#define MOT_XPAD_MAX 50  /* 48-byte subfield + CI byte + end marker */

typedef struct mot_enc mot_enc_t;

/* Create MOT encoder from an image file (JPEG or PNG).
 * content_name is the filename shown to the receiver (e.g. "logo.jpg").
 * transport_id is a nonzero 16-bit identifier for this object.
 * Returns NULL on error. Max image size: 32 KB. */
mot_enc_t *mot_enc_new(const char *image_path, const char *content_name,
                       uint16_t transport_id);

/* Create MOT encoder from raw data with explicit content type.
 * content_type: 6-bit MOT content type (e.g. 0x07 = Application)
 * content_subtype: 9-bit MOT content subtype (e.g. 0x00)
 * Max data size: 32 KB. */
mot_enc_t *mot_enc_new_raw(const uint8_t *data, size_t data_len,
                           const char *content_name,
                           int content_type, int content_subtype,
                           uint16_t transport_id);

void       mot_enc_free(mot_enc_t *e);

/* Get the next X-PAD field for MOT transmission.
 * Returns X-PAD byte count (>0), or 0 if the full object has been sent.
 * Sets *fpad0, *fpad1 for F-PAD values (Variable Size X-PAD with CI). */
int mot_enc_get_xpad(mot_enc_t *e,
                     uint8_t *xpad_out, size_t xpad_cap,
                     uint8_t *fpad0, uint8_t *fpad1);

/* Nonzero if the full MOT object has been sent at least once. */
int mot_enc_complete(const mot_enc_t *e);

/* Restart transmission from the beginning. */
void mot_enc_restart(mot_enc_t *e);

#ifdef __cplusplus
}
#endif
