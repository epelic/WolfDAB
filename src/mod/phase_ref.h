#pragma once
/*
 * DAB Phase Reference Symbol generator (ETSI EN 300 401 §14.3.2).
 *
 * The phase reference symbol (PRS) is a fixed, standard-defined OFDM
 * symbol transmitted once per frame, right after the Null symbol. It
 * provides the absolute phase anchor for the differential modulation of
 * the data symbols that follow.
 *
 * For each carrier k the PRS takes one of four QPSK values
 *   z_k = j^phi_k, phi_k in {0, 1, 2, 3}
 * with phi_k = (h[i][k-k'] + n) mod 4 where (i, k', n) are given by
 * Table 44 (Mode I) as functions of the carrier index.
 *
 * This module fills a flat 1536-carrier buffer in the logical carrier
 * order expected by ofdm_symbol_build():
 *   out[0..767]    = k = +1..+768
 *   out[768..1535] = k = -768..-1
 */

#include <complex.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAB_MODE_I_PRS_CARRIERS 1536

/* Write the Mode I phase reference symbol into `out` (1536 complex floats,
 * unit magnitude). `out` is assumed to have space for at least 1536
 * entries. */
void dab_phase_ref_mode1(float _Complex *out);

#ifdef __cplusplus
}
#endif
