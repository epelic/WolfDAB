#include "mod/freq_il.h"

#include <complex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

int main(void) {
    size_t indices[DAB_MODE_I_FI_CARRIERS];
    dab_freq_il_build_mode1(indices);

    /* Hand-computed first four entries from the recurrence
     *   perm_1 = 511  → perm<1024 → 511 + 512 = 1023
     *   perm_2 = (13*511 + 511) mod 2048 = 1010 → 1010 + 512 = 1522
     *   perm_3 = (13*1010 + 511) mod 2048 = 1353 → 1353 - 1025 = 328
     *   perm_4 = (13*1353 + 511) mod 2048 = 1716 → 1716 - 1025 = 691
     */
    CHECK(indices[0] == 1023, "indices[0] = %zu\n", indices[0]);
    CHECK(indices[1] == 1522, "indices[1] = %zu\n", indices[1]);
    CHECK(indices[2] == 328,  "indices[2] = %zu\n", indices[2]);
    CHECK(indices[3] == 691,  "indices[3] = %zu\n", indices[3]);

    /* Bijectivity: every physical carrier slot is hit exactly once. */
    {
        uint8_t seen[DAB_MODE_I_FI_CARRIERS] = {0};
        for (size_t j = 0; j < DAB_MODE_I_FI_CARRIERS; ++j) {
            CHECK(indices[j] < DAB_MODE_I_FI_CARRIERS,
                  "indices[%zu] = %zu out of range\n", j, indices[j]);
            CHECK(seen[indices[j]] == 0,
                  "collision at j=%zu → %zu\n", j, indices[j]);
            seen[indices[j]] = 1;
        }
        for (size_t p = 0; p < DAB_MODE_I_FI_CARRIERS; ++p) {
            CHECK(seen[p] == 1, "carrier %zu not covered\n", p);
        }
    }

    /* Round-trip: interleave then de-interleave returns the original. */
    {
        float _Complex *in   = malloc(sizeof(*in)   * DAB_MODE_I_FI_CARRIERS);
        float _Complex *mid  = malloc(sizeof(*mid)  * DAB_MODE_I_FI_CARRIERS);
        float _Complex *back = malloc(sizeof(*back) * DAB_MODE_I_FI_CARRIERS);
        CHECK(in && mid && back, "alloc\n");
        for (size_t j = 0; j < DAB_MODE_I_FI_CARRIERS; ++j) {
            in[j] = (float)j + (float)(1.0f * (float)j) * (float _Complex)I;
        }
        dab_freq_il_apply(indices, in, mid);
        for (size_t j = 0; j < DAB_MODE_I_FI_CARRIERS; ++j) {
            back[j] = mid[indices[j]];
        }
        for (size_t j = 0; j < DAB_MODE_I_FI_CARRIERS; ++j) {
            CHECK(back[j] == in[j], "roundtrip j=%zu\n", j);
        }
        free(in); free(mid); free(back);
    }

    printf("test_freq_il: OK\n");
    return 0;
}
