#include "crc16.h"

/* --- CRC-16-CCITT-FALSE (poly 0x1021) ------------------------------------ */

static uint16_t ccitt_table[256];
static int      ccitt_ready = 0;

static void ccitt_init(void) {
    for (int i = 0; i < 256; ++i) {
        uint16_t c = (uint16_t)(i << 8);
        for (int b = 0; b < 8; ++b)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
        ccitt_table[i] = c;
    }
    ccitt_ready = 1;
}

uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
    if (!ccitt_ready) ccitt_init();
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = (uint16_t)((crc << 8) ^ ccitt_table[(crc >> 8) ^ data[i]]);
    }
    return crc;
}

uint16_t crc16_fib(const uint8_t *data, size_t len) {
    return (uint16_t)(crc16_ccitt(data, len) ^ 0xFFFF);
}

/* --- DAB Fire code (poly 0x782F) ----------------------------------------- */

static uint16_t fire_table[256];
static int      fire_ready = 0;

static void fire_init(void) {
    for (int i = 0; i < 256; ++i) {
        uint16_t c = (uint16_t)(i << 8);
        for (int b = 0; b < 8; ++b)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x782F) : (uint16_t)(c << 1);
        fire_table[i] = c;
    }
    fire_ready = 1;
}

uint16_t crc16_firecode(const uint8_t *data, size_t len) {
    /* DAB+ Fire code: generator 0x782F (implicit top bit x^16), init=0,
     * no final XOR, MSB-first. Invariant: running this CRC over
     * (data || parity) yields 0 — which is exactly how qt-dab's
     * firecodeChecker validates the superframe header. */
    if (!fire_ready) fire_init();
    uint16_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc = (uint16_t)((crc << 8) ^ fire_table[(crc >> 8) ^ data[i]]);
    }
    return crc;
}
