#include "common/ring.h"
#include <stdio.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

int main(void) {
    ring_t r;
    CHECK(ring_init(&r, 16), "init 16\n");
    CHECK(ring_capacity(&r) == 16, "cap\n");
    CHECK(ring_readable(&r) == 0, "empty\n");
    CHECK(ring_writable(&r) == 16, "writable\n");

    int16_t in[10]  = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    int16_t out[16] = { 0 };

    CHECK(ring_write(&r, in, 10) == 10, "write 10\n");
    CHECK(ring_readable(&r) == 10, "readable 10\n");
    CHECK(ring_writable(&r) == 6,  "writable 6\n");

    /* read 4, write 10 more -> wraps past end of storage (indices 10..19) */
    CHECK(ring_read(&r, out, 4) == 4, "read 4\n");
    CHECK(out[0] == 1 && out[3] == 4, "read order\n");

    int16_t more[10] = { 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };
    CHECK(ring_write(&r, more, 10) == 10, "write wrap\n");
    CHECK(ring_readable(&r) == 16, "full\n");

    int16_t rest[20] = { 0 };
    CHECK(ring_read(&r, rest, 20) == 16, "drain\n");
    for (int i = 0; i < 16; ++i) {
        CHECK(rest[i] == i + 5, "seq[%d]=%d\n", i, rest[i]);
    }
    CHECK(ring_readable(&r) == 0, "drained\n");

    /* Over-read/over-write should clamp, not block or corrupt. */
    CHECK(ring_write(&r, in, 20) == 16, "clamp write\n");
    CHECK(ring_read(&r, rest, 99) == 16, "clamp read\n");

    /* Reject non-power-of-two. */
    ring_t bad;
    CHECK(!ring_init(&bad, 10), "non-pow2 rejected\n");

    ring_free(&r);
    printf("OK: ring SPSC\n");
    return 0;
}
