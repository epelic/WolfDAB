#pragma once
/*
 * DAB frequency interleaver (ETSI EN 300 401 §14.6, Mode I).
 *
 * The interleaver permutes 1536 complex QPSK symbols across the 1536
 * active carriers using a PRBS-like recurrence
 *   perm_{j+1} = (13 * perm_j + 511) mod 2048
 * starting from perm_0 = 0, with perm values falling inside the DC or
 * guard-band skipped. Each valid perm is mapped to a physical carrier
 * index in [0, 1535]:
 *   perm > 1024: physical = perm - 1025   (positive carriers, k = +1..+768)
 *   perm < 1024: physical = perm + 512    (negative carriers, k = -768..-1)
 * which matches the logical carrier layout used by phase_ref.h,
 * dqpsk.h and ofdm_symbol.h ([0..767] = k=+1..+768, [768..1535] = k=-768..-1).
 *
 * indices[j] therefore holds the physical carrier slot where data symbol j
 * must land.
 */

#include <complex.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAB_MODE_I_FI_CARRIERS 1536

/* Fill `indices` (must hold 1536 slots) with the Mode I frequency
 * interleaver permutation. The result is deterministic and position
 * 0..1535 in the output physical carrier space is hit exactly once. */
void dab_freq_il_build_mode1(size_t *indices);

/* Apply a prebuilt permutation to a block of 1536 input symbols:
 *   out[indices[j]] = in[j]  for j = 0..1535
 * `in` and `out` must not alias. */
void dab_freq_il_apply(const size_t *indices,
                       const float _Complex *in,
                       float _Complex *out);

#ifdef __cplusplus
}
#endif
