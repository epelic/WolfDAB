#pragma once
/*
 * AAC encoder wrapper (libfdk-aac, Opendigitalradio fork).
 *
 * Supports three modes via the DABTX_AAC_MODE_* constants:
 *   - RAW_LC:      AAC-LC, TT_MP4_RAW output (individual AUs, legacy)
 *   - DABPLUS_LC:  AAC-LC via TT_DABPLUS (encoder emits full superframe body)
 *   - DABPLUS_HEv2: HE-AAC v2 (SBR+PS) via TT_DABPLUS (best quality at low bitrates)
 *
 * In DABPLUS modes, the encoder internally assembles the 120 ms
 * superframe (Firecode, AU headers, AU CRCs). The caller only needs
 * to append RS(120,110) parity via dabplus_sf_from_raw().
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DABTX_AAC_SR          48000
#define DABTX_AAC_CHANNELS    2
#define DABTX_AAC_GRANULE     960    /* PCM samples per channel per AU */

/* Encoder modes */
#define DABTX_AAC_MODE_RAW_LC       0  /* AAC-LC, TT_MP4_RAW, 6 AUs/SF */
#define DABTX_AAC_MODE_DABPLUS_LC   1  /* AAC-LC, TT_DABPLUS, 6 calls/SF */
#define DABTX_AAC_MODE_DABPLUS_SBR  2  /* HE-AAC v1 (SBR), TT_DABPLUS, 3 calls/SF */
#define DABTX_AAC_MODE_DABPLUS_PS   3  /* HE-AAC v2 (SBR+PS), TT_DABPLUS */

typedef struct aac_enc aac_enc_t;

/*
 * Open the encoder.
 *   mode        : one of DABTX_AAC_MODE_*
 *   bitrate_bps : for RAW_LC this is the AAC bitrate;
 *                 for DABPLUS modes this is the subchannel bitrate
 *                 (subchannel_index * 8000, e.g. 64000 for 64 kbps).
 */
aac_enc_t *aac_enc_open(int mode, int bitrate_bps);
aac_enc_t *aac_enc_open_ex(int mode, int bitrate_bps, int sample_rate);
void       aac_enc_close(aac_enc_t *e);

/* How many aac_enc_frame() calls produce one superframe output.
 * RAW_LC / DABPLUS_LC: 6.  DABPLUS_HEv2: 3. */
int aac_enc_calls_per_sf(const aac_enc_t *e);

/* True if the encoder emits complete superframe bodies (TT_DABPLUS). */
int aac_enc_is_dabplus(const aac_enc_t *e);

/*
 * Encode one granule (960 PCM samples × 2 channels = 1920 int16_t).
 *
 * In RAW_LC mode, each call may produce one raw AAC AU in `out`.
 * In DABPLUS modes, output arrives every `calls_per_sf` calls as a
 * complete superframe body (110 * R bytes, where R = bitrate/8000).
 *
 *   pcm_in   : DABTX_AAC_GRANULE * DABTX_AAC_CHANNELS int16 samples
 *   out      : destination buffer
 *   out_cap  : capacity of `out` in bytes
 *   out_len  : receives the number of bytes written (0 while accumulating)
 *   anc_data : ancillary data (PAD) to embed, or NULL for none
 *   anc_len  : number of ancillary bytes (0 if no PAD)
 * Returns 0 on success, negative on error.
 */
int aac_enc_frame(aac_enc_t *e,
                  const int16_t *pcm_in,
                  uint8_t *out, size_t out_cap, size_t *out_len,
                  const uint8_t *anc_data, size_t anc_len);

#ifdef __cplusplus
}
#endif
