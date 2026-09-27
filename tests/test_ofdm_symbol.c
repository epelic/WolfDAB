#include "mod/ofdm_symbol.h"

#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

int main(void) {
    ofdm_symbol_ctx_t *ctx = ofdm_symbol_new();
    CHECK(ctx, "new\n");

    float _Complex *carriers = (float _Complex *)calloc(DAB_MODE_I_CARRIERS, sizeof(*carriers));
    float _Complex *out      = (float _Complex *)calloc(DAB_MODE_I_SYMBOL_LEN, sizeof(*out));
    CHECK(carriers && out, "alloc\n");

    /* Case 1: all-zero carriers → all-zero output. */
    ofdm_symbol_build(ctx, carriers, out);
    for (int i = 0; i < DAB_MODE_I_SYMBOL_LEN; ++i) {
        CHECK(crealf(out[i]) == 0.0f && cimagf(out[i]) == 0.0f, "zero[%d]\n", i);
    }

    /* Case 2: uniform QPSK = (1+j)/sqrt(2) on all 1536 carriers.
     *
     * Parseval (with 1/sqrt(N) IFFT normalisation): the sum over the 2048
     * useful time-domain samples of |x[n]|^2 equals sum_k |X[k]|^2 = 1536.
     * Average power per useful sample = 1536/2048 = 0.75 EXACTLY.
     *
     * We deliberately don't include the cyclic prefix in this average:
     * with an all-equal carrier input the PAPR peaks at n=0 so the first
     * ~500 samples are atypical and the CP (a copy of the last 504 useful
     * samples) has its own non-representative average. */
    const float _Complex q = (1.0f + 1.0f * (float _Complex)I) / sqrtf(2.0f);
    for (int i = 0; i < DAB_MODE_I_CARRIERS; ++i) carriers[i] = q;
    ofdm_symbol_build(ctx, carriers, out);

    double power_useful = 0.0;
    for (int i = DAB_MODE_I_CP; i < DAB_MODE_I_SYMBOL_LEN; ++i) {
        double r = crealf(out[i]);
        double m = cimagf(out[i]);
        power_useful += r * r + m * m;
    }
    double avg_useful = power_useful / DAB_MODE_I_IFFT;
    CHECK(avg_useful > 0.74 && avg_useful < 0.76,
          "avg useful power %f expected 0.75\n", avg_useful);

    /* Cyclic prefix integrity: out[0..CP-1] == out[IFFT..IFFT+CP-1]
     * (the last CP samples of the IFFT output are replicated at the front). */
    for (int i = 0; i < DAB_MODE_I_CP; ++i) {
        float _Complex a = out[i];
        float _Complex b = out[DAB_MODE_I_IFFT + i];
        float d = cabsf(a - b);
        CHECK(d < 1e-5f, "CP[%d] differs from tail, |d|=%g\n", i, d);
    }

    /* Case 3: single carrier at k=+1 → time-domain complex exponential.
     * out[n] = (1/sqrt(N)) * exp(j·2π·n/N). Peak magnitude = 1/sqrt(N)
     * at every sample. Verify a few. */
    for (int i = 0; i < DAB_MODE_I_CARRIERS; ++i) carriers[i] = 0.0f;
    carriers[0] = 1.0f;   /* logical slot for k=+1 */
    ofdm_symbol_build(ctx, carriers, out);
    float expected_mag = 1.0f / sqrtf((float)DAB_MODE_I_IFFT);
    for (int i = 0; i < DAB_MODE_I_SYMBOL_LEN; i += 64) {
        float m = cabsf(out[i]);
        CHECK(fabsf(m - expected_mag) < 1e-5f,
              "k=+1 mag[%d]=%g expected %g\n", i, m, expected_mag);
    }

    free(carriers);
    free(out);
    ofdm_symbol_free(ctx);
    printf("OK: ofdm_symbol %d samples, CP %d\n",
           DAB_MODE_I_SYMBOL_LEN, DAB_MODE_I_CP);
    return 0;
}
