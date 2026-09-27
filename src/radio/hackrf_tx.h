#pragma once
#include <complex.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One-shot probe used by --probe-hackrf: open, print board info, close. */
int hackrf_tx_probe(void);

/*
 * Streaming TX wrapper.
 *
 * Usage:
 *   hackrf_tx_t *tx = hackrf_tx_open(freq_hz, txvga_db);
 *   hackrf_tx_push(tx, prefill, hackrf_tx_writable(tx));  // optional
 *   hackrf_tx_start(tx);
 *   while (...) hackrf_tx_push(tx, samples, n);
 *   hackrf_tx_close(tx);
 *
 * open() configures the HackRF for 2.048 MS/s TX and turns off the
 * external amp but does NOT start the TX stream — that lets the caller
 * pre-fill the sample ring before the TX callback starts pulling, which
 * avoids a cold-start underrun burst (~256 ms of zeros). start() then
 * arms the TX callback that converts complex float to int8 I/Q on the
 * fly and bumps an underrun counter readable via hackrf_tx_stats() when
 * the ring runs dry.
 *
 * hackrf_tx_push() is non-blocking: it writes as many samples as fit and
 * returns the actual number. Callers are expected to back off (sleep or
 * yield) when fewer samples than requested are accepted.
 */

typedef struct hackrf_tx hackrf_tx_t;

typedef struct {
    uint64_t samples_pushed;    /* producer writes                       */
    uint64_t samples_consumed;  /* tx callback reads                     */
    uint64_t underruns;         /* tx callback fills with zeros          */
} hackrf_tx_stats_t;

hackrf_tx_t *hackrf_tx_open(uint64_t freq_hz, unsigned txvga_db);
hackrf_tx_t *hackrf_tx_open_ex(uint64_t freq_hz, unsigned txvga_db, int amp_api_flag);
int          hackrf_tx_start(hackrf_tx_t *tx);
void         hackrf_tx_close(hackrf_tx_t *tx);

size_t hackrf_tx_writable(const hackrf_tx_t *tx);
size_t hackrf_tx_push(hackrf_tx_t *tx, const float _Complex *samples, size_t n);
void   hackrf_tx_get_stats(const hackrf_tx_t *tx, hackrf_tx_stats_t *s);

#ifdef __cplusplus
}
#endif
