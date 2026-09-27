#include "mod/dqpsk.h"

#include <math.h>

#define CARRIERS    1536
#define HALF_BYTES  (CARRIERS / 8)  /* 192 */

void dab_qpsk_base_mode1(const uint8_t *iq_bytes,
                         float _Complex *base_out) {
    const float v = (float)(1.0 / 1.41421356237309504880);
    const uint8_t *i_bytes = iq_bytes;
    const uint8_t *q_bytes = iq_bytes + HALF_BYTES;

    for (size_t k = 0; k < CARRIERS; ++k) {
        unsigned shift = 7u - (unsigned)(k & 7u);
        unsigned bit_i = (i_bytes[k >> 3] >> shift) & 1u;
        unsigned bit_q = (q_bytes[k >> 3] >> shift) & 1u;
        float re = bit_i ? -v : v;
        float im = bit_q ? -v : v;
        base_out[k] = re + im * (float _Complex)I;
    }
}

void dab_dqpsk_map_mode1(const float _Complex *prev,
                         const uint8_t *iq_bytes,
                         float _Complex *out) {
    float _Complex base[CARRIERS];
    dab_qpsk_base_mode1(iq_bytes, base);
    for (size_t k = 0; k < CARRIERS; ++k) {
        out[k] = prev[k] * base[k];
    }
}
