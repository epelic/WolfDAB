#pragma once
/*
 * DLS (Dynamic Label Segment) PAD encoder for DAB+.
 *
 * Generates PAD frames (F-PAD + X-PAD) containing DLS Data Groups
 * ready to be fed to fdk-aac as ancillary data (IN_ANCILLRY_DATA).
 *
 * The DLS text is split into segments of max 16 characters. Each segment
 * becomes a Data Group with 2-byte prefix + text + 2-byte CRC. The PAD
 * encoder cycles through segments continuously so late-tuning receivers
 * can assemble the full label.
 *
 * Reference: ETSI EN 300 401 §7.4 (PAD), ETSI TS 102 563 §5.4 (DAB+ PAD).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum DLS text length (bytes). */
#define DLS_MAX_TEXT  128
/* Maximum segments (128/16 = 8). */
#define DLS_MAX_SEGS 8
/* Max Data Group size: 2 prefix + 16 text + 2 CRC = 20 bytes. */
#define DLS_MAX_DG   20

typedef struct dls_enc dls_enc_t;

/* Create a DLS encoder with the given text.
 * charset: 0 = EBU Latin (default, most compatible), 15 = UTF-8. */
dls_enc_t *dls_enc_new(const char *text, int charset);
void       dls_enc_free(dls_enc_t *e);

/* Change the DLS text (toggles the toggle bit for receivers). */
void dls_enc_set_text(dls_enc_t *e, const char *text);

/*
 * Get the next X-PAD data for superframe injection.
 *
 * Writes X-PAD bytes (without F-PAD) into `xpad_out`.
 * Sets *fpad0 and *fpad1 to the F-PAD byte values.
 * Returns the number of X-PAD bytes written.
 *
 * The X-PAD bytes are in reversed order (first logical byte at
 * highest index, per DAB+ convention). The caller should inject
 * these via pad_inject() into the superframe body.
 *
 * xpad_cap must be >= DLS_XPAD_MAX.
 *
 * Each call advances to the next segment.
 */
int dls_enc_get_xpad(dls_enc_t *e,
                     uint8_t *xpad_out, size_t xpad_cap,
                     uint8_t *fpad0, uint8_t *fpad1);

/* Maximum X-PAD size this encoder can produce. */
#define DLS_XPAD_MAX 48

#ifdef __cplusplus
}
#endif
