#include "pluto_tx.h"

#include "common/log.h"
#include "common/ring_cf.h"

#include <iio.h>
#include <ad9361.h>
#include <windows.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE       2048000LL
#define RF_BANDWIDTH      1750000LL
/* Proven stable Pluto streaming geometry used by the pre-regression engine. */
#define RING_CAP          8388608u
#define IIO_BUFFER_SAMPLES 65536u
#define PLUTO_SCALE       12000.0f

struct pluto_tx {
    struct iio_context *ctx;
    struct iio_device *phy;
    struct iio_device *txdev;
    struct iio_channel *tx_cfg;
    struct iio_channel *tx_lo;
    struct iio_channel *tx_i;
    struct iio_channel *tx_q;
    struct iio_buffer *buffer;
    ring_cf_t ring;
    HANDLE worker;
    HANDLE data_ready;
    _Atomic int stop;
    _Atomic uint64_t samples_pushed;
    _Atomic uint64_t samples_consumed;
    _Atomic uint64_t underruns;
    _Atomic int failed;
    int started;
};

static void log_iio_error(const char *what, int error) {
    char text[128] = {0};
    iio_strerror(error, text, sizeof(text));
    LOGE("%s: %s (%d)", what, text, error);
}

static int is_pluto_context(struct iio_context *ctx) {
    return ctx &&
           iio_context_find_device(ctx, "ad9361-phy") &&
           iio_context_find_device(ctx, "cf-ad9361-dds-core-lpc");
}

static struct iio_context *scan_usb_contexts(void) {
    struct iio_scan_context *scan = iio_create_scan_context("usb=0456:*", 0);
    if (!scan) return NULL;
    struct iio_context_info **info = NULL;
    ssize_t count = iio_scan_context_get_info_list(scan, &info);
    struct iio_context *found = NULL;
    for (ssize_t i = 0; i < count && !found; ++i) {
        const char *uri = iio_context_info_get_uri(info[i]);
        if (!uri) continue;
        struct iio_context *candidate = iio_create_context_from_uri(uri);
        if (is_pluto_context(candidate)) {
            LOGI("PlutoSDR discovered: %s (%s)", uri,
                 iio_context_info_get_description(info[i]));
            found = candidate;
        } else if (candidate) {
            iio_context_destroy(candidate);
        }
    }
    if (info) iio_context_info_list_free(info);
    iio_scan_context_destroy(scan);
    return found;
}

static struct iio_context *open_context(void) {
    const char *override = getenv("WOLFDAB_PLUTO_URI");
    const char *uris[] = {override};
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); ++i) {
        if (!uris[i] || !*uris[i]) continue;
        struct iio_context *ctx = iio_create_context_from_uri(uris[i]);
        if (is_pluto_context(ctx)) {
            LOGI("PlutoSDR context: %s (%s)", uris[i],
                 iio_context_get_description(ctx));
            return ctx;
        }
        if (ctx) iio_context_destroy(ctx);
    }
    /* Prefer native USB streaming. It avoids the RNDIS/TCP shutdown latency
       and remains independent of the address assigned to the Pluto link. */
    struct iio_context *ctx = scan_usb_contexts();
    if (ctx) return ctx;
    const char *network_uris[] = {"ip:pluto.local", "ip:192.168.2.1"};
    for (size_t i = 0; i < sizeof(network_uris) / sizeof(network_uris[0]); ++i) {
        ctx = iio_create_context_from_uri(network_uris[i]);
        if (is_pluto_context(ctx)) {
            LOGI("PlutoSDR context: %s (%s)", network_uris[i],
                 iio_context_get_description(ctx));
            return ctx;
        }
        if (ctx) iio_context_destroy(ctx);
    }
    LOGE("PlutoSDR not found by USB or network");
    return NULL;
}

static int configure_channel(struct pluto_tx *tx, uint64_t frequency,
                             unsigned attenuation_db) {
    int r;
    if ((r = iio_channel_attr_write(tx->tx_cfg, "rf_port_select", "A")) < 0) {
        log_iio_error("Pluto TX port", r); return -1;
    }
    if ((r = iio_channel_attr_write_longlong(tx->tx_cfg, "rf_bandwidth", RF_BANDWIDTH)) < 0) {
        log_iio_error("Pluto RF bandwidth", r); return -1;
    }
    /* 2.048 MS/s is below the AD936x direct-rate floor.  libad9361 loads
       the matching interpolation FIR and then programs the exact DAB rate. */
    if ((r = ad9361_set_bb_rate(tx->phy, SAMPLE_RATE)) < 0) {
        log_iio_error("Pluto 2.048 MS/s FIR configuration", r); return -1;
    }
    /* Do not trust a successful helper call alone: the DAB waveform is only
       valid when the TX path itself confirms the exact 2.048 MHz clock. */
    long long actual_rate = 0;
    if ((r = iio_channel_attr_write_longlong(tx->tx_cfg, "sampling_frequency",
                                              SAMPLE_RATE)) < 0) {
        log_iio_error("Pluto TX sample-rate write", r); return -1;
    }
    if ((r = iio_channel_attr_read_longlong(tx->tx_cfg, "sampling_frequency",
                                             &actual_rate)) < 0) {
        log_iio_error("Pluto TX sample-rate readback", r); return -1;
    }
    if (llabs(actual_rate - SAMPLE_RATE) > 2) {
        LOGE("Pluto TX clock mismatch: requested %lld Hz, actual %lld Hz",
             (long long)SAMPLE_RATE, actual_rate);
        return -1;
    }
    LOGI("Pluto TX clock confirmed: %lld Hz", actual_rate);
    if (attenuation_db > 89u) attenuation_db = 89u;
    if ((r = iio_channel_attr_write_double(tx->tx_cfg, "hardwaregain",
                                           -(double)attenuation_db)) < 0) {
        log_iio_error("Pluto TX attenuation", r); return -1;
    }
    if ((r = iio_channel_attr_write_longlong(tx->tx_lo, "frequency",
                                              (long long)frequency)) < 0) {
        log_iio_error("Pluto LO frequency", r); return -1;
    }
    return 0;
}

int pluto_tx_probe(void) {
    struct iio_context *ctx = open_context();
    if (!ctx) return -1;
    struct iio_device *phy = iio_context_find_device(ctx, "ad9361-phy");
    struct iio_device *txdev = iio_context_find_device(ctx, "cf-ad9361-dds-core-lpc");
    if (!phy || !txdev) {
        LOGE("PlutoSDR IIO transmit devices not found");
        iio_context_destroy(ctx);
        return -1;
    }
    LOGI("PlutoSDR ready: %s", iio_context_get_description(ctx));
    iio_context_destroy(ctx);
    return 0;
}

int pluto_tx_check_dab(void) {
    struct iio_context *ctx = open_context();
    if (!ctx) return -1;
    struct iio_device *phy = iio_context_find_device(ctx, "ad9361-phy");
    struct iio_channel *lo = phy ? iio_device_find_channel(phy, "altvoltage1", true) : NULL;
    if (!lo) {
        LOGE("PlutoSDR TX LO channel not found");
        iio_context_destroy(ctx);
        return -1;
    }
    long long previous = 0;
    int have_previous = iio_channel_attr_read_longlong(lo, "frequency", &previous) == 0;
    int r = iio_channel_attr_write_longlong(lo, "frequency", 174928000LL);
    if (r == 0 && have_previous)
        iio_channel_attr_write_longlong(lo, "frequency", previous);
    if (r < 0) {
        LOGE("PlutoSDR Band III disabled: enable the AD9364 tuning range");
        iio_context_destroy(ctx);
        return 2;
    }
    LOGI("PlutoSDR Band III enabled (5A accepted)");
    iio_context_destroy(ctx);
    return 0;
}

static DWORD WINAPI tx_worker(LPVOID opaque) {
    pluto_tx_t *tx = (pluto_tx_t *)opaque;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    float _Complex *scratch = (float _Complex *)malloc(
        IIO_BUFFER_SAMPLES * sizeof(*scratch));
    if (!scratch) return 1;

    LARGE_INTEGER qpc_frequency, qpc_start;
    QueryPerformanceFrequency(&qpc_frequency);
    QueryPerformanceCounter(&qpc_start);
    uint64_t submitted = 0;

    while (!atomic_load_explicit(&tx->stop, memory_order_relaxed)) {
        while (ring_cf_readable(&tx->ring) < IIO_BUFFER_SAMPLES &&
               !atomic_load_explicit(&tx->stop, memory_order_relaxed)) {
            WaitForSingleObject(tx->data_ready, 20);
        }
        if (atomic_load_explicit(&tx->stop, memory_order_relaxed)) break;
        size_t got = ring_cf_read(&tx->ring, scratch, IIO_BUFFER_SAMPLES);
        if (got < IIO_BUFFER_SAMPLES) {
            continue;
        }

        ptrdiff_t step = iio_buffer_step(tx->buffer);
        char *out = (char *)iio_buffer_first(tx->buffer, tx->tx_i);
        char *end = (char *)iio_buffer_end(tx->buffer);
        size_t index = 0;
        for (; out < end && index < IIO_BUFFER_SAMPLES; out += step, ++index) {
            float re = crealf(scratch[index]) * PLUTO_SCALE;
            float im = cimagf(scratch[index]) * PLUTO_SCALE;
            if (re > 32767.0f) re = 32767.0f; else if (re < -32768.0f) re = -32768.0f;
            if (im > 32767.0f) im = 32767.0f; else if (im < -32768.0f) im = -32768.0f;
            ((int16_t *)out)[0] = (int16_t)lrintf(re);
            ((int16_t *)out)[1] = (int16_t)lrintf(im);
        }

        ssize_t pushed = iio_buffer_push(tx->buffer);
        if (pushed < 0) {
            if (!atomic_load_explicit(&tx->stop, memory_order_relaxed)) {
                log_iio_error("Pluto buffer push", (int)pushed);
                atomic_store_explicit(&tx->failed, 1, memory_order_relaxed);
            }
            break;
        }
        atomic_fetch_add_explicit(&tx->samples_consumed,
                                  IIO_BUFFER_SAMPLES, memory_order_relaxed);
        submitted += IIO_BUFFER_SAMPLES;

        /* Some Windows/libiio USB combinations acknowledge a push slightly
           before the Pluto has consumed the previous DMA block.  Pace the
           submission clock explicitly, while the eight kernel buffers absorb
           normal Windows scheduling jitter.  This prevents the host queue
           from running ahead and exhausting the IQ ring over time. */
        const LONGLONG deadline = qpc_start.QuadPart + (LONGLONG)(
            ((long double)submitted * (long double)qpc_frequency.QuadPart) /
            (long double)SAMPLE_RATE);
        for (;;) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            LONGLONG ticks_left = deadline - now.QuadPart;
            if (ticks_left <= 0 ||
                atomic_load_explicit(&tx->stop, memory_order_relaxed)) break;
            DWORD ms = (DWORD)((ticks_left * 1000LL) / qpc_frequency.QuadPart);
            if (ms > 1) Sleep(ms - 1);
            else SwitchToThread();
        }
    }
    free(scratch);
    return 0;
}

pluto_tx_t *pluto_tx_open_ex(uint64_t freq_hz, unsigned attenuation_db,
                             int frequency_correction_hz) {
    pluto_tx_t *tx = (pluto_tx_t *)calloc(1, sizeof(*tx));
    if (!tx || !ring_cf_init(&tx->ring, RING_CAP)) {
        free(tx); return NULL;
    }
    tx->ctx = open_context();
    if (!tx->ctx) goto fail;
    tx->data_ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!tx->data_ready) goto fail;
    tx->phy = iio_context_find_device(tx->ctx, "ad9361-phy");
    tx->txdev = iio_context_find_device(tx->ctx, "cf-ad9361-dds-core-lpc");
    if (!tx->phy || !tx->txdev) {
        LOGE("PlutoSDR transmit chain not found"); goto fail;
    }
    tx->tx_cfg = iio_device_find_channel(tx->phy, "voltage0", true);
    tx->tx_lo = iio_device_find_channel(tx->phy, "altvoltage1", true);
    tx->tx_i = iio_device_find_channel(tx->txdev, "voltage0", true);
    tx->tx_q = iio_device_find_channel(tx->txdev, "voltage1", true);
    if (!tx->tx_cfg || !tx->tx_lo || !tx->tx_i || !tx->tx_q) {
        LOGE("PlutoSDR TX channels not found"); goto fail;
    }
    int64_t corrected = (int64_t)freq_hz + (int64_t)frequency_correction_hz;
    if (corrected < 1 || configure_channel(tx, (uint64_t)corrected, attenuation_db) != 0) goto fail;
    iio_channel_enable(tx->tx_i);
    iio_channel_enable(tx->tx_q);
    /* Stable 8 x 32-ms libiio queue used by the original Pluto engine. */
    int kb = iio_device_set_kernel_buffers_count(tx->txdev, 8u);
    if (kb < 0) log_iio_error("Pluto kernel buffer count", kb);
    tx->buffer = iio_device_create_buffer(tx->txdev, IIO_BUFFER_SAMPLES, false);
    if (!tx->buffer) {
        LOGE("Cannot create PlutoSDR TX buffer"); goto fail;
    }
    LOGI("PlutoSDR TX armed: %.6f MHz (correction %+d Hz), attenuation=%u dB, 2.048 MS/s",
         corrected / 1e6, frequency_correction_hz, attenuation_db);
    return tx;

fail:
    pluto_tx_close(tx);
    return NULL;
}

pluto_tx_t *pluto_tx_open(uint64_t freq_hz, unsigned attenuation_db) {
    return pluto_tx_open_ex(freq_hz, attenuation_db, 0);
}

int pluto_tx_start(pluto_tx_t *tx) {
    if (!tx || !tx->buffer) return -1;
    if (tx->started) return 0;
    atomic_store_explicit(&tx->stop, 0, memory_order_relaxed);
    atomic_store_explicit(&tx->failed, 0, memory_order_relaxed);
    tx->worker = CreateThread(NULL, 0, tx_worker, tx, 0, NULL);
    if (!tx->worker) return -1;
    tx->started = 1;
    LOGI("PlutoSDR TX streaming");
    return 0;
}

void pluto_tx_close(pluto_tx_t *tx) {
    if (!tx) return;
    if (tx->worker) {
        atomic_store_explicit(&tx->stop, 1, memory_order_relaxed);
        if (tx->data_ready) SetEvent(tx->data_ready);
        if (tx->buffer) iio_buffer_cancel(tx->buffer);
        WaitForSingleObject(tx->worker, 5000);
        CloseHandle(tx->worker);
        tx->worker = NULL;
    }
    if (tx->buffer) iio_buffer_destroy(tx->buffer);
    if (tx->tx_i) iio_channel_disable(tx->tx_i);
    if (tx->tx_q) iio_channel_disable(tx->tx_q);
    if (tx->ctx) iio_context_destroy(tx->ctx);
    if (tx->data_ready) CloseHandle(tx->data_ready);
    ring_cf_free(&tx->ring);
    free(tx);
}

size_t pluto_tx_writable(const pluto_tx_t *tx) {
    return ring_cf_writable((ring_cf_t *)&tx->ring);
}

size_t pluto_tx_push(pluto_tx_t *tx, const float _Complex *samples, size_t n) {
    size_t written = ring_cf_write(&tx->ring, samples, n);
    atomic_fetch_add_explicit(&tx->samples_pushed, written, memory_order_relaxed);
    if (written && tx->data_ready) SetEvent(tx->data_ready);
    return written;
}

void pluto_tx_get_stats(const pluto_tx_t *tx, pluto_tx_stats_t *stats) {
    stats->samples_pushed = atomic_load_explicit(&tx->samples_pushed, memory_order_relaxed);
    stats->samples_consumed = atomic_load_explicit(&tx->samples_consumed, memory_order_relaxed);
    stats->underruns = atomic_load_explicit(&tx->underruns, memory_order_relaxed);
    stats->ring_fill = ring_cf_readable((ring_cf_t *)&tx->ring);
    stats->ring_capacity = ring_cf_capacity((ring_cf_t *)&tx->ring);
}

int pluto_tx_has_failed(const pluto_tx_t *tx) {
    return tx && atomic_load_explicit((atomic_int *)&tx->failed,
                                      memory_order_relaxed);
}
