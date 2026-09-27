#include "mod/frame.h"
#include "mod/ofdm_symbol.h"

#include <complex.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

int main(void) {
    dab_frame_ctx_t *ctx = dab_frame_new();
    CHECK(ctx, "new\n");

    const size_t fic_bytes = DAB_MODE_I_FIC_SYMBOLS * DAB_MODE_I_SYMBOL_BITS_BYTES;
    const size_t msc_bytes = DAB_MODE_I_MSC_SYMBOLS * DAB_MODE_I_SYMBOL_BITS_BYTES;
    uint8_t *fic = (uint8_t *)calloc(1, fic_bytes);
    uint8_t *msc = (uint8_t *)calloc(1, msc_bytes);
    float _Complex *frame = (float _Complex *)calloc(DAB_MODE_I_FRAME_SAMPLES,
                                                     sizeof(*frame));
    CHECK(fic && msc && frame, "alloc\n");

    /* Case 1: zero-bit input. Null symbol and data symbols should all be
     * finite; null span must be exactly zero. */
    dab_frame_build(ctx, fic, msc, frame);

    for (int i = 0; i < DAB_MODE_I_NULL_SAMPLES; ++i) {
        CHECK(crealf(frame[i]) == 0.0f && cimagf(frame[i]) == 0.0f,
              "null[%d] = %f + %fj\n", i, crealf(frame[i]), cimagf(frame[i]));
    }
    for (int i = 0; i < DAB_MODE_I_FRAME_SAMPLES; ++i) {
        float re = crealf(frame[i]), im = cimagf(frame[i]);
        CHECK(isfinite(re) && isfinite(im),
              "nonfinite at %d: %f+%fj\n", i, re, im);
    }

    /* Case 2: PRS symbol sits immediately after the null. Its useful part
     * should have average power 1536/2048 = 0.75 (all carriers active at
     * unit magnitude, same accounting as test_ofdm_symbol). */
    {
        const int prs_start = DAB_MODE_I_NULL_SAMPLES + DAB_MODE_I_CP;
        const int prs_end   = DAB_MODE_I_NULL_SAMPLES + DAB_MODE_I_SYMBOL_SAMPLES;
        double power = 0.0;
        for (int i = prs_start; i < prs_end; ++i) {
            double r = crealf(frame[i]);
            double m = cimagf(frame[i]);
            power += r * r + m * m;
        }
        double avg = power / DAB_MODE_I_IFFT;
        CHECK(avg > 0.74 && avg < 0.76,
              "PRS useful power %f expected 0.75\n", avg);
    }

    /* Case 3: pseudo-random input still yields a finite, unit-scale frame.
     * Every OFDM data symbol should have useful-part average power near 0.75
     * because every carrier is at unit magnitude (|prev|=|base|=1). */
    uint32_t s = 0xABAD1DEAu;
    for (size_t i = 0; i < fic_bytes; ++i) {
        s = s * 1664525u + 1013904223u;
        fic[i] = (uint8_t)(s >> 16);
    }
    for (size_t i = 0; i < msc_bytes; ++i) {
        s = s * 1664525u + 1013904223u;
        msc[i] = (uint8_t)(s >> 16);
    }
    dab_frame_build(ctx, fic, msc, frame);

    /* Check average useful power for every one of the 76 symbols. */
    int sym_idx = 0;
    int off = DAB_MODE_I_NULL_SAMPLES;
    for (sym_idx = 0; sym_idx < 76; ++sym_idx) {
        const int start = off + DAB_MODE_I_CP;
        const int end   = off + DAB_MODE_I_SYMBOL_SAMPLES;
        double power = 0.0;
        for (int i = start; i < end; ++i) {
            double r = crealf(frame[i]);
            double m = cimagf(frame[i]);
            power += r * r + m * m;
        }
        double avg = power / DAB_MODE_I_IFFT;
        CHECK(avg > 0.74 && avg < 0.76,
              "symbol %d avg power %f\n", sym_idx, avg);
        off += DAB_MODE_I_SYMBOL_SAMPLES;
    }
    CHECK(off == DAB_MODE_I_FRAME_SAMPLES, "frame length cursor %d\n", off);

    /* Case 4: null remains zero across calls (statelessness w.r.t. PRS). */
    for (int i = 0; i < DAB_MODE_I_NULL_SAMPLES; ++i) {
        CHECK(frame[i] == 0.0f + 0.0f * (float _Complex)I,
              "null not zero after random run, i=%d\n", i);
    }

    free(fic); free(msc); free(frame);
    dab_frame_free(ctx);
    printf("test_frame: OK\n");
    return 0;
}
