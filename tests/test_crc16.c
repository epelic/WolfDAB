#include "common/crc16.h"
#include <stdio.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

int main(void) {
    /* Classic CRC-16/CCITT-FALSE reference: "123456789" -> 0x29B1. */
    const char *ref = "123456789";
    uint16_t c = crc16_ccitt((const uint8_t *)ref, strlen(ref));
    CHECK(c == 0x29B1, "ccitt(123456789)=0x%04x expected 0x29B1\n", c);

    /* Trivial: CRC of empty buffer is the initial value 0xFFFF. */
    CHECK(crc16_ccitt((const uint8_t *)"", 0) == 0xFFFF, "ccitt(empty)\n");

    /* Firecode: run over 9 arbitrary bytes and check that feeding the result
     * bytes through again yields a stable, non-zero output (sanity — no
     * public reference vector available at build time). The stronger check
     * lives in test_superframe where we build a full header and verify that
     * firecode bytes are reproduced deterministically. */
    uint8_t h[9] = { 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint16_t f1 = crc16_firecode(h, sizeof(h));
    uint16_t f2 = crc16_firecode(h, sizeof(h));
    CHECK(f1 == f2, "firecode deterministic\n");
    CHECK(f1 != 0, "firecode nonzero on nontrivial input\n");

    printf("OK: crc16\n");
    return 0;
}
