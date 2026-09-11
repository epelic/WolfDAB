#include "mod/conv_enc.h"
#include "mod/puncture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

/* End-to-end test for EEP-3A at the protection profile we'll actually use
 * for the demo service: 128 kbps subchannel.
 *
 * Per frame (24 ms) EEP-3A @ 128 kbps expects:
 *   - subchannel input: 128 kbps * 24 ms = 3072 bits = 384 bytes
 *   - mother encoder output: 384*4 + 3 = 1539 bytes
 *   - punctured output: 6144 bits = 768 bytes = 96 CUs × 64 bits
 */
int main(void) {
    const int bitrate = 128;
    const size_t sub_bytes = (size_t)bitrate * 24 / 8;
    const size_t mother_bytes = DAB_CONV_OUT_BYTES(sub_bytes);
    const size_t expected_out_bits = (size_t)bitrate * 6 * 8;

    CHECK(sub_bytes == 384, "sub_bytes %zu\n", sub_bytes);
    CHECK(mother_bytes == 1539, "mother_bytes %zu\n", mother_bytes);
    CHECK(expected_out_bits == 6144, "expected_out_bits %zu\n",
          expected_out_bits);

    dab_punct_rule_t rules[2];
    dab_punct_program_t prog;
    CHECK(dab_eep3a_program(bitrate, rules, &prog) == 0, "program\n");

    /* Rule invariants. */
    CHECK(rules[0].length_bytes == (size_t)(((6 * 128 / 8) - 3) * 16),
          "r0 len %zu\n", rules[0].length_bytes);
    CHECK(rules[0].length_bytes == 1488, "r0 len value\n");
    CHECK(rules[1].length_bytes == 48, "r1 len %zu\n", rules[1].length_bytes);
    CHECK(rules[0].pattern == DAB_PI[8], "r0 pat\n");
    CHECK(rules[1].pattern == DAB_PI[7], "r1 pat\n");
    /* rules + tail (3 bytes) must cover the full mother stream. */
    CHECK(rules[0].length_bytes + rules[1].length_bytes + 3 == mother_bytes,
          "coverage\n");

    /* Expected output bit count. */
    CHECK(dab_punct_out_bits(&prog) == expected_out_bits,
          "out bits %zu\n", dab_punct_out_bits(&prog));

    /* Encode a pseudo-random subchannel and verify shapes + zero-input
     * trivial path. */
    uint8_t *sub = (uint8_t *)malloc(sub_bytes);
    uint8_t *mother = (uint8_t *)malloc(mother_bytes);
    uint8_t *out = (uint8_t *)calloc(1, expected_out_bits / 8 + 1);
    CHECK(sub && mother && out, "alloc\n");

    /* Zero input → zero output (conv encoder is linear, starts at zero). */
    memset(sub, 0, sub_bytes);
    dab_conv_enc_mother(sub, sub_bytes, mother);
    size_t n = dab_punct_apply(&prog, mother, mother_bytes, out);
    CHECK(n == expected_out_bits, "zero: out bits %zu\n", n);
    for (size_t i = 0; i < expected_out_bits / 8; ++i) {
        CHECK(out[i] == 0, "zero out[%zu]=0x%02x\n", i, out[i]);
    }

    /* Pseudo-random input: just assert bit count and that something was
     * written (non-trivial bit density). */
    uint32_t s = 0xBADCAFEu;
    for (size_t i = 0; i < sub_bytes; ++i) {
        s = s * 1664525u + 1013904223u;
        sub[i] = (uint8_t)(s >> 17);
    }
    memset(out, 0, expected_out_bits / 8 + 1);
    dab_conv_enc_mother(sub, sub_bytes, mother);
    n = dab_punct_apply(&prog, mother, mother_bytes, out);
    CHECK(n == expected_out_bits, "rand: out bits %zu\n", n);
    int ones = 0;
    for (size_t i = 0; i < expected_out_bits / 8; ++i) {
        ones += __builtin_popcount(out[i]);
    }
    /* Rate-1/2-ish ensemble: expect ~50% ones. Allow wide window. */
    CHECK(ones > 2000 && ones < 4000,
          "rand density ones=%d\n", ones);

    free(sub);
    free(mother);
    free(out);
    printf("test_eep3a: OK bitrate=%d sub=%zu mother=%zu out_bits=%zu\n",
           bitrate, sub_bytes, mother_bytes, expected_out_bits);
    return 0;
}
