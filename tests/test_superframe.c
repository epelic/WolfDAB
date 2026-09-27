#include "common/crc16.h"
#include "dabplus/superframe.h"
#include "fec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

static uint16_t read_u16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

int main(void) {
    dabplus_cfg_t cfg = {
        .bitrate_kbps     = 64,
        .num_aus          = 6,
        .dac_rate         = 1,  /* 48 kHz */
        .sbr_flag         = 0,
        .aac_channel_mode = 1,  /* stereo */
        .ps_flag          = 0,
        .mpeg_surround    = 0,
    };

    /* Size arithmetic: 64 kbps → 960 bytes total, 110 * 8 = 880 data. */
    CHECK(dabplus_sf_size(&cfg)      == 960, "sf_size\n");
    CHECK(dabplus_sf_data_size(&cfg) == 880, "data_size\n");
    CHECK(dabplus_sf_header_size(&cfg) == 11, "header_size num_aus=6\n");

    dabplus_sf_t *sf = dabplus_sf_new(&cfg);
    CHECK(sf, "sf_new\n");

    /* Make six synthetic AUs of varying but plausible sizes. */
    uint8_t au_data[6][200];
    const uint8_t *au_ptrs[6];
    size_t         au_sizes[6] = { 180, 140, 120, 160, 100, 130 };
    for (int i = 0; i < 6; ++i) {
        for (size_t k = 0; k < au_sizes[i]; ++k)
            au_data[i][k] = (uint8_t)((i * 37 + k * 13 + 1) & 0xFF);
        au_ptrs[i] = au_data[i];
    }
    size_t au_total = 0;
    for (int i = 0; i < 6; ++i) au_total += au_sizes[i] + 2;
    CHECK(au_total + dabplus_sf_header_size(&cfg) <= 880, "AUs fit in data area\n");

    uint8_t out[960];
    CHECK(dabplus_sf_build(sf, au_ptrs, au_sizes, out, sizeof(out)) == 0, "build\n");

    /* Output is row-major per ETSI TS 102 563: logical byte k (header,
     * AUs, CRCs, pad) lives at out[k] for k in [0..data_size-1], and the
     * 10 RS parity rows live at out[data_size..sf_size-1]. So we can
     * inspect the header / AUs / CRCs straight from `out`. */
    const int R = 8;
    const uint8_t *data = out;

    /* Firecode check: recompute over bytes 2..10 and compare to bytes 0..1. */
    uint16_t fc_got = read_u16(data);
    uint16_t fc_ref = crc16_firecode(data + 2, 9);
    CHECK(fc_got == fc_ref, "firecode mismatch: got %04x ref %04x\n", fc_got, fc_ref);

    /* Decode the config byte: dac_rate=1, sbr=0, stereo=1, ps=0, surround=0
     * → 01010000 = 0x50. */
    CHECK(data[2] == 0x50, "config byte=0x%02x expected 0x50\n", data[2]);

    /* au_start[0] is implicit = 11; first AU payload should match au_data[0]. */
    CHECK(memcmp(data + 11, au_data[0], au_sizes[0]) == 0, "AU0 content\n");

    /* Decode au_start[1..5] from bytes 3..10 (5 × 12 bit MSB-first). */
    size_t au_start[6] = { 11, 0, 0, 0, 0, 0 };
    for (int i = 1; i <= 5; ++i) {
        int bit_off = (i - 1) * 12;
        int byte_off = 3 + bit_off / 8;
        int shift    = bit_off % 8;
        uint32_t v = ((uint32_t)data[byte_off] << 16) |
                     ((uint32_t)data[byte_off + 1] << 8) |
                     (uint32_t)data[byte_off + 2];
        au_start[i] = (v >> (24 - shift - 12)) & 0xFFF;
    }
    /* AU CRCs: the non-last AUs store crc16_ccitt(AU) XOR 0xFFFF at
     * au_start[i] + au_sizes[i]. The last AU stores its CRC at the fixed
     * position data_size-2, covering the extended range from au_start[5]
     * up to data_size-2 (including any zero padding from calloc). */
    size_t expected = 11;
    const size_t data_size_local = 880;
    for (int i = 0; i < 6; ++i) {
        CHECK(au_start[i] == expected, "au_start[%d]=%zu expected %zu\n",
              i, au_start[i], expected);
        if (i < 5) {
            uint16_t crc_stored = read_u16(data + expected + au_sizes[i]);
            uint16_t crc_ref =
                (uint16_t)(crc16_ccitt(au_data[i], au_sizes[i]) ^ 0xFFFF);
            CHECK(crc_stored == crc_ref, "AU%d CRC %04x vs %04x\n",
                  i, crc_stored, crc_ref);
            expected += au_sizes[i] + 2;
        } else {
            /* Last AU: CRC at data_size - 2, covers [au_start[5]..crc_pos-1]. */
            size_t crc_pos = data_size_local - 2;
            size_t crc_len = crc_pos - au_start[i];
            uint16_t crc_stored = read_u16(data + crc_pos);
            uint16_t crc_ref =
                (uint16_t)(crc16_ccitt(data + au_start[i], crc_len) ^ 0xFFFF);
            CHECK(crc_stored == crc_ref,
                  "AU%d (last) CRC %04x vs %04x\n", i, crc_stored, crc_ref);
        }
    }

    /* RS error-correction: flip up to 5 bytes in column 3 and verify that
     * decode_rs_char fixes them. */
    void *rs = init_rs_char(8, 0x11d, 0, 1, 10, 135);
    CHECK(rs != NULL, "init_rs_char\n");

    /* Extract column 3 (a full 120-byte RS codeword) from the row-major
     * output, flip 5 bytes, then check that libfec corrects them. */
    uint8_t col[120];
    uint8_t col_ref[120];
    for (int row = 0; row < 120; ++row) col_ref[row] = out[row * R + 3];
    memcpy(col, col_ref, 120);
    col[7]  ^= 0xA5;
    col[42] ^= 0x3C;
    col[99] ^= 0x77;
    col[109]^= 0x11;
    col[115]^= 0xFE;    /* 5 errors, exactly t=5 */
    int nerr = decode_rs_char(rs, col, NULL, 0);
    CHECK(nerr == 5, "RS corrected errors, got %d\n", nerr);
    CHECK(memcmp(col, col_ref, 120) == 0, "RS restored column bytes\n");
    free_rs_char(rs);

    dabplus_sf_free(sf);
    printf("OK: superframe 64 kbps 6 AUs, %zu data bytes protected to %zu\n",
           (size_t)880, (size_t)960);
    return 0;
}
