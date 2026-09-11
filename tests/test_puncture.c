#include "mod/puncture.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

int main(void) {
    /* PI_24 is the no-op pattern: 32 ones, popcount 32. */
    CHECK(DAB_PI[24] == 0xffffffffu, "PI_24\n");
    CHECK(__builtin_popcount(DAB_PI[24]) == 32, "PI_24 popcount\n");
    CHECK(__builtin_popcount(DAB_PI[8])  == 16, "PI_8 popcount\n");
    CHECK(__builtin_popcount(DAB_PI[7])  == 15, "PI_7 popcount\n");
    CHECK(__builtin_popcount(DAB_PI[1])  == 9,  "PI_1 popcount\n");

    /* Monotonicity: PI_k popcount should be non-decreasing in k.
     * (Higher PI index = higher rate = fewer bits punctured.) */
    for (int k = 2; k <= 24; ++k) {
        int a = __builtin_popcount(DAB_PI[k - 1]);
        int b = __builtin_popcount(DAB_PI[k]);
        CHECK(b >= a, "PI monotonicity broken at k=%d (%d -> %d)\n", k, a, b);
    }

    /* Pass-through test with PI_24: 4-byte mother + 3-byte tail with all-1
     * tail should produce exactly mother_bits + 24 output bits. */
    {
        dab_punct_rule_t rule = { .length_bytes = 4, .pattern = DAB_PI[24] };
        dab_punct_program_t prog = {
            .rules = &rule, .n_rules = 1, .tail_pattern = 0xffffffu
        };
        CHECK(dab_punct_out_bits(&prog) == 32 + 24, "passthrough out_bits\n");

        uint8_t mother[7] = { 0xA5, 0x5A, 0xF0, 0x0F, 0xDE, 0xAD, 0xBE };
        uint8_t out[8] = {0};
        size_t n = dab_punct_apply(&prog, mother, 7, out);
        CHECK(n == 56, "out bits %zu\n", n);
        /* Bits should match mother exactly (MSB-first). */
        for (int i = 0; i < 7; ++i) {
            CHECK(out[i] == mother[i], "pass[%d] %02x vs %02x\n",
                  i, out[i], mother[i]);
        }
    }

    /* Half-rate test: PI_8 = 0xCCCCCCCC keeps bits in positions
     * {0,1,4,5,8,9,12,13,...} of each 32-bit group (MSB = position 0).
     * That means every pair of two kept bits is followed by two dropped
     * bits. With a mother byte 0xFF the output for 8 input bits would be
     * the sequence 11 (keep) .. (drop) .. = bit pattern "1100 1100". */
    {
        dab_punct_rule_t rule = { .length_bytes = 4, .pattern = 0xccccccccu };
        dab_punct_program_t prog = {
            .rules = &rule, .n_rules = 1, .tail_pattern = 0u
        };
        CHECK(dab_punct_out_bits(&prog) == 16, "halfrate count\n");
        uint8_t mother[7] = { 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0 };
        uint8_t out[2] = {0};
        size_t n = dab_punct_apply(&prog, mother, 7, out);
        CHECK(n == 16, "halfrate bits %zu\n", n);
        CHECK(out[0] == 0xFF && out[1] == 0xFF,
              "halfrate out %02x %02x\n", out[0], out[1]);
    }

    printf("test_puncture: OK\n");
    return 0;
}
