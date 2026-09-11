#include "hackrf_tx.h"

#include "common/log.h"
#include "common/ring_cf.h"

#include <hackrf.h>

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Probe                                                              */
/* ------------------------------------------------------------------ */

int hackrf_tx_probe(void) {
    int r = hackrf_init();
    if (r != HACKRF_SUCCESS) {
        LOGE("hackrf_init failed: %s", hackrf_error_name(r));
        return -1;
    }
    hackrf_device *dev = NULL;
    r = hackrf_open(&dev);
    if (r != HACKRF_SUCCESS) {
        LOGE("hackrf_open failed: %s", hackrf_error_name(r));
        hackrf_exit();
        return -1;
    }
    read_partid_serialno_t id;
    if (hackrf_board_partid_serialno_read(dev, &id) == HACKRF_SUCCESS) {
        LOGI("HackRF serial: %08x%08x%08x%08x",
             id.serial_no[0], id.serial_no[1], id.serial_no[2], id.serial_no[3]);
    }
    uint8_t board_id = 0;
    hackrf_board_id_read(dev, &board_id);
    LOGI("Board: %s", hackrf_board_id_name((enum hackrf_board_id)board_id));
    char version[128] = {0};
    hackrf_version_string_read(dev, version, sizeof(version));
    LOGI("Firmware: %s", version);
    hackrf_close(dev);
    hackrf_exit();
    return 0;
}

/* ------------------------------------------------------------------ */
/* Streaming TX                                                       */
/* ------------------------------------------------------------------ */

#define RING_CAP 524288u          /* 512 ki samples ≈ 256 ms @ 2.048 MS/s */

struct hackrf_tx {
    hackrf_device    *dev;
    ring_cf_t         ring;
    _Atomic uint64_t  samples_pushed;
    _Atomic uint64_t  samples_consumed;
    _Atomic uint64_t  underruns;
    int               started;
};

/* ofdm_symbol already applies 1/sqrt(2048).  A factor near 46 gives the
 * same ~40-count RMS level as ODR-DabMod's normalised s8 output and leaves
 * the headroom required by the OFDM crest factor. 127 clipped most samples. */
#define TX_SCALE 46.0f

static int tx_callback(hackrf_transfer *xfer) {
    hackrf_tx_t *tx = (hackrf_tx_t *)xfer->tx_ctx;
    /* HackRF expects int8 I/Q interleaved, valid_length is byte count. */
    const size_t n_pairs = (size_t)xfer->valid_length / 2u;
    int8_t *out = (int8_t *)xfer->buffer;

    /* Pull complex float from ring into a small stack scratch, convert. */
    enum { CHUNK = 2048 };
    float _Complex scratch[CHUNK];

    size_t done = 0;
    while (done < n_pairs) {
        size_t want = n_pairs - done;
        if (want > CHUNK) want = CHUNK;
        size_t got = ring_cf_read(&tx->ring, scratch, want);
        for (size_t i = 0; i < got; ++i) {
            float re = crealf(scratch[i]) * TX_SCALE;
            float im = cimagf(scratch[i]) * TX_SCALE;
            if (re >  127.0f) re =  127.0f; else if (re < -127.0f) re = -127.0f;
            if (im >  127.0f) im =  127.0f; else if (im < -127.0f) im = -127.0f;
            out[2 * (done + i) + 0] = (int8_t)lrintf(re);
            out[2 * (done + i) + 1] = (int8_t)lrintf(im);
        }
        if (got < want) {
            /* Underrun: emit zeros for the remainder and bump counter. */
            memset(out + 2 * (done + got), 0, 2 * (want - got));
            atomic_fetch_add_explicit(&tx->underruns, (want - got),
                                      memory_order_relaxed);
        }
        done += want;
    }
    atomic_fetch_add_explicit(&tx->samples_consumed, n_pairs,
                              memory_order_relaxed);
    return 0;
}

hackrf_tx_t *hackrf_tx_open_ex(uint64_t freq_hz, unsigned txvga_db, int amp_api_flag) {
    int r = hackrf_init();
    if (r != HACKRF_SUCCESS) {
        LOGE("hackrf_init: %s", hackrf_error_name(r));
        return NULL;
    }
    hackrf_tx_t *tx = (hackrf_tx_t *)calloc(1, sizeof(*tx));
    if (!tx) { hackrf_exit(); return NULL; }
    if (!ring_cf_init(&tx->ring, RING_CAP)) {
        free(tx); hackrf_exit(); return NULL;
    }

    r = hackrf_open(&tx->dev);
    if (r != HACKRF_SUCCESS) {
        LOGE("hackrf_open: %s", hackrf_error_name(r));
        goto fail;
    }
    r = hackrf_set_sample_rate(tx->dev, 2048000.0);
    if (r != HACKRF_SUCCESS) { LOGE("set_sample_rate: %s", hackrf_error_name(r)); goto fail; }
    r = hackrf_set_baseband_filter_bandwidth(tx->dev, 1750000u);
    if (r != HACKRF_SUCCESS) { LOGE("set_baseband_filter: %s", hackrf_error_name(r)); goto fail; }
    r = hackrf_set_freq(tx->dev, freq_hz);
    if (r != HACKRF_SUCCESS) { LOGE("set_freq: %s", hackrf_error_name(r)); goto fail; }
    r = hackrf_set_amp_enable(tx->dev, (uint8_t)(amp_api_flag ? 1 : 0));
    if (r != HACKRF_SUCCESS) { LOGE("set_amp: %s", hackrf_error_name(r)); goto fail; }
    if (txvga_db > 47u) txvga_db = 47u;
    r = hackrf_set_txvga_gain(tx->dev, txvga_db);
    if (r != HACKRF_SUCCESS) { LOGE("set_txvga: %s", hackrf_error_name(r)); goto fail; }

    LOGI("HackRF TX armed: %.3f MHz, txvga=%u dB, ring=%u samples",
         freq_hz / 1e6, txvga_db, RING_CAP);
    return tx;

fail:
    if (tx->dev) hackrf_close(tx->dev);
    ring_cf_free(&tx->ring);
    free(tx);
    hackrf_exit();
    return NULL;
}

hackrf_tx_t *hackrf_tx_open(uint64_t freq_hz, unsigned txvga_db) {
    return hackrf_tx_open_ex(freq_hz, txvga_db, 0);
}

int hackrf_tx_start(hackrf_tx_t *tx) {
    if (!tx || !tx->dev) return -1;
    if (tx->started) return 0;
    int r = hackrf_start_tx(tx->dev, tx_callback, tx);
    if (r != HACKRF_SUCCESS) {
        LOGE("start_tx: %s", hackrf_error_name(r));
        return -1;
    }
    tx->started = 1;
    LOGI("HackRF TX streaming");
    return 0;
}

void hackrf_tx_close(hackrf_tx_t *tx) {
    if (!tx) return;
    if (tx->dev) {
        if (tx->started) hackrf_stop_tx(tx->dev);
        hackrf_close(tx->dev);
    }
    ring_cf_free(&tx->ring);
    hackrf_exit();
    free(tx);
}

size_t hackrf_tx_writable(const hackrf_tx_t *tx) {
    return ring_cf_writable((ring_cf_t *)&tx->ring);
}

size_t hackrf_tx_push(hackrf_tx_t *tx, const float _Complex *samples, size_t n) {
    size_t w = ring_cf_write(&tx->ring, samples, n);
    atomic_fetch_add_explicit(&tx->samples_pushed, w, memory_order_relaxed);
    return w;
}

void hackrf_tx_get_stats(const hackrf_tx_t *tx, hackrf_tx_stats_t *s) {
    s->samples_pushed   = atomic_load_explicit(&tx->samples_pushed,   memory_order_relaxed);
    s->samples_consumed = atomic_load_explicit(&tx->samples_consumed, memory_order_relaxed);
    s->underruns        = atomic_load_explicit(&tx->underruns,        memory_order_relaxed);
}
