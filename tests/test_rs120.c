/* Round-trip test for DAB+ RS(120,110) via libfec. */
#include "fec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    /* DAB+ RS(120,110) — shortened RS(255,245), GF(2^8), poly 0x11d,
     * fcr=0, prim=1, nroots=10, pad=135. */
    void *rs = init_rs_char(8, 0x11d, 0, 1, 10, 135);
    assert(rs);

    unsigned char data[120];
    for (int i = 0; i < 110; ++i) data[i] = (unsigned char)(i * 7 + 3);
    memset(data + 110, 0, 10);

    encode_rs_char(rs, data, data + 110);

    unsigned char corrupt[120];
    memcpy(corrupt, data, sizeof(corrupt));
    corrupt[5]  ^= 0xa5;
    corrupt[42] ^= 0x3c;
    corrupt[99] ^= 0x77;
    corrupt[109] ^= 0x11;
    corrupt[115] ^= 0xfe;    /* 5 byte errors, well within t=5 capacity */

    int nerr = decode_rs_char(rs, corrupt, NULL, 0);
    assert(nerr == 5);
    assert(memcmp(corrupt, data, 120) == 0);

    free_rs_char(rs);
    printf("OK: RS(120,110) corrected %d errors\n", nerr);
    return 0;
}
