#pragma once
/*
 * DAB Main Service Channel subchannel codec (ETSI EN 300 401 §11, §12).
 *
 * Per CIF (24 ms) a subchannel is encoded through
 *   1. PRBS9 energy dispersal (x^9 + x^5 + 1, reset to 0x1FF per CIF)
 *   2. mother convolutional encoder (rate 1/4, K = 7)
 *   3. EEP-A level 3 puncturing (rate ≈ 1/2)
 *   4. bit-level time interleaver (16-frame, per-subchannel state)
 *
 * For the demo config (128 kbps EEP-3A) the per-CIF sizes are
 *   in  = 384 bytes   (= bitrate_kbps * 3)
 *   out = 768 bytes   (= bitrate_kbps * 6 = 96 CUs × 64 bits)
 *
 * The encoded subchannel is placed into a 6912-byte CIF buffer at the
 * byte offset start_address × 8; untouched CUs are zero-padded. Four
 * CIFs of 6912 bytes each are concatenated to form the 27648-byte MSC
 * payload that dab_frame_build consumes.
 *
 * NOTE: the time interleaver has 15 frames of warm-up; steady-state
 * output only appears from CIF 15 onward. This is expected and the
 * receiver tolerates it.
 */

#include <stddef.h>
#include <stdint.h>
#include "mux/eep_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DAB_MSC_CIF_BYTES        6912u
#define DAB_MSC_CIFS_PER_FRAME   4u
#define DAB_MSC_FRAME_BYTES      27648u   /* 4 × 6912 */

typedef struct dab_msc_subch dab_msc_subch_t;

/* Allocate an EEP-3A subchannel context.
 *   bitrate_kbps      : multiple of 8 in [32, 384]
 *   start_address_cu  : subchannel start in Capacity Units (64 bits = 8 bytes)
 * Returns NULL on invalid bitrate or allocation failure. */
dab_msc_subch_t *dab_msc_subch_new(int bitrate_kbps, unsigned start_address_cu,
                                   dab_eep_profile_t profile);
void             dab_msc_subch_free(dab_msc_subch_t *ctx);

/* Geometry accessors. */
size_t   dab_msc_subch_in_bytes (const dab_msc_subch_t *ctx); /* bitrate * 3 */
size_t   dab_msc_subch_out_bytes(const dab_msc_subch_t *ctx); /* bitrate * 6 */
unsigned dab_msc_subch_start_byte(const dab_msc_subch_t *ctx);/* start_addr*8 */

/* Encode exactly one CIF of one subchannel. `in` has in_bytes bytes,
 * `out` has out_bytes bytes. Advances the internal time interleaver. */
void dab_msc_subch_encode_cif(dab_msc_subch_t *ctx,
                              const uint8_t *in, uint8_t *out);

/* Convenience: encode the 4 CIFs of one RF frame for a single subchannel
 * and lay them out in the 27648-byte MSC frame buffer. Each CIF is zeroed
 * then the subchannel is written at its start_address × 8 byte offset. */
void dab_msc_frame_build_single(dab_msc_subch_t *ctx,
                                const uint8_t *in_frame,
                                uint8_t *out_frame);

/*
 * Encode 4 CIFs for N subchannels into the 27648-byte MSC frame buffer.
 * The frame is zeroed once, then each subchannel is encoded and placed at
 * its own CU offset. Subchannel CU ranges must not overlap.
 *
 *   subch[i]    : subchannel contexts (each with its own start_address, bitrate)
 *   in_frames[i]: per-subchannel input, 4 CIFs of in_bytes each
 *   n           : number of subchannels
 *   out_frame   : 27648-byte output buffer
 */
void dab_msc_frame_build_multi(dab_msc_subch_t **subch,
                               const uint8_t *const *in_frames,
                               int n,
                               uint8_t *out_frame);

#ifdef __cplusplus
}
#endif
