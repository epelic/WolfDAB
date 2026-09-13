#include "mod/msc_code.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

static void fill_random(uint8_t *buf, size_t n, uint32_t seed) {
    uint32_t s = seed;
    for (size_t i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        buf[i] = (uint8_t)(s >> 17);
    }
}

int main(void) {
    /* ---- Constants ---- */
    CHECK(DAB_MSC_CIF_BYTES      == 6912,  "cif bytes\n");
    CHECK(DAB_MSC_CIFS_PER_FRAME == 4,     "cifs per frame\n");
    CHECK(DAB_MSC_FRAME_BYTES    == 27648, "frame bytes\n");

    /* ---- Geometry: 128 kbps @ start 0 ---- */
    dab_msc_subch_t *s128 = dab_msc_subch_new(128, 0, DAB_EEP_3A);
    CHECK(s128 != NULL, "new(128, 0)\n");
    CHECK(dab_msc_subch_in_bytes (s128) == 384, "128 in_bytes\n");
    CHECK(dab_msc_subch_out_bytes(s128) == 768, "128 out_bytes\n");
    CHECK(dab_msc_subch_start_byte(s128) == 0, "128 start_byte\n");

    /* ---- Geometry: 64 kbps @ start 12 (CU) = byte offset 96 ---- */
    dab_msc_subch_t *s64 = dab_msc_subch_new(64, 12, DAB_EEP_3A);
    CHECK(s64 != NULL, "new(64, 12)\n");
    CHECK(dab_msc_subch_in_bytes (s64) == 192, "64 in_bytes\n");
    CHECK(dab_msc_subch_out_bytes(s64) == 384, "64 out_bytes\n");
    CHECK(dab_msc_subch_start_byte(s64) == 96, "64 start_byte\n");

    /* ---- Invalid bitrates ---- */
    dab_msc_subch_t *s24 = dab_msc_subch_new(24, 0, DAB_EEP_3A);
    CHECK(s24 != NULL, "accept 24 kbps EEP-3A\n");
    CHECK(dab_msc_subch_in_bytes(s24) == 72 && dab_msc_subch_out_bytes(s24) == 144,
          "24 kbps geometry\n");
    dab_msc_subch_free(s24);
    dab_msc_subch_t *s8 = dab_msc_subch_new(8, 0, DAB_EEP_3A);
    dab_msc_subch_t *s16 = dab_msc_subch_new(16, 0, DAB_EEP_3A);
    CHECK(s8 != NULL && s16 != NULL, "accept 8/16 kbps EEP-3A\n");
    dab_msc_subch_free(s8); dab_msc_subch_free(s16);
    CHECK(dab_msc_subch_new(400, 0, DAB_EEP_3A) == NULL, "reject 400 kbps\n");
    CHECK(dab_msc_subch_new(100, 0, DAB_EEP_3A) == NULL, "reject 100 kbps (not /8)\n");

    /* ---- Determinism across fresh contexts ---- */
    uint8_t zero[384] = {0};
    uint8_t out_a[768], out_b[768];
    dab_msc_subch_encode_cif(s128, zero, out_a);
    dab_msc_subch_free(s128);
    s128 = dab_msc_subch_new(128, 0, DAB_EEP_3A);
    dab_msc_subch_encode_cif(s128, zero, out_b);
    CHECK(memcmp(out_a, out_b, sizeof(out_a)) == 0, "determinism\n");

    /* ---- Zero input still produces non-zero output (PRBS seed nonzero).
     * On CIF 0 the time interleaver is mostly pulling zeros from empty
     * history, but bits with delay 0 pass through, so some are set. ---- */
    int ones = 0;
    for (size_t i = 0; i < sizeof(out_a); ++i) ones += __builtin_popcount(out_a[i]);
    CHECK(ones > 0, "zero-input all zero — scrambler broken?\n");

    /* ---- Input sensitivity ---- */
    dab_msc_subch_free(s128);
    s128 = dab_msc_subch_new(128, 0, DAB_EEP_3A);
    uint8_t other[384];
    memset(other, 0xA5, sizeof(other));
    uint8_t out_c[768];
    dab_msc_subch_encode_cif(s128, other, out_c);
    CHECK(memcmp(out_a, out_c, sizeof(out_a)) != 0, "input sensitivity\n");

    /* ---- frame_build_single layout: start_address 0, zero-pad verified ---- */
    dab_msc_subch_free(s128);
    s128 = dab_msc_subch_new(128, 0, DAB_EEP_3A);
    uint8_t in_frame[4 * 384];
    fill_random(in_frame, sizeof(in_frame), 0xDEADBEEFu);
    static uint8_t frame_out[DAB_MSC_FRAME_BYTES];
    dab_msc_frame_build_single(s128, in_frame, frame_out);

    for (unsigned c = 0; c < DAB_MSC_CIFS_PER_FRAME; ++c) {
        const uint8_t *cif = frame_out + (size_t)c * DAB_MSC_CIF_BYTES;
        /* Bytes 768..6911 must be zero (no other subchannel). */
        for (size_t i = 768; i < DAB_MSC_CIF_BYTES; ++i) {
            CHECK(cif[i] == 0, "cif %u byte %zu not padded\n", c, i);
        }
    }

    /* ---- frame_build_single matches per-CIF encode_cif when replayed ---- */
    dab_msc_subch_free(s128);
    s128 = dab_msc_subch_new(128, 0, DAB_EEP_3A);
    for (unsigned c = 0; c < DAB_MSC_CIFS_PER_FRAME; ++c) {
        uint8_t expect[768];
        dab_msc_subch_encode_cif(s128, in_frame + (size_t)c * 384, expect);
        const uint8_t *cif = frame_out + (size_t)c * DAB_MSC_CIF_BYTES;
        CHECK(memcmp(cif, expect, 768) == 0, "cif %u replay mismatch\n", c);
    }

    /* ---- start_address != 0: offset placement ---- */
    dab_msc_subch_t *s128_off = dab_msc_subch_new(128, 12, DAB_EEP_3A); /* byte offset 96 */
    static uint8_t frame_off[DAB_MSC_FRAME_BYTES];
    dab_msc_frame_build_single(s128_off, in_frame, frame_off);
    for (unsigned c = 0; c < DAB_MSC_CIFS_PER_FRAME; ++c) {
        const uint8_t *cif = frame_off + (size_t)c * DAB_MSC_CIF_BYTES;
        /* Bytes [0, 96) zero */
        for (size_t i = 0; i < 96; ++i) {
            CHECK(cif[i] == 0, "off cif %u pre-byte %zu nonzero\n", c, i);
        }
        /* Bytes [96+768, 6912) zero */
        for (size_t i = 96 + 768; i < DAB_MSC_CIF_BYTES; ++i) {
            CHECK(cif[i] == 0, "off cif %u post-byte %zu nonzero\n", c, i);
        }
    }

    dab_msc_subch_free(s128);
    dab_msc_subch_free(s128_off);
    dab_msc_subch_free(s64);

    printf("test_msc_code: OK\n");
    return 0;
}
