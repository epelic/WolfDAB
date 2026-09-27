#include "mod/dqpsk.h"
#include "mod/phase_ref.h"

#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

#define CARRIERS 1536

int main(void) {
    float _Complex *prs  = (float _Complex *)calloc(CARRIERS, sizeof(*prs));
    float _Complex *sym  = (float _Complex *)calloc(CARRIERS, sizeof(*sym));
    uint8_t *iq          = (uint8_t *)calloc(DAB_MODE_I_CARRIERS_IQ_BYTES, 1);
    CHECK(prs && sym && iq, "alloc\n");

    dab_phase_ref_mode1(prs);

    /* Case 1: all-zero IQ bits → base = (1+j)/sqrt(2) for every carrier.
     * Expect |sym[k]| = 1 and sym[k] = prs[k] * (1+j)/sqrt(2). */
    memset(iq, 0, DAB_MODE_I_CARRIERS_IQ_BYTES);
    dab_dqpsk_map_mode1(prs, iq, sym);

    const float v = (float)(1.0 / 1.41421356237309504880);
    const float _Complex base_pp = v + v * (float _Complex)I;
    for (int k = 0; k < CARRIERS; ++k) {
        float mag = cabsf(sym[k]);
        CHECK(fabsf(mag - 1.0f) < 1e-6f, "|sym[%d]|=%f\n", k, mag);
        float _Complex expected = prs[k] * base_pp;
        float err = cabsf(sym[k] - expected);
        CHECK(err < 1e-6f, "sym[%d] err=%f\n", k, err);
    }

    /* Case 2: carrier 0 with I=1, Q=0 → base = (-1+j)/sqrt(2).
     * prs[0] = -j, so sym[0] = -j * (-1+j)/sqrt(2) = (j - j*j)/sqrt(2)
     *       = (j + 1)/sqrt(2). */
    memset(iq, 0, DAB_MODE_I_CARRIERS_IQ_BYTES);
    iq[0] = 0x80;  /* I-byte 0, MSB = carrier 0 I-bit = 1 */
    /* Q byte 0 stays 0. */
    dab_dqpsk_map_mode1(prs, iq, sym);
    float _Complex exp0 = v + v * (float _Complex)I;  /* (1 + j)/sqrt(2) */
    float err0 = cabsf(sym[0] - exp0);
    CHECK(err0 < 1e-6f, "carrier 0 hit err=%f got %f+%fj\n",
          err0, crealf(sym[0]), cimagf(sym[0]));

    /* Case 3: π/4 alternation. Starting from PRS (on {±1,±j}), after one
     * DQPSK symbol the constellation lives on (±1±j)/sqrt(2). After a
     * second DQPSK symbol applied on top it should return to {±1,±j}. */
    memset(iq, 0, DAB_MODE_I_CARRIERS_IQ_BYTES);
    dab_dqpsk_map_mode1(prs, iq, sym);
    float _Complex *sym2 = (float _Complex *)calloc(CARRIERS, sizeof(*sym2));
    CHECK(sym2, "alloc2\n");
    dab_dqpsk_map_mode1(sym, iq, sym2);
    for (int k = 0; k < CARRIERS; ++k) {
        float re = crealf(sym2[k]);
        float im = cimagf(sym2[k]);
        int on_axis = (fabsf(re) < 1e-5f && fabsf(fabsf(im) - 1.0f) < 1e-5f)
                   || (fabsf(im) < 1e-5f && fabsf(fabsf(re) - 1.0f) < 1e-5f);
        CHECK(on_axis, "sym2[%d] = %f+%fj not on {±1,±j}\n", k, re, im);
    }

    free(sym2);
    free(prs);
    free(sym);
    free(iq);
    printf("test_dqpsk: OK\n");
    return 0;
}
