#include "mod/time_il.h"

#include <stdlib.h>
#include <string.h>

#define TIL_DEPTH 16

struct dab_time_il {
    size_t   framesize;
    int      head;          /* index of the most recently written frame */
    uint8_t *buf;           /* TIL_DEPTH * framesize bytes, row-major */
};

dab_time_il_t *dab_time_il_new(size_t framesize) {
    if (framesize == 0 || (framesize & 1u) != 0) return NULL;
    dab_time_il_t *t = (dab_time_il_t *)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->framesize = framesize;
    t->head = -1;  /* first process() will advance to 0 */
    t->buf = (uint8_t *)calloc(TIL_DEPTH, framesize);
    if (!t->buf) { free(t); return NULL; }
    return t;
}

void dab_time_il_free(dab_time_il_t *t) {
    if (!t) return;
    free(t->buf);
    free(t);
}

static inline const uint8_t *row(const dab_time_il_t *t, int delay) {
    int idx = (t->head - delay) & (TIL_DEPTH - 1);
    return t->buf + (size_t)idx * t->framesize;
}

void dab_time_il_process(dab_time_il_t *t,
                         const uint8_t *in,
                         uint8_t *out) {
    const size_t fs = t->framesize;
    t->head = (t->head + 1) & (TIL_DEPTH - 1);
    uint8_t *h0 = t->buf + (size_t)t->head * fs;
    memcpy(h0, in, fs);

    const uint8_t *h[TIL_DEPTH];
    for (int d = 0; d < TIL_DEPTH; ++d) h[d] = row(t, d);

    for (size_t i = 0; i < fs; i += 2) {
        /* Even byte: delays {0, 8, 4, 12, 2, 10, 6, 14}. */
        out[i] = (uint8_t)(
              (h[0 ][i] & 0x80)
            | (h[8 ][i] & 0x40)
            | (h[4 ][i] & 0x20)
            | (h[12][i] & 0x10)
            | (h[2 ][i] & 0x08)
            | (h[10][i] & 0x04)
            | (h[6 ][i] & 0x02)
            | (h[14][i] & 0x01));
        /* Odd byte: delays {1, 9, 5, 13, 3, 11, 7, 15}. */
        out[i + 1] = (uint8_t)(
              (h[1 ][i + 1] & 0x80)
            | (h[9 ][i + 1] & 0x40)
            | (h[5 ][i + 1] & 0x20)
            | (h[13][i + 1] & 0x10)
            | (h[3 ][i + 1] & 0x08)
            | (h[11][i + 1] & 0x04)
            | (h[7 ][i + 1] & 0x02)
            | (h[15][i + 1] & 0x01));
    }
}
