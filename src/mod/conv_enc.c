#include "mod/conv_enc.h"

/*
 * Implementation notes
 * --------------------
 * The encoder keeps a 7-bit sliding window `memory` holding the current
 * input bit in bit 6 (MSB within the 7-bit window) and the six previous
 * bits in bits 5..0. On each clock the window shifts right and the new
 * bit enters at bit 6. The four output bits are the even parity of
 * `memory` masked with each generator.
 *
 * This mirrors ODR-DabMod's ConvEncoder::process byte-for-byte so the
 * output stream is bit-identical to the reference modulator (vendored
 * at third_party/ODR-DabMod for cross-checking only).
 */

static inline unsigned parity7(unsigned x) {
    return (unsigned)__builtin_parity(x & 0x7fu);
}

void dab_conv_enc_mother(const uint8_t *in, size_t n_in_bytes, uint8_t *out) {
    uint16_t memory = 0;
    size_t out_idx = 0;

    for (size_t i = 0; i < n_in_bytes; ++i) {
        uint8_t data = in[i];
        for (unsigned oc = 0; oc < 4; ++oc) {
            uint8_t o = 0;
            for (unsigned j = 0; j < 2; ++j) {
                memory >>= 1;
                memory |= (uint16_t)(((data >> 7) & 1u) << 6);
                data = (uint8_t)(data << 1);
                o = (uint8_t)((o << 1) | parity7(memory & 0x5bu));
                o = (uint8_t)((o << 1) | parity7(memory & 0x79u));
                o = (uint8_t)((o << 1) | parity7(memory & 0x65u));
                o = (uint8_t)((o << 1) | parity7(memory & 0x5bu));
            }
            out[out_idx++] = o;
        }
    }

    /* Six zero tail bits → three output bytes. */
    for (unsigned pad = 0; pad < 3; ++pad) {
        uint8_t o = 0;
        for (unsigned j = 0; j < 2; ++j) {
            memory >>= 1;
            o = (uint8_t)((o << 1) | parity7(memory & 0x5bu));
            o = (uint8_t)((o << 1) | parity7(memory & 0x79u));
            o = (uint8_t)((o << 1) | parity7(memory & 0x65u));
            o = (uint8_t)((o << 1) | parity7(memory & 0x5bu));
        }
        out[out_idx++] = o;
    }
}
