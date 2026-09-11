#pragma once
/*
 * SPSC lock-free ring buffer for complex float samples.
 *
 * Same design as ring.h (power-of-two capacity, running 64-bit indices,
 * acquire/release fences on the opposite side's index) but holds
 * float _Complex samples — one unit in the buffer is one IQ sample.
 */

#include <complex.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float _Complex   *buf;
    size_t            mask;
    _Atomic uint64_t  widx;
    _Atomic uint64_t  ridx;
} ring_cf_t;

static inline bool ring_cf_init(ring_cf_t *r, size_t capacity_pow2) {
    if (capacity_pow2 == 0 || (capacity_pow2 & (capacity_pow2 - 1)) != 0) return false;
    r->buf = (float _Complex *)calloc(capacity_pow2, sizeof(float _Complex));
    if (!r->buf) return false;
    r->mask = capacity_pow2 - 1;
    atomic_store_explicit(&r->widx, 0, memory_order_relaxed);
    atomic_store_explicit(&r->ridx, 0, memory_order_relaxed);
    return true;
}

static inline void ring_cf_free(ring_cf_t *r) { free(r->buf); r->buf = NULL; }

static inline size_t ring_cf_capacity(const ring_cf_t *r) { return r->mask + 1; }

static inline size_t ring_cf_readable(const ring_cf_t *r) {
    uint64_t w  = atomic_load_explicit(&r->widx, memory_order_acquire);
    uint64_t rr = atomic_load_explicit(&r->ridx, memory_order_relaxed);
    return (size_t)(w - rr);
}

static inline size_t ring_cf_writable(const ring_cf_t *r) {
    uint64_t w  = atomic_load_explicit(&r->widx, memory_order_relaxed);
    uint64_t rr = atomic_load_explicit(&r->ridx, memory_order_acquire);
    return ring_cf_capacity(r) - (size_t)(w - rr);
}

static inline size_t ring_cf_write(ring_cf_t *r, const float _Complex *src, size_t n) {
    size_t free_n = ring_cf_writable(r);
    if (n > free_n) n = free_n;
    uint64_t w = atomic_load_explicit(&r->widx, memory_order_relaxed);
    size_t pos = (size_t)(w & r->mask);
    size_t first = ring_cf_capacity(r) - pos;
    if (first > n) first = n;
    memcpy(r->buf + pos, src, first * sizeof(float _Complex));
    if (n > first) memcpy(r->buf, src + first, (n - first) * sizeof(float _Complex));
    atomic_store_explicit(&r->widx, w + n, memory_order_release);
    return n;
}

static inline size_t ring_cf_read(ring_cf_t *r, float _Complex *dst, size_t n) {
    size_t avail = ring_cf_readable(r);
    if (n > avail) n = avail;
    uint64_t rr = atomic_load_explicit(&r->ridx, memory_order_relaxed);
    size_t pos = (size_t)(rr & r->mask);
    size_t first = ring_cf_capacity(r) - pos;
    if (first > n) first = n;
    memcpy(dst, r->buf + pos, first * sizeof(float _Complex));
    if (n > first) memcpy(dst + first, r->buf, (n - first) * sizeof(float _Complex));
    atomic_store_explicit(&r->ridx, rr + n, memory_order_release);
    return n;
}

#ifdef __cplusplus
}
#endif
