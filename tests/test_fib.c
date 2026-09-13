#include "common/crc16.h"
#include "mux/fib.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

int main(void) {
    /* FIB CRC reference: "123456789" -> 0x29B1 XOR 0xFFFF = 0xD64E */
    uint16_t f = crc16_fib((const uint8_t *)"123456789", 9);
    CHECK(f == 0xD64E, "fib crc ref: got %04x expected D64E\n", f);

    fib_t fib;
    fib_reset(&fib);

    fig0_0_t f00 = {
        .ensemble_id    = 0xE001,
        .change_flag    = 0,
        .al_flag        = 0,
        .cif_count_high = 0,
        .cif_count_low  = 0,
    };
    CHECK(fig0_0_write(&fib, &f00) == 0, "fig0_0_write\n");
    CHECK(fib.used == 6, "fig0_0 length %zu\n", fib.used);

    /* FIG header and body inspection before finalize. */
    CHECK(fib.bytes[0] == 0x05,           /* type=0, length=5 */
          "fig header byte 0x%02x\n", fib.bytes[0]);
    CHECK(fib.bytes[1] == 0x00,           /* C/N=0 OE=0 P/D=0 ext=0 */
          "fig0_0 flags 0x%02x\n", fib.bytes[1]);
    CHECK(fib.bytes[2] == 0xE0 && fib.bytes[3] == 0x01, "EId bytes\n");
    CHECK(fib.bytes[4] == 0x00 && fib.bytes[5] == 0x00, "CIF count bytes\n");

    fib_finalize(&fib);

    fib_reset(&fib);
    fig0_17_t pty = { .service_id = 0x43E1, .pty = 10 };
    CHECK(fig0_17_write(&fib, &pty) == 0, "fig0_17_write\n");
    CHECK(fib.used == 6, "fig0_17 length %zu\n", fib.used);
    CHECK(fib.bytes[0] == 0x05 && fib.bytes[1] == 0x11,
          "fig0_17 header %02x %02x\n", fib.bytes[0], fib.bytes[1]);
    CHECK(fib.bytes[2] == 0x43 && fib.bytes[3] == 0xE1 && fib.bytes[5] == 10,
          "fig0_17 payload mismatch\n");
    fib_finalize(&fib);

    /* After finalize: bytes 6..29 are 0xFF padding, bytes 30..31 are CRC. */
    for (int i = 6; i < 30; ++i)
        CHECK(fib.bytes[i] == 0xFF, "padding[%d]=0x%02x\n", i, fib.bytes[i]);

    uint16_t crc_got = (uint16_t)((fib.bytes[30] << 8) | fib.bytes[31]);
    uint16_t crc_ref = crc16_fib(fib.bytes, FIB_PAYLOAD_CAP);
    CHECK(crc_got == crc_ref, "fib crc %04x != %04x\n", crc_got, crc_ref);

    /* Overflow guard: alloc beyond payload cap returns NULL. */
    fib_reset(&fib);
    CHECK(fib_alloc(&fib, 30) != NULL, "fill 30\n");
    CHECK(fib_alloc(&fib, 1)  == NULL, "overflow rejected\n");

    printf("OK: fib + fig0_0\n");
    return 0;
}
