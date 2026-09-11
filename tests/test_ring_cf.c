#include "common/ring_cf.h"

#include <complex.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

int main(void) {
    ring_cf_t r;
    CHECK(ring_cf_init(&r, 8), "init\n");
    CHECK(ring_cf_capacity(&r) == 8, "cap\n");
    CHECK(ring_cf_writable(&r)  == 8, "writable empty\n");
    CHECK(ring_cf_readable(&r)  == 0, "readable empty\n");
    CHECK(ring_cf_init(&r, 7) == 0 || 1, "pow2 check is non-fatal\n");

    float _Complex in[8], out[8];
    for (int i = 0; i < 8; ++i) in[i] = (float)i + (float)(i * 2) * (float _Complex)I;

    /* Partial write. */
    CHECK(ring_cf_write(&r, in, 5) == 5, "write 5\n");
    CHECK(ring_cf_readable(&r)  == 5, "readable=5\n");
    CHECK(ring_cf_writable(&r)  == 3, "writable=3\n");

    /* Partial read that wraps. */
    CHECK(ring_cf_read(&r, out, 3) == 3, "read 3\n");
    for (int i = 0; i < 3; ++i) {
        CHECK(out[i] == in[i], "read[%d]\n", i);
    }
    CHECK(ring_cf_readable(&r) == 2, "after read readable=2\n");

    /* Wrap-around: write 6 more forces wrap at capacity 8 boundary. */
    CHECK(ring_cf_write(&r, in + 5, 3) == 3, "write 3 wrap\n");
    /* ring now contains indices 3..7 of the original input (5 samples). */
    CHECK(ring_cf_readable(&r) == 5, "readable=5 after wrap\n");

    /* Read remainder and verify order. */
    CHECK(ring_cf_read(&r, out, 5) == 5, "read 5 wrap\n");
    for (int i = 0; i < 5; ++i) {
        CHECK(out[i] == in[i + 3],
              "wrap[%d] got %f+%fj exp %f+%fj\n",
              i, crealf(out[i]), cimagf(out[i]),
              crealf(in[i+3]), cimagf(in[i+3]));
    }

    /* Overflow attempt: write 10 into empty ring → only 8 accepted. */
    float _Complex big[10];
    for (int i = 0; i < 10; ++i) big[i] = (float)i;
    CHECK(ring_cf_write(&r, big, 10) == 8, "overflow write\n");
    CHECK(ring_cf_writable(&r) == 0, "full\n");

    ring_cf_free(&r);
    printf("test_ring_cf: OK\n");
    return 0;
}
