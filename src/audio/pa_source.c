#include "pa_source.h"
#include "common/log.h"
#include "common/ring.h"
#include <portaudio.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PA_SR       48000
#define PA_CHANNELS 2

/*
 * WASAPI loopback backend (see audio/wasapi_loopback.c). Its struct
 * shares this layout up to and including the ring + atomic counters, so
 * pa_source_read / pa_source_get_stats work transparently on either
 * backend. We distinguish the two at close() time via stream == NULL.
 */
pa_source_t *wasapi_loopback_open_by_name(const char *wanted_name,
                                          size_t ring_samples_pow2);
void         wasapi_loopback_close(pa_source_t *s);

struct pa_source {
    PaStream         *stream;
    ring_t            ring;
    _Atomic uint64_t  frames_in;
    _Atomic uint64_t  frames_dropped;
};

int pa_list_input_devices(void) {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        LOGE("Pa_Initialize: %s", Pa_GetErrorText(err));
        return -1;
    }
    int n = Pa_GetDeviceCount();
    if (n < 0) {
        LOGE("Pa_GetDeviceCount: %s", Pa_GetErrorText(n));
        Pa_Terminate();
        return -1;
    }
    printf("%-4s %-4s %-40s %4s %6s  %s\n", "idx", "dir", "name", "ch", "rate", "host-api");
    for (int i = 0; i < n; ++i) {
        const PaDeviceInfo *d = Pa_GetDeviceInfo(i);
        if (!d) continue;
        const PaHostApiInfo *h = Pa_GetHostApiInfo(d->hostApi);
        if (d->maxInputChannels > 0) {
            printf("%-4d %-4s %-40.40s %4d %6.0f  %s\n",
                   i, "in", d->name, d->maxInputChannels,
                   d->defaultSampleRate, h ? h->name : "?");
        } else if (d->maxOutputChannels > 0 && h && h->type == paWASAPI) {
            /* WASAPI output → can be captured via loopback in pa_source_open */
            printf("%-4d %-4s %-40.40s %4d %6.0f  %s\n",
                   i, "lb", d->name, d->maxOutputChannels,
                   d->defaultSampleRate, h ? h->name : "?");
        }
    }
    Pa_Terminate();
    return 0;
}

static int pa_callback(const void *input, void *output,
                       unsigned long frame_count,
                       const PaStreamCallbackTimeInfo *ti,
                       PaStreamCallbackFlags flags, void *user_data) {
    (void)output; (void)ti; (void)flags;
    pa_source_t *s = (pa_source_t *)user_data;
    const int16_t *in = (const int16_t *)input;
    if (!in) return paContinue;

    size_t n_samples = (size_t)frame_count * PA_CHANNELS;
    size_t written   = ring_write(&s->ring, in, n_samples);
    atomic_fetch_add_explicit(&s->frames_in, frame_count, memory_order_relaxed);
    if (written < n_samples) {
        atomic_fetch_add_explicit(&s->frames_dropped,
                                  (n_samples - written) / PA_CHANNELS,
                                  memory_order_relaxed);
    }
    return paContinue;
}

pa_source_t *pa_source_open(int device_index, size_t ring_samples_pow2) {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        LOGE("Pa_Initialize: %s", Pa_GetErrorText(err));
        return NULL;
    }

    const PaDeviceInfo *d = Pa_GetDeviceInfo(device_index);
    if (!d) {
        LOGE("device %d: no such device", device_index);
        Pa_Terminate();
        return NULL;
    }
    const PaHostApiInfo *h = Pa_GetHostApiInfo(d->hostApi);
    const int is_wasapi_output =
        d->maxInputChannels == 0 &&
        d->maxOutputChannels >= PA_CHANNELS &&
        h && h->type == paWASAPI;
    if (d->maxInputChannels < PA_CHANNELS && !is_wasapi_output) {
        LOGE("device %d: not stereo-capable (%s)", device_index, d->name);
        Pa_Terminate();
        return NULL;
    }

    if (is_wasapi_output) {
        /* Hand the capture off to the direct-COM WASAPI loopback path.
         * Snapshot the name BEFORE Pa_Terminate() — that call frees the
         * PortAudio device descriptor and any pointer we hold into it. */
        char name_copy[256];
        strncpy(name_copy, d->name, sizeof(name_copy) - 1);
        name_copy[sizeof(name_copy) - 1] = 0;
        Pa_Terminate();
        LOGI("audio: routing device %d ('%s') via WASAPI loopback",
             device_index, name_copy);
        return wasapi_loopback_open_by_name(name_copy, ring_samples_pow2);
    }

    pa_source_t *s = (pa_source_t *)calloc(1, sizeof(*s));
    if (!s) { Pa_Terminate(); return NULL; }
    if (!ring_init(&s->ring, ring_samples_pow2)) {
        LOGE("ring_init: capacity %zu is not a power of two", ring_samples_pow2);
        free(s); Pa_Terminate(); return NULL;
    }

    PaStreamParameters in_params;
    memset(&in_params, 0, sizeof(in_params));
    in_params.device                    = device_index;
    in_params.channelCount              = PA_CHANNELS;
    in_params.sampleFormat              = paInt16;
    in_params.suggestedLatency          = is_wasapi_output
                                            ? d->defaultLowOutputLatency
                                            : d->defaultLowInputLatency;
    in_params.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(&s->stream, &in_params, NULL /* no output */,
                        (double)PA_SR, paFramesPerBufferUnspecified,
                        paNoFlag, pa_callback, s);
    if (err != paNoError) {
        LOGE("Pa_OpenStream(%s): %s", d->name, Pa_GetErrorText(err));
        if (is_wasapi_output) {
            LOGE("this PortAudio build does not expose WASAPI loopback for "
                 "output devices — use a regular input device (Stereo Mix) "
                 "or route the source to a capturable endpoint.");
        }
        ring_free(&s->ring); free(s); Pa_Terminate(); return NULL;
    }
    err = Pa_StartStream(s->stream);
    if (err != paNoError) {
        LOGE("Pa_StartStream: %s", Pa_GetErrorText(err));
        Pa_CloseStream(s->stream); ring_free(&s->ring); free(s);
        Pa_Terminate(); return NULL;
    }
    LOGI("audio: opened '%s' @ %d Hz stereo (%s), ring %zu samples",
         d->name, PA_SR,
         is_wasapi_output ? "WASAPI loopback" : "input",
         ring_capacity(&s->ring));
    return s;
}

void pa_source_close(pa_source_t *s) {
    if (!s) return;
    if (!s->stream) {
        /* WASAPI loopback backend — delegate full cleanup. */
        wasapi_loopback_close(s);
        return;
    }
    Pa_StopStream(s->stream);
    Pa_CloseStream(s->stream);
    ring_free(&s->ring);
    free(s);
    Pa_Terminate();
}

size_t pa_source_read(pa_source_t *s, int16_t *dst, size_t n_samples) {
    return ring_read(&s->ring, dst, n_samples);
}

void pa_source_get_stats(const pa_source_t *s, pa_source_stats_t *out) {
    out->frames_in      = atomic_load_explicit(&s->frames_in, memory_order_relaxed);
    out->frames_dropped = atomic_load_explicit(&s->frames_dropped, memory_order_relaxed);
    out->ring_fill      = ring_readable(&s->ring);
    out->ring_capacity  = ring_capacity(&s->ring);
}
