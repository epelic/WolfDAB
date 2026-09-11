#include "mod/conv_enc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

/* Reference encoder: same algorithm, different loop structure — one bit at
 * a time through a 7-bit shift register. Used to cross-check the packed
 * byte-level implementation. */
static void ref_encode(const uint8_t *in, size_t n_in_bytes, uint8_t *out_bits) {
    unsigned mem = 0;
    size_t obi = 0;
    size_t total_in_bits = n_in_bytes * 8;
    for (size_t i = 0; i < total_in_bits + 6; ++i) {
        unsigned bit;
        if (i < total_in_bits) {
            bit = (in[i >> 3] >> (7 - (i & 7))) & 1u;
        } else {
            bit = 0;  /* tail */
        }
        mem = (mem >> 1) | (bit << 6);
        out_bits[obi++] = (uint8_t)__builtin_parity(mem & 0x5bu);
        out_bits[obi++] = (uint8_t)__builtin_parity(mem & 0x79u);
        out_bits[obi++] = (uint8_t)__builtin_parity(mem & 0x65u);
        out_bits[obi++] = (uint8_t)__builtin_parity(mem & 0x5bu);
    }
}

static void pack_bits(const uint8_t *bits, size_t n_bits, uint8_t *out) {
    memset(out, 0, (n_bits + 7) / 8);
    for (size_t i = 0; i < n_bits; ++i) {
        out[i >> 3] |= (uint8_t)(bits[i] << (7 - (i & 7)));
    }
}

int main(void) {
    /* Case 1: zero input → zero output (encoder starts at zero, stays). */
    {
        uint8_t zin[16] = {0};
        uint8_t zout[DAB_CONV_OUT_BYTES(16)];
        dab_conv_enc_mother(zin, 16, zout);
        for (size_t i = 0; i < sizeof(zout); ++i) {
            CHECK(zout[i] == 0, "zero[%zu] = 0x%02x\n", i, zout[i]);
        }
    }

    /* Case 2: impulse (input byte 0x80 then zeros). Hand-computed first
     * four output bytes from generator-polynomial evaluation.
     * See analysis in memory/project_state.md / spec §11.1. */
    {
        uint8_t in[4] = {0x80, 0x00, 0x00, 0x00};
        uint8_t out[DAB_CONV_OUT_BYTES(4)];
        dab_conv_enc_mother(in, 4, out);
        CHECK(out[0] == 0xF6, "impulse[0] = 0x%02x exp 0xF6\n", out[0]);
        CHECK(out[1] == 0xDD, "impulse[1] = 0x%02x exp 0xDD\n", out[1]);
        CHECK(out[2] == 0x29, "impulse[2] = 0x%02x exp 0x29\n", out[2]);
        CHECK(out[3] == 0xF0, "impulse[3] = 0x%02x exp 0xF0\n", out[3]);
        /* From byte 4 onward the memory has fully cleared (1 shifted past
         * D^6) so encoder output is zero until the tail phase — which also
         * outputs zero because the tail is zeros into an all-zero state. */
        for (size_t i = 4; i < sizeof(out); ++i) {
            CHECK(out[i] == 0, "impulse tail[%zu] = 0x%02x\n", i, out[i]);
        }
    }

    /* Case 3: pseudo-random 128-byte input cross-checked against the
     * per-bit reference implementation. */
    {
        const size_t N = 128;
        uint8_t in[128];
        uint32_t s = 0xC0FFEE42u;
        for (size_t i = 0; i < N; ++i) {
            s = s * 1664525u + 1013904223u;
            in[i] = (uint8_t)(s >> 17);
        }
        uint8_t out[DAB_CONV_OUT_BYTES(128)];
        dab_conv_enc_mother(in, N, out);

        const size_t ref_bits = N * 32 + 24;
        uint8_t *ref = (uint8_t *)malloc(ref_bits);
        uint8_t *ref_packed = (uint8_t *)malloc((ref_bits + 7) / 8);
        CHECK(ref && ref_packed, "alloc\n");
        ref_encode(in, N, ref);
        pack_bits(ref, ref_bits, ref_packed);

        for (size_t i = 0; i < sizeof(out); ++i) {
            CHECK(out[i] == ref_packed[i],
                  "random[%zu] packed=0x%02x ref=0x%02x\n",
                  i, out[i], ref_packed[i]);
        }
        free(ref);
        free(ref_packed);
    }

    /* Case 4: output length sanity. */
    CHECK(DAB_CONV_OUT_BYTES(384) == 1539, "length macro\n");

    printf("test_conv_enc: OK\n");
    return 0;
}
