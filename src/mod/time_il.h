#pragma once
/*
 * DAB MSC Time Interleaver (ETSI EN 300 401 §12).
 *
 * The time interleaver spreads the bits of each input frame across 16
 * successive output frames so that consecutive bits of the same codeword
 * do not end up in the same deep-fade slot. Delays are applied per bit
 * position and depend on whether the byte index within the frame is
 * even or odd:
 *
 *   even-byte bit 7..0 → delays {0, 8, 4, 12, 2,  10, 6,  14}
 *   odd-byte  bit 7..0 → delays {1, 9, 5, 13, 3,  11, 7,  15}
 *
 * (These are the bit-reversed orderings of 0..15 per the spec.)
 *
 * Only the Main Service Channel is interleaved; the Fast Information
 * Channel passes through unchanged, so this module is only used on the
 * subchannel path.
 *
 * Framesize must be an even number of bytes (the delay tables require
 * pairing of consecutive bytes). For our demo config with EEP-3A at
 * 128 kbps the subchannel produces 768 bytes per CIF, which satisfies
 * the constraint.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dab_time_il dab_time_il_t;

/* Allocate a fresh interleaver for `framesize` bytes per frame. Internal
 * history starts zero-filled, so the first 15 output frames are partial.
 * Returns NULL on allocation failure or if framesize is zero or odd. */
dab_time_il_t *dab_time_il_new(size_t framesize);
void           dab_time_il_free(dab_time_il_t *t);

/* Run one frame through the interleaver. Both buffers must hold
 * exactly `framesize` bytes. `in` and `out` may not alias. */
void dab_time_il_process(dab_time_il_t *t,
                         const uint8_t *in,
                         uint8_t *out);

#ifdef __cplusplus
}
#endif
