#include "mux/cif.h"
#include <stdio.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

int main(void) {
    cif_t cif;
    cif_reset(&cif);
    for (unsigned i = 0; i < CIF_BYTES; ++i)
        CHECK(cif.data[i] == 0, "zeroed\n");

    /* 64 kbps EEP-3A audio subchannel = 24 CUs = 192 bytes per 24 ms frame,
     * placed at CU address 0. */
    uint8_t sub[192];
    for (int i = 0; i < 192; ++i) sub[i] = (uint8_t)(i ^ 0xA5);
    CHECK(cif_write_subch(&cif, 0, 24, sub) == 0, "write 24 CUs @0\n");
    CHECK(memcmp(cif.data, sub, 192) == 0, "subch bytes placed at offset 0\n");
    /* Bytes after the subchannel must still be zero. */
    for (unsigned i = 192; i < 192 + 16; ++i)
        CHECK(cif.data[i] == 0, "no spill past subch end [%u]=0x%02x\n", i, cif.data[i]);

    /* Mapping at a nonzero CU address (say 100 → byte 800). */
    uint8_t sub2[64];
    for (int i = 0; i < 64; ++i) sub2[i] = (uint8_t)(0xFE - i);
    CHECK(cif_write_subch(&cif, 100, 8, sub2) == 0, "write 8 CUs @100\n");
    CHECK(memcmp(cif.data + 800, sub2, 64) == 0, "subch bytes placed at offset 800\n");

    /* Overflow: 864 CUs is exact capacity, 865 must be rejected. */
    uint8_t dummy[8] = { 0 };
    CHECK(cif_write_subch(&cif, 863, 1, dummy) == 0, "last CU OK\n");
    CHECK(cif_write_subch(&cif, 864, 1, dummy) != 0, "past end rejected\n");
    CHECK(cif_write_subch(&cif, 863, 2, dummy) != 0, "straddle rejected\n");

    printf("OK: cif mapping\n");
    return 0;
}
