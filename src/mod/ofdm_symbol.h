#pragma once
/*
 * DAB Mode I OFDM symbol generator.
 *
 * One Mode I OFDM symbol carries 1536 active carriers (k = -768..-1, 1..768,
 * with k=0 unused) and is transmitted as a 2552-sample complex baseband
 * waveform at 2.048 MS/s:
 *
 *   2048 samples IFFT output (T_u, useful symbol)
 *    504 samples cyclic prefix (copy of the last 504 samples of T_u)
 *
 * Total symbol length Ts = 2552 samples ≈ 1.246 ms.
 *
 * The generator takes 1536 already-mapped complex carriers (whatever the
 * caller provides — QPSK, DQPSK-differentially-encoded, or a fixed phase
 * reference) and produces the time-domain samples. It does NOT do frequency
 * interleaving or DQPSK mapping; those are upstream stages.
 *
 * Carrier-to-IFFT-bin mapping (EN 300 401 §14.5):
 *   carriers[0..767]    → IFFT bins [1..768]        (k = +1..+768)
 *   carriers[768..1535] → IFFT bins [1280..2047]    (k = -768..-1)
 *   IFFT bin 0 (DC)     → 0 + 0j
 *   IFFT bins 769..1279 → 0 + 0j (guard / unused spectrum)
 */

#include <complex.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAB_MODE_I_CARRIERS   1536
#define DAB_MODE_I_IFFT       2048
#define DAB_MODE_I_CP         504
#define DAB_MODE_I_SYMBOL_LEN (DAB_MODE_I_IFFT + DAB_MODE_I_CP)  /* 2552 */

typedef struct ofdm_symbol_ctx ofdm_symbol_ctx_t;

ofdm_symbol_ctx_t *ofdm_symbol_new(void);
void               ofdm_symbol_free(ofdm_symbol_ctx_t *ctx);

/*
 * Build one OFDM symbol.
 *
 *   carriers : 1536 complex values in logical order (+1..+768, -768..-1)
 *   out_iq   : destination, 2552 complex float samples (CP + useful)
 *
 * The IFFT is normalised by 1/sqrt(2048) so that an all-ones input produces
 * unit-magnitude time-domain samples on average.
 */
void ofdm_symbol_build(ofdm_symbol_ctx_t *ctx,
                       const float _Complex *carriers,
                       float _Complex *out_iq);

#ifdef __cplusplus
}
#endif
