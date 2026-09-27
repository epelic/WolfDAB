#include "common/crc16.h"
#include "mux/eti.h"
#include "mux/fib.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

/* Build 3 FIBs for the FIC of one Mode I logical frame. */
static void build_fics(uint8_t *fics /* 96 bytes */) {
    fib_t fib;

    fib_reset(&fib);
    fig0_0_t f00 = { .ensemble_id = 0xE001 };
    fig0_0_write(&fib, &f00);
    fig0_1_eep_t f01 = {
        .sub_ch_id       = 1,
        .start_addr      = 0,
        .eep_option      = 0,       /* EEP-A */
        .prot_level      = 2,       /* EEP 3-A */
        .sub_ch_size_cus = 48,      /* 64 kbps EEP-3A = 48 CUs */
    };
    fig0_1_eep_write(&fib, &f01);
    fib_finalize(&fib);
    memcpy(fics + 0, fib.bytes, 32);

    fib_reset(&fib);
    fig0_2_audio_t f02 = {
        .service_id = 0xE001,
        .asc_type   = 63,           /* DAB+ */
        .sub_ch_id  = 1,
    };
    fig0_2_audio_write(&fib, &f02);
    fig0_8_audio_t f08 = { .service_id = 0xE001, .sub_ch_id = 1 };
    fig0_8_audio_write(&fib, &f08);
    fib_finalize(&fib);
    memcpy(fics + 32, fib.bytes, 32);

    fib_reset(&fib);
    fig1_0_write(&fib, 0xE001, "dabtx test Ens", 0x0000);
    fib_finalize(&fib);
    memcpy(fics + 64, fib.bytes, 32);
}

int main(void) {
    /* Single DAB+ audio subchannel at 64 kbps EEP-3A. Payload = 192 bytes. */
    eti_subch_t sub = {
        .sub_ch_id      = 1,
        .start_addr_cus = 0,
        .tpl            = 0x22,    /* 0x20 | (EEP_A<<2=0) | level 2 */
        .size_bytes     = 192,
    };

    eti_builder_t b;
    eti_builder_init(&b, &sub);

    /* FL = 1 + 24 + 1 + (192/4) = 74 words → frame_size = (74+4)*4 = 312 bytes */
    const size_t expected = (1 + 24 + 1 + 192 / 4 + 4) * 4;
    CHECK(expected == 312, "size math: %zu\n", expected);

    uint8_t fics[96];
    build_fics(fics);
    /* verify each FIB has a valid FIB CRC */
    for (int i = 0; i < 3; ++i) {
        uint16_t got = (uint16_t)((fics[i * 32 + 30] << 8) | fics[i * 32 + 31]);
        uint16_t ref = crc16_fib(fics + i * 32, 30);
        CHECK(got == ref, "FIB %d CRC %04x != %04x\n", i, got, ref);
    }

    uint8_t payload[192];
    for (int i = 0; i < 192; ++i) payload[i] = (uint8_t)(i ^ 0xA5);

    uint8_t frame[ETI_MAX_FRAME];
    size_t n = eti_builder_frame(&b, fics, payload, frame);
    CHECK(n == 312, "frame size %zu\n", n);

    /* SYNC: ERR = 0xFF, FSYNC (frame 0, even) = 0x49 0xC5 0xF8 */
    CHECK(frame[0] == 0xFF, "ERR\n");
    CHECK(frame[1] == 0x49 && frame[2] == 0xC5 && frame[3] == 0xF8,
          "FSYNC %02x %02x %02x\n", frame[1], frame[2], frame[3]);

    /* FC decode */
    CHECK(frame[4] == 0, "FCT\n");
    CHECK((frame[5] & 0x80) != 0, "FICF=1\n");
    CHECK((frame[5] & 0x7F) == 1, "NST=1\n");
    CHECK(((frame[6] >> 3) & 0x03) == 1, "MID=1 (Mode I)\n");
    uint16_t FL = (uint16_t)(((frame[6] & 0x07) << 8) | frame[7]);
    CHECK(FL == 74, "FL=%u expected 74\n", FL);

    /* STC decode */
    CHECK((frame[8] >> 2) == 1, "STC SCID=1\n");
    uint16_t sad = (uint16_t)(((frame[8] & 0x03) << 8) | frame[9]);
    CHECK(sad == 0, "STC SAD=0\n");
    CHECK((frame[10] >> 2) == 0x22, "STC TPL=0x22\n");
    uint16_t stl = (uint16_t)(((frame[10] & 0x03) << 8) | frame[11]);
    CHECK(stl == 24, "STC STL=24 dwords (=192 bytes), got %u\n", stl);

    /* EOH: MNSC 0x0000 then HCRC covers bytes [4..14) */
    CHECK(frame[12] == 0 && frame[13] == 0, "MNSC 0\n");
    uint16_t hcrc_got = (uint16_t)((frame[14] << 8) | frame[15]);
    /* HCRC covers bytes [4..14): FC(4) + STC(4) + MNSC(2) = 10 bytes. */
    uint16_t hcrc_ref = crc16_fib(frame + 4, 10);
    CHECK(hcrc_got == hcrc_ref, "HCRC %04x != %04x\n", hcrc_got, hcrc_ref);

    /* FIC contents match (96 bytes starting at byte 16) */
    CHECK(memcmp(frame + 16, fics, 96) == 0, "FIC contents\n");
    /* MSC contents match (192 bytes starting at byte 112) */
    CHECK(memcmp(frame + 16 + 96, payload, 192) == 0, "MSC contents\n");

    /* EOF: MSTCRC covers [16..16+96+192) = 288 bytes of MST */
    size_t mst_end = 16 + 96 + 192;
    uint16_t mstcrc_got = (uint16_t)((frame[mst_end] << 8) | frame[mst_end + 1]);
    uint16_t mstcrc_ref = crc16_fib(frame + 16, 288);
    CHECK(mstcrc_got == mstcrc_ref, "MSTCRC %04x != %04x\n", mstcrc_got, mstcrc_ref);
    CHECK(frame[mst_end + 2] == 0xFF && frame[mst_end + 3] == 0xFF, "RFU\n");

    /* TIST all 0xFF */
    for (int i = 0; i < 4; ++i)
        CHECK(frame[mst_end + 4 + i] == 0xFF, "TIST[%d]\n", i);

    /* Odd frame inverts FSYNC */
    n = eti_builder_frame(&b, fics, payload, frame);
    CHECK(n == 312, "frame 1 size\n");
    CHECK(frame[1] == 0xB6 && frame[2] == 0x3A && frame[3] == 0x07,
          "FSYNC frame 1 %02x %02x %02x\n", frame[1], frame[2], frame[3]);
    CHECK(frame[4] == 1, "FCT frame 1\n");

    printf("OK: eti frame %zu bytes, HCRC %04x MSTCRC %04x\n",
           (size_t)312, hcrc_ref, mstcrc_ref);
    return 0;
}
