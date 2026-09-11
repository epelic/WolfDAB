#pragma once
/*
 * Single-producer / single-consumer lock-free ring buffer for 16-bit samples.
 *
 * Capacity is fixed at construction and MUST be a power of two; this lets the
 * read/write indices run freely modulo 2^64 and we mask only on access, which
 * avoids the "full vs empty" ambiguity of mod-N ring counters.
 *
 * Safe to call ring_write() from one thread and ring_read() from another
 * without locks. Both sides use acquire/release fences on the opposite side's
 * index. No memory allocation inside read/write.
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t          *buf;
    size_t            mask;   /* capacity - 1, capacity must be power of two */
    _Atomic uint64_t  widx;   /* producer writes, consumer reads (acquire)   */
    _Atomic uint64_t  ridx;   /* consumer writes, producer reads (acquire)   */
} ring_t;

static inline bool ring_init(ring_t *r, size_t capacity_pow2) {
    if (capacity_pow2 == 0 || (capacity_pow2 & (capacity_pow2 - 1)) != 0) return false;
    r->buf = (int16_t *)calloc(capacity_pow2, sizeof(int16_t));
    if (!r->buf) return false;
    r->mask = capacity_pow2 - 1;
    atomic_store_explicit(&r->widx, 0, memory_order_relaxed);
    atomic_store_explicit(&r->ridx, 0, memory_order_relaxed);
    return true;
}

static inline void ring_free(ring_t *r) {
    free(r->buf);
    r->buf = NULL;
}

static inline size_t ring_capacity(const ring_t *r) { return r->mask + 1; }

static inline size_t ring_readable(const ring_t *r) {
    uint64_t w = atomic_load_explicit(&r->widx, memory_order_acquire);
    uint64_t rr = atomic_load_explicit(&r->ridx, memory_order_relaxed);
    return (size_t)(w - rr);
}

static inline size_t ring_writable(const ring_t *r) {
    uint64_t w = atomic_load_explicit(&r->widx, memory_order_relaxed);
    uint64_t rr = atomic_load_explicit(&r->ridx, memory_order_acquire);
    return ring_capacity(r) - (size_t)(w - rr);
}

/* Producer side. Returns number of samples actually written. */
static inline size_t ring_write(ring_t *r, const int16_t *src, size_t n) {
    size_t free_n = ring_writable(r);
    if (n > free_n) n = free_n;
    uint64_t w = atomic_load_explicit(&r->widx, memory_order_relaxed);
    size_t pos = (size_t)(w & r->mask);
    size_t first = ring_capacity(r) - pos;
    if (first > n) first = n;
    memcpy(r->buf + pos, src, first * sizeof(int16_t));
    if (n > first) memcpy(r->buf, src + first, (n - first) * sizeof(int16_t));
    atomic_store_explicit(&r->widx, w + n, memory_order_release);
    return n;
}

/* Consumer side. Returns number of samples actually read. */
static inline size_t ring_read(ring_t *r, int16_t *dst, size_t n) {
    size_t avail = ring_readable(r);
    if (n > avail) n = avail;
    uint64_t rr = atomic_load_explicit(&r->ridx, memory_order_relaxed);
    size_t pos = (size_t)(rr & r->mask);
    size_t first = ring_capacity(r) - pos;
    if (first > n) first = n;
    memcpy(dst, r->buf + pos, first * sizeof(int16_t));
    if (n > first) memcpy(dst + first, r->buf, (n - first) * sizeof(int16_t));
    atomic_store_explicit(&r->ridx, rr + n, memory_order_release);
    return n;
}

#ifdef __cplusplus
}
#endif
