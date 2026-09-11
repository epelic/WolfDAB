#include "mod/fic_code.h"
#include "mod/puncture.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

int main(void) {
    /* Output size sanity. */
    CHECK(DAB_FIC_BYTES_PER_CIF     == 96,   "size const\n");
    CHECK(DAB_FIC_OUT_BYTES_PER_CIF == 288,  "out size const\n");
    CHECK(DAB_FIC_FRAME_OUT_BYTES   == 1152, "frame out const\n");

    /* Case 1: zero input still produces non-zero output because the PRBS
     * seed is non-zero. Check output is deterministic across calls. */
    uint8_t zero_in[DAB_FIC_BYTES_PER_CIF] = {0};
    uint8_t out_a[DAB_FIC_OUT_BYTES_PER_CIF];
    uint8_t out_b[DAB_FIC_OUT_BYTES_PER_CIF];
    dab_fic_encode_cif(zero_in, out_a);
    dab_fic_encode_cif(zero_in, out_b);
    CHECK(memcmp(out_a, out_b, sizeof(out_a)) == 0, "deterministic\n");

    int ones = 0;
    for (size_t i = 0; i < sizeof(out_a); ++i) ones += __builtin_popcount(out_a[i]);
    CHECK(ones > 0, "zero-input: output all zero, scrambler broken?\n");

    /* Case 2: changing the input changes the output. */
    uint8_t other_in[DAB_FIC_BYTES_PER_CIF];
    memset(other_in, 0xAA, sizeof(other_in));
    uint8_t out_c[DAB_FIC_OUT_BYTES_PER_CIF];
    dab_fic_encode_cif(other_in, out_c);
    CHECK(memcmp(out_a, out_c, sizeof(out_a)) != 0, "input sensitivity\n");

    /* Case 3: frame encoder writes 4 independent per-CIF blocks. */
    uint8_t frame_in[DAB_FIC_FRAME_IN_BYTES];
    uint8_t frame_out[DAB_FIC_FRAME_OUT_BYTES];
    uint32_t s = 0xC0DEBA5Eu;
    for (size_t i = 0; i < sizeof(frame_in); ++i) {
        s = s * 1664525u + 1013904223u;
        frame_in[i] = (uint8_t)(s >> 17);
    }
    dab_fic_encode_frame(frame_in, frame_out);

    for (unsigned c = 0; c < DAB_FIC_CIFS_PER_FRAME; ++c) {
        uint8_t ref[DAB_FIC_OUT_BYTES_PER_CIF];
        dab_fic_encode_cif(frame_in + c * DAB_FIC_BYTES_PER_CIF, ref);
        CHECK(memcmp(frame_out + c * DAB_FIC_OUT_BYTES_PER_CIF,
                     ref, sizeof(ref)) == 0,
              "frame cif %u mismatch\n", c);
    }

    /* Case 4: random input produces ~50% bit density (±15%). */
    int total_ones = 0;
    for (size_t i = 0; i < sizeof(frame_out); ++i)
        total_ones += __builtin_popcount(frame_out[i]);
    int total_bits = (int)sizeof(frame_out) * 8;  /* 9216 */
    CHECK(total_bits == 9216, "frame bits %d\n", total_bits);
    CHECK(total_ones > total_bits * 35 / 100 && total_ones < total_bits * 65 / 100,
          "density ones=%d/%d\n", total_ones, total_bits);

    /* Case 5: cross-check puncturing output bit count against program. */
    CHECK(dab_punct_out_bits(&(dab_punct_program_t){
            .rules = (dab_punct_rule_t[2]){
                { .length_bytes = 21 * 16, .pattern = 0xEEEEEEEEu },
                { .length_bytes =  3 * 16, .pattern = 0xEEEEEEECu },
            },
            .n_rules = 2,
            .tail_pattern = 0xCCCCCCu,
          }) == 2304, "bits_out == 2304\n");

    printf("test_fic_code: OK\n");
    return 0;
}
