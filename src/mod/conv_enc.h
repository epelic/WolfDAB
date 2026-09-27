#pragma once
/*
 * DAB mother convolutional encoder (ETSI EN 300 401 §11.1).
 *
 * Rate 1/4, constraint length K = 7 (6 memory stages), native generators
 *   G1 = 0133 oct = 0x5b = 1 + D   + D^3 + D^4 + D^6
 *   G2 = 0171 oct = 0x79 = 1 + D^2 + D^3 + D^5 + D^6
 *   G3 = 0145 oct = 0x65 = 1 + D   + D^2 + D^5 + D^6
 *   G4 = 0133 oct = 0x5b = 1 + D   + D^3 + D^4 + D^6
 *
 * After the last information bit, six zero tail bits are shifted in to
 * force the encoder back to the all-zero state, producing 6 * 4 = 24
 * additional output bits.
 *
 * Bit order: both input and output are byte-packed, MSB first, matching
 * the on-the-wire bit ordering used throughout DAB.
 *
 * Sizes: n bytes in → 4*n + 3 bytes out
 *        (equivalently 8*n bits in → 32*n + 24 bits out).
 *
 * The output layout per input bit is (g1, g2, g3, g4). Four input bits
 * therefore fill one output byte MSB-first as
 *   [g1_0 g2_0 g3_0 g4_0 g1_1 g2_1 g3_1 g4_1]... — i.e. two input bits
 * produce one output byte.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void dab_conv_enc_mother(const uint8_t *in, size_t n_in_bytes, uint8_t *out);

#define DAB_CONV_OUT_BYTES(n_in_bytes) (((size_t)(n_in_bytes)) * 4u + 3u)

#ifdef __cplusplus
}
#endif
