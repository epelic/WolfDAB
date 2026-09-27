#pragma once
/*
 * DAB π/4-DQPSK mapper (ETSI EN 300 401 §14.5/§14.6).
 *
 * Each carrier carries two bits per OFDM data symbol (I and Q). The base
 * QPSK constellation is
 *   z_base = ((1 - 2*bit_I) + j*(1 - 2*bit_Q)) / sqrt(2)
 * giving four points (±1 ± j)/sqrt(2) on the unit circle. The transmitted
 * carrier value is then obtained by differential multiplication:
 *   z_curr[k] = z_prev[k] * z_base[k]
 * where z_prev is the previous OFDM symbol's carrier value (the phase
 * reference symbol for the first data symbol of a frame).
 *
 * Because the PRS lives on the {+1, +j, -1, -j} constellation (angles
 * multiples of π/2) and z_base lives on (±1±j)/sqrt(2) (angles π/4 +
 * multiples of π/2), consecutive data symbols alternate between the two
 * constellations — hence "π/4-DQPSK".
 *
 * Input bit layout (per EN 300 401 §14.5 and ODR-DabMod QpskSymbolMapper):
 * 384 bytes per symbol, split into two halves of 192 bytes each. The
 * first half holds the in-phase bits for carriers 0..1535, MSB first
 * within each byte (so bit 7 of byte 0 is carrier 0). The second half
 * holds the quadrature bits in the same ordering.
 *
 * The carrier ordering matches phase_ref.h / ofdm_symbol.h:
 *   [0..767]    = k = +1..+768
 *   [768..1535] = k = -768..-1
 */

#include <complex.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAB_MODE_I_CARRIERS_IQ_BYTES 384  /* 192 I + 192 Q */

/* Map 3072 input bits (384 bytes = 192 I-block + 192 Q-block, MSB-first)
 * onto 1536 base QPSK carriers (±1 ± j)/sqrt(2). No differential step.
 * The output is in the same logical carrier order as the input bit
 * layout and must be frequency-interleaved before being combined with
 * the previous symbol. */
void dab_qpsk_base_mode1(const uint8_t *iq_bytes,
                         float _Complex *base_out);

/* Legacy one-shot helper kept for the standalone dqpsk test: computes
 *   out[k] = prev[k] * base_k
 * without frequency interleaving. Do NOT use in the frame pipeline. */
void dab_dqpsk_map_mode1(const float _Complex *prev,
                         const uint8_t *iq_bytes,
                         float _Complex *out);

#ifdef __cplusplus
}
#endif
