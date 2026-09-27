#include "mod/freq_il.h"

void dab_freq_il_build_mode1(size_t *indices) {
    const size_t num      = 2048;
    const size_t carriers = 1536;
    const size_t alpha    = 13;
    const size_t beta     = 511;
    const size_t guard_lo = (num - carriers) / 2;          /* 256 */
    const size_t guard_hi = num - guard_lo;                /* 1792 */
    const size_t half     = num / 2;                       /* 1024 */

    size_t perm = 0;
    size_t out_idx = 0;
    for (size_t j = 1; j < num; ++j) {
        perm = (alpha * perm + beta) & (num - 1);
        if (perm >= guard_lo && perm <= guard_hi && perm != half) {
            indices[out_idx++] =
                (perm > half) ? (perm - (1 + half))
                              : (perm + (carriers - half));
        }
    }
    /* The loop must produce exactly `carriers` valid permutations. */
}

void dab_freq_il_apply(const size_t *indices,
                       const float _Complex *in,
                       float _Complex *out) {
    for (size_t j = 0; j < DAB_MODE_I_FI_CARRIERS; ++j) {
        out[indices[j]] = in[j];
    }
}
