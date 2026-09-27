#pragma once
/*
 * Tiny MSB-first bit writer. DAB bitstreams pack fields big-endian, highest
 * bit first, which is what write_bits() does here. No bounds checking beyond
 * a caller-supplied capacity.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *buf;      /* byte buffer, must be zero-initialised by caller */
    size_t   bit_cap;  /* total capacity in bits */
    size_t   bit_pos;  /* next bit offset from byte 0, MSB-first */
} bitbuf_t;

static inline void bitbuf_init(bitbuf_t *b, uint8_t *storage, size_t byte_cap) {
    b->buf     = storage;
    b->bit_cap = byte_cap * 8;
    b->bit_pos = 0;
    memset(storage, 0, byte_cap);
}

/* Write the low `nbits` of `value` MSB-first. Returns 0 on success, -1 on overflow. */
static inline int bitbuf_put(bitbuf_t *b, uint32_t value, unsigned nbits) {
    if (b->bit_pos + nbits > b->bit_cap) return -1;
    for (unsigned i = 0; i < nbits; ++i) {
        unsigned bit = (value >> (nbits - 1 - i)) & 1u;
        size_t   p   = b->bit_pos + i;
        b->buf[p >> 3] |= (uint8_t)(bit << (7 - (p & 7)));
    }
    b->bit_pos += nbits;
    return 0;
}

/* Align to next byte boundary by writing zero bits. */
static inline int bitbuf_align_byte(bitbuf_t *b) {
    unsigned pad = (8u - (unsigned)(b->bit_pos & 7u)) & 7u;
    return bitbuf_put(b, 0, pad);
}

static inline size_t bitbuf_bytes(const bitbuf_t *b) {
    return (b->bit_pos + 7u) / 8u;
}

#ifdef __cplusplus
}
#endif
