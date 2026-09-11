#include "common/prbs.h"
#include <stdio.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

int main(void) {
    /* Roundtrip: scramble then descramble with fresh registers restores data. */
    uint8_t in[128];
    for (size_t i = 0; i < sizeof(in); ++i) in[i] = (uint8_t)(i * 37 + 11);

    uint8_t scrambled[128];
    prbs9_t p;

    prbs9_reset(&p);
    prbs9_xor(&p, in, scrambled, sizeof(in));
    /* Scrambled must differ from input (would be an astronomical coincidence
     * for a PRBS to produce the zero mask). */
    CHECK(memcmp(in, scrambled, sizeof(in)) != 0, "scramble is identity\n");

    uint8_t back[128];
    prbs9_reset(&p);
    prbs9_xor(&p, scrambled, back, sizeof(back));
    CHECK(memcmp(in, back, sizeof(back)) == 0, "roundtrip mismatch\n");

    /* PRBS9 with seed 0x1FF has maximum period 2^9 - 1 = 511 bits. After
     * scrambling 512 / 8 = 64 bytes with an all-zero input, we should see
     * that the XOR mask pattern repeats from byte 63.875 onwards. We check
     * the softer property: the mask over the first 511 bits has exactly
     * 256 ones (balanced m-sequence). */
    uint8_t zeros[64] = { 0 };
    uint8_t mask[64];
    prbs9_reset(&p);
    prbs9_xor(&p, zeros, mask, sizeof(zeros));
    int ones = 0;
    for (int i = 0; i < 511; ++i) {
        int byte = i / 8;
        int bit  = 7 - (i % 8);
        if ((mask[byte] >> bit) & 1) ones++;
    }
    CHECK(ones == 256, "m-sequence ones=%d expected 256\n", ones);

    printf("OK: prbs9 roundtrip, ones=%d\n", ones);
    return 0;
}
