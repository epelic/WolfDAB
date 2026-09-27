#include "mod/phase_ref.h"

#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

static int approx_eq(float _Complex a, float _Complex b) {
    return fabsf(crealf(a) - crealf(b)) < 1e-6f
        && fabsf(cimagf(a) - cimagf(b)) < 1e-6f;
}

int main(void) {
    float _Complex *prs = (float _Complex *)calloc(DAB_MODE_I_PRS_CARRIERS,
                                                   sizeof(*prs));
    CHECK(prs, "alloc\n");

    dab_phase_ref_mode1(prs);

    /* Unit-magnitude invariant: every PRS carrier is on the unit circle. */
    for (int k = 0; k < DAB_MODE_I_PRS_CARRIERS; ++k) {
        float mag = cabsf(prs[k]);
        CHECK(fabsf(mag - 1.0f) < 1e-6f, "|prs[%d]| = %f\n", k, mag);
    }

    /* Values land on the QPSK grid {+1, +j, -1, -j}. */
    for (int k = 0; k < DAB_MODE_I_PRS_CARRIERS; ++k) {
        float re = crealf(prs[k]);
        float im = cimagf(prs[k]);
        int is_pm1_re = (fabsf(re - 1.0f) < 1e-6f || fabsf(re + 1.0f) < 1e-6f);
        int is_zero_re = fabsf(re) < 1e-6f;
        int is_pm1_im = (fabsf(im - 1.0f) < 1e-6f || fabsf(im + 1.0f) < 1e-6f);
        int is_zero_im = fabsf(im) < 1e-6f;
        CHECK((is_pm1_re && is_zero_im) || (is_zero_re && is_pm1_im),
              "prs[%d] = %f + %fj off the QPSK grid\n", k, re, im);
    }

    /* First few carriers: hand-computed from Mode I Table 44 offset 0
     * = (i=0, n=3) and H_TABLE[0][0..3] = {0, 2, 0, 0}.
     *   phi[0] = (0 + 3) mod 4 = 3 → -j
     *   phi[1] = (2 + 3) mod 4 = 1 → +j
     *   phi[2] = (0 + 3) mod 4 = 3 → -j
     *   phi[3] = (0 + 3) mod 4 = 3 → -j
     */
    CHECK(approx_eq(prs[0], 0.0f - 1.0f * I), "prs[0]\n");
    CHECK(approx_eq(prs[1], 0.0f + 1.0f * I), "prs[1]\n");
    CHECK(approx_eq(prs[2], 0.0f - 1.0f * I), "prs[2]\n");
    CHECK(approx_eq(prs[3], 0.0f - 1.0f * I), "prs[3]\n");

    /* First carrier of the negative-frequency block lives at index 768.
     * Offset 24 in MODE1_TABLE: (i=0, n=1), H_TABLE[0][0] = 0
     *   phi = (0 + 1) mod 4 = 1 → +j
     */
    CHECK(approx_eq(prs[768], 0.0f + 1.0f * I), "prs[768]\n");

    free(prs);
    printf("test_phase_ref: OK\n");
    return 0;
}
