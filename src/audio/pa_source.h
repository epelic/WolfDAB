#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Print all PortAudio input devices to stdout. */
int pa_list_input_devices(void);

/*
 * PortAudio capture source.
 *
 * Opens the given device at 48 kHz, S16 stereo interleaved, and streams
 * samples into an internal SPSC ring. The PortAudio callback is the
 * producer; the caller is the single consumer via pa_source_read().
 *
 * If the device does not natively support 48 kHz we let PortAudio resample
 * (WDMKS/MME hosts on Windows tolerate this well enough for our purposes).
 */
typedef struct pa_source pa_source_t;

pa_source_t *pa_source_open(int device_index, size_t ring_samples_pow2);
void         pa_source_close(pa_source_t *src);

/* Returns number of int16 samples actually read (interleaved L/R). */
size_t pa_source_read(pa_source_t *src, int16_t *dst, size_t n_samples);

/* Snapshot counters for diagnostics. */
typedef struct {
    uint64_t frames_in;      /* audio frames delivered by PortAudio callback */
    uint64_t frames_dropped; /* frames lost because ring was full            */
    size_t   ring_fill;      /* samples currently waiting in the ring        */
    size_t   ring_capacity;
} pa_source_stats_t;
void pa_source_get_stats(const pa_source_t *src, pa_source_stats_t *out);

#ifdef __cplusplus
}
#endif
