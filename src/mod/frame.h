#pragma once
/*
 * DAB Mode I frame assembler.
 *
 * Builds one complete 96 ms transmission frame at 2.048 MS/s:
 *
 *   null symbol (2656 samples, all-zero complex baseband)
 * + phase reference symbol (2552 samples, fixed per EN 300 401 Table 44)
 * + 3 FIC OFDM symbols (2552 samples each)
 * + 72 MSC OFDM symbols (2552 samples each)
 * = 2656 + 75 * 2552 = 196608 samples
 *
 * Each OFDM symbol takes 384 bytes of input: 192 I-bits followed by 192
 * Q-bits (same layout as dqpsk.h). The assembler
 *   1. maps bits → 1536 base QPSK carriers in logical order,
 *   2. frequency-interleaves the base carriers into physical order,
 *   3. multiplies with the previous symbol's physical carriers (PRS for
 *      the first FIC symbol),
 *   4. runs 2048-pt IFFT + cyclic prefix via ofdm_symbol_build.
 */

#include <complex.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAB_MODE_I_FRAME_SAMPLES  196608
#define DAB_MODE_I_NULL_SAMPLES   2656
#define DAB_MODE_I_SYMBOL_SAMPLES 2552      /* same as DAB_MODE_I_SYMBOL_LEN */
#define DAB_MODE_I_SYMBOLS_DATA   75        /* PRS + 3 FIC + 72 MSC — 76 total,
                                               but the null symbol is not an
                                               OFDM symbol. */
#define DAB_MODE_I_FIC_SYMBOLS    3
#define DAB_MODE_I_MSC_SYMBOLS    72
#define DAB_MODE_I_SYMBOL_BITS_BYTES 384    /* 192 I + 192 Q */

typedef struct dab_frame_ctx dab_frame_ctx_t;

dab_frame_ctx_t *dab_frame_new(void);
void             dab_frame_free(dab_frame_ctx_t *ctx);

/* Assemble one Mode I frame.
 *
 *   fic_bits_frame : 3 * 384 = 1152 bytes — one OFDM-symbol worth per
 *                    FIC symbol, already energy-dispersed + conv-encoded
 *                    + punctured to the FIC profile, in the 192 I + 192 Q
 *                    layout per symbol.
 *   msc_bits_frame : 72 * 384 = 27648 bytes — same layout for MSC.
 *   out            : 196608 complex float samples (caller-allocated).
 *
 * The call is stateless across frames: each call re-seeds the differential
 * modulator from the PRS. This is conservative but costs only one OFDM
 * symbol's worth of drift at frame boundaries, which is absorbed by the
 * receiver since the PRS reappears every frame anyway.
 */
void dab_frame_build(dab_frame_ctx_t *ctx,
                     const uint8_t *fic_bits_frame,
                     const uint8_t *msc_bits_frame,
                     float _Complex *out);

#ifdef __cplusplus
}
#endif
