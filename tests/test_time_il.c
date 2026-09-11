#include "mod/time_il.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

/* Delay tables per EN 300 401 §12 — must match the implementation in
 * time_il.c. Bit index 0 here corresponds to MSB (0x80), index 7 to LSB. */
static const int EVEN_DELAY[8] = { 0, 8, 4, 12, 2, 10, 6, 14 };
static const int ODR_DELAY[8]  = { 1, 9, 5, 13, 3, 11, 7, 15 };

int main(void) {
    /* Argument validation. */
    CHECK(dab_time_il_new(0) == NULL, "framesize 0 must fail\n");
    CHECK(dab_time_il_new(7) == NULL, "odd framesize must fail\n");

    const size_t FS = 8;
    dab_time_il_t *t = dab_time_il_new(FS);
    CHECK(t, "alloc\n");

    /* Case 1: first frame — every delay except d=0 (for even bytes) and
     * d=1 (for odd bytes) reads from still-zero history. All other bits
     * are zero. Input = all 0xFF. */
    uint8_t in[8], out[8];
    memset(in, 0xFF, 8);
    dab_time_il_process(t, in, out);
    /* Even byte 0: only d=0 bit (0x80) contributes → 0x80.
     * Odd  byte 1: only d=1 bit (0x80) — but d=1 means previous frame,
     *              still zero → 0x00. */
    CHECK(out[0] == 0x80, "frame0 even byte = 0x%02x\n", out[0]);
    CHECK(out[1] == 0x00, "frame0 odd byte = 0x%02x\n", out[1]);

    /* Case 2: run 15 more frames of zeros, then verify the 0xFF bits
     * reappear at each of the expected delays. */
    uint8_t zero[8] = {0};
    /* Track the "hit count" for each bit position of byte 0 (even). */
    int hits_even[8] = {0};
    int hits_odd[8]  = {0};
    /* frame0 already counted above: bit 7 hit. */
    hits_even[0] += (out[0] & 0x80) ? 1 : 0;

    for (int f = 1; f <= 15; ++f) {
        dab_time_il_process(t, zero, out);
        for (int b = 0; b < 8; ++b) {
            int mask = 1 << (7 - b);
            if (out[0] & mask) hits_even[b] += 1;
            if (out[1] & mask) hits_odd[b]  += 1;
        }
    }
    /* Each bit position should have fired exactly once — at the frame
     * whose delay equals the position's delay value. */
    for (int b = 0; b < 8; ++b) {
        CHECK(hits_even[b] == 1, "even bit %d hits=%d\n", b, hits_even[b]);
        CHECK(hits_odd[b]  == 1, "odd bit %d hits=%d\n",  b, hits_odd[b]);
    }

    dab_time_il_free(t);

    /* Case 3: steady-state roundtrip consistency. Feed pseudo-random
     * input frames, after the 16-frame warm-up verify that each output
     * bit matches the correct delayed input bit. */
    const size_t FS2 = 32;
    const int N_FRAMES = 40;
    t = dab_time_il_new(FS2);
    CHECK(t, "alloc2\n");

    uint8_t *frames = (uint8_t *)malloc((size_t)N_FRAMES * FS2);
    uint8_t *outbuf = (uint8_t *)malloc(FS2);
    CHECK(frames && outbuf, "alloc3\n");
    uint32_t s = 0xDEADBEEFu;
    for (int f = 0; f < N_FRAMES; ++f) {
        for (size_t i = 0; i < FS2; ++i) {
            s = s * 1664525u + 1013904223u;
            frames[(size_t)f * FS2 + i] = (uint8_t)(s >> 16);
        }
    }
    for (int f = 0; f < N_FRAMES; ++f) {
        dab_time_il_process(t, frames + (size_t)f * FS2, outbuf);
        if (f < 15) continue;
        for (size_t i = 0; i < FS2; ++i) {
            const int *d = (i & 1u) ? ODR_DELAY : EVEN_DELAY;
            for (int b = 0; b < 8; ++b) {
                int mask = 1 << (7 - b);
                int src_frame = f - d[b];
                int exp = (frames[(size_t)src_frame * FS2 + i] & mask) ? 1 : 0;
                int got = (outbuf[i] & mask) ? 1 : 0;
                CHECK(got == exp,
                      "steady f=%d i=%zu b=%d src=%d got=%d exp=%d\n",
                      f, i, b, src_frame, got, exp);
            }
        }
    }
    free(frames);
    free(outbuf);
    dab_time_il_free(t);

    printf("test_time_il: OK\n");
    return 0;
}
