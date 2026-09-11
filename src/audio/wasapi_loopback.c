/*
 * WASAPI-Loopback capture backend.
 *
 * Used when the selected source device is a WASAPI output endpoint rather
 * than a regular input. Windows provides loopback recording via
 * IAudioClient::Initialize() with AUDCLNT_STREAMFLAGS_LOOPBACK, but the
 * MSYS2 UCRT64 libportaudio doesn't expose that feature through its flag
 * set — so we talk to the COM API directly.
 *
 * We enumerate render (output) endpoints, match by friendly name against
 * the PortAudio device name, activate an IAudioClient in shared loopback
 * mode, and spin a worker thread that pulls captured buffers, converts
 * the device mix format to 48 kHz stereo int16 and feeds them into the
 * same pa_source_t ring buffer that the normal input path uses. The
 * cmd_tx producer loop then reads from the ring completely unaware of
 * whether the source is an analog microphone or a bit-perfect loopback
 * of whatever is currently playing through the speaker endpoint.
 *
 * Sample-format handling: shared-mode WASAPI virtually always delivers
 * WAVEFORMATEXTENSIBLE with KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 32-bit
 * floats, stereo (or more) at the endpoint's mix rate. We support:
 *   - IEEE float 32-bit → int16 with saturation
 *   - PCM int16 pass-through
 *   - channel count >= 2 → first two channels used (L, R)
 *   - sample rate != 48 kHz is not resampled, just reported as an error
 *     (the DAB+ pipeline requires 48 kHz input).
 */

#include "pa_source.h"
#include "common/log.h"
#include "common/ring.h"

#define COBJMACROS
/* INITGUID must be defined before including the headers so that the
 * CLSID / IID / KSDATAFORMAT_SUBTYPE_* symbols get materialized into
 * this translation unit — the mingw-w64 libuuid does not ship them. */
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <ksmedia.h>

#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WL_SR       48000
#define WL_CHANNELS 2

/* ksmedia.h does not ship this GUID in the mingw-w64 libuuid; declare a
 * local copy so the IsEqualGUID comparison below links. */
static const GUID WL_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {
    0x00000003, 0x0000, 0x0010,
    { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }
};

/*
 * The pa_source_t struct layout lives in pa_source.c — we cannot see it
 * from here. To hand captured samples into the existing ring without
 * touching the opaque struct, we instead expose a tiny set of helpers
 * from pa_source.c (see below) and feed them frame by frame.
 *
 * Actually, to avoid a circular dependency and keep the ring access
 * trivially fast, we maintain our own struct that *contains* the ring +
 * counters, and return a pa_source_t* cast of it. pa_source.c treats
 * incoming pa_source_t* as opaque, so we can make the first field(s)
 * match: { PaStream *stream = NULL; ring_t ring; atomics; }. Then the
 * existing pa_source_read / pa_source_get_stats / pa_source_close paths
 * can pick up on a `PaStream == NULL` marker and branch to our cleanup.
 *
 * This works as long as we keep the layout of this struct identical to
 * the one in pa_source.c up to and including the fields pa_source.c
 * touches, and add our own WASAPI state after that.
 */
struct pa_source {
    void             *stream;           /* always NULL for WASAPI loopback */
    ring_t            ring;
    _Atomic uint64_t  frames_in;
    _Atomic uint64_t  frames_dropped;

    /* WASAPI-specific state */
    IMMDeviceEnumerator *enumerator;
    IMMDevice           *immDevice;
    IAudioClient        *audioClient;
    IAudioCaptureClient *captureClient;
    WAVEFORMATEX        *mixFormat;
    HANDLE               event;
    HANDLE               thread;
    _Atomic int          running;
    int                  is_float;
    unsigned             channels;
    unsigned             sampleRate;
    unsigned             bytesPerFrame;
};

static int count_wasapi_outputs(void);

/* ---- small helpers for wide / narrow name conversion ------------------- */

static void wchar_to_utf8(const WCHAR *src, char *dst, size_t dst_cap) {
    int n = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, (int)dst_cap, NULL, NULL);
    if (n <= 0 && dst_cap > 0) dst[0] = 0;
}

/* Return malloced list of friendly names for all active render endpoints. */
static int enumerate_render_endpoints(IMMDeviceEnumerator *enumerator,
                                      IMMDeviceCollection **out_collection,
                                      UINT *out_count) {
    HRESULT hr = IMMDeviceEnumerator_EnumAudioEndpoints(
        enumerator, eRender, DEVICE_STATE_ACTIVE, out_collection);
    if (FAILED(hr)) {
        LOGE("EnumAudioEndpoints failed: 0x%08lx", (unsigned long)hr);
        return -1;
    }
    hr = IMMDeviceCollection_GetCount(*out_collection, out_count);
    if (FAILED(hr)) {
        LOGE("IMMDeviceCollection_GetCount: 0x%08lx", (unsigned long)hr);
        IMMDeviceCollection_Release(*out_collection);
        *out_collection = NULL;
        return -1;
    }
    return 0;
}

/* Get friendly name of an IMMDevice into utf8 buffer. */
static int immdevice_friendly_name(IMMDevice *dev, char *buf, size_t cap) {
    IPropertyStore *props = NULL;
    HRESULT hr = IMMDevice_OpenPropertyStore(dev, STGM_READ, &props);
    if (FAILED(hr)) return -1;
    PROPVARIANT v;
    PropVariantInit(&v);
    hr = IPropertyStore_GetValue(props, &PKEY_Device_FriendlyName, &v);
    if (SUCCEEDED(hr) && v.vt == VT_LPWSTR && v.pwszVal) {
        wchar_to_utf8(v.pwszVal, buf, cap);
    } else {
        buf[0] = 0;
    }
    PropVariantClear(&v);
    IPropertyStore_Release(props);
    return 0;
}

/* ---- format conversion ------------------------------------------------- */

static inline int16_t clip16(int32_t v) {
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static size_t convert_to_s16_stereo(const uint8_t *src, size_t src_frames,
                                    int16_t *dst,
                                    unsigned channels, int is_float) {
    /* Take channels 0 and 1 (L, R), drop the rest if channels > 2. */
    const unsigned frame_stride = channels;
    if (is_float) {
        const float *in = (const float *)src;
        for (size_t i = 0; i < src_frames; ++i) {
            float l = in[i * frame_stride + 0];
            float r = (channels > 1) ? in[i * frame_stride + 1] : l;
            int32_t li = (int32_t)lrintf(l * 32767.0f);
            int32_t ri = (int32_t)lrintf(r * 32767.0f);
            dst[i * 2 + 0] = clip16(li);
            dst[i * 2 + 1] = clip16(ri);
        }
    } else {
        const int16_t *in = (const int16_t *)src;
        for (size_t i = 0; i < src_frames; ++i) {
            dst[i * 2 + 0] = in[i * frame_stride + 0];
            dst[i * 2 + 1] = (channels > 1) ? in[i * frame_stride + 1]
                                            : in[i * frame_stride + 0];
        }
    }
    return src_frames;
}

/* ---- capture thread ---------------------------------------------------- */

static DWORD WINAPI capture_thread(LPVOID arg) {
    pa_source_t *s = (pa_source_t *)arg;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    HRESULT hr = IAudioClient_Start(s->audioClient);
    if (FAILED(hr)) {
        LOGE("IAudioClient_Start: 0x%08lx", (unsigned long)hr);
        CoUninitialize();
        return 1;
    }

    int16_t *scratch = NULL;
    size_t scratch_cap = 0;

    while (atomic_load(&s->running)) {
        DWORD wait = WaitForSingleObject(s->event, 200);
        if (!atomic_load(&s->running)) break;
        if (wait == WAIT_TIMEOUT) continue;

        UINT32 packetSize = 0;
        hr = IAudioCaptureClient_GetNextPacketSize(s->captureClient, &packetSize);
        while (SUCCEEDED(hr) && packetSize > 0) {
            BYTE *data = NULL;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = IAudioCaptureClient_GetBuffer(s->captureClient, &data,
                                                &frames, &flags, NULL, NULL);
            if (FAILED(hr)) break;

            size_t need = (size_t)frames * 2;
            if (scratch_cap < need) {
                free(scratch);
                scratch = (int16_t *)malloc(need * sizeof(int16_t));
                scratch_cap = need;
                if (!scratch) { scratch_cap = 0; break; }
            }

            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                memset(scratch, 0, need * sizeof(int16_t));
            } else {
                convert_to_s16_stereo(data, frames, scratch,
                                      s->channels, s->is_float);
            }

            size_t wrote = ring_write(&s->ring, scratch, need);
            atomic_fetch_add_explicit(&s->frames_in, frames, memory_order_relaxed);
            if (wrote < need) {
                atomic_fetch_add_explicit(&s->frames_dropped,
                                          (need - wrote) / 2,
                                          memory_order_relaxed);
            }

            IAudioCaptureClient_ReleaseBuffer(s->captureClient, frames);
            hr = IAudioCaptureClient_GetNextPacketSize(s->captureClient, &packetSize);
        }
    }

    IAudioClient_Stop(s->audioClient);
    free(scratch);
    CoUninitialize();
    return 0;
}

/* ---- device enumeration / list ---------------------------------------- */

int wasapi_loopback_list(void) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    int com_owned = SUCCEEDED(hr);

    IMMDeviceEnumerator *enumerator = NULL;
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator, (void **)&enumerator);
    if (FAILED(hr)) {
        LOGE("CoCreateInstance MMDeviceEnumerator: 0x%08lx", (unsigned long)hr);
        if (com_owned) CoUninitialize();
        return -1;
    }

    IMMDeviceCollection *coll = NULL;
    UINT count = 0;
    if (enumerate_render_endpoints(enumerator, &coll, &count) != 0) {
        IMMDeviceEnumerator_Release(enumerator);
        if (com_owned) CoUninitialize();
        return -1;
    }

    printf("%-4s %-50s %s\n", "lb#", "name", "default rate");
    for (UINT i = 0; i < count; ++i) {
        IMMDevice *dev = NULL;
        if (FAILED(IMMDeviceCollection_Item(coll, i, &dev))) continue;
        char name[256] = {0};
        immdevice_friendly_name(dev, name, sizeof(name));
        printf("%-4u %-50.50s wasapi loopback\n", i, name);
        IMMDevice_Release(dev);
    }

    IMMDeviceCollection_Release(coll);
    IMMDeviceEnumerator_Release(enumerator);
    if (com_owned) CoUninitialize();
    return 0;
}

/* ---- open ------------------------------------------------------------- */

pa_source_t *wasapi_loopback_open_by_name(const char *wanted_name,
                                          size_t ring_samples_pow2);

pa_source_t *wasapi_loopback_open(int lb_index, size_t ring_samples_pow2) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    int com_owned = SUCCEEDED(hr);

    pa_source_t *s = (pa_source_t *)calloc(1, sizeof(*s));
    if (!s) { if (com_owned) CoUninitialize(); return NULL; }
    s->stream = NULL;
    if (!ring_init(&s->ring, ring_samples_pow2)) {
        LOGE("ring_init: cap %zu not power of two", ring_samples_pow2);
        free(s); if (com_owned) CoUninitialize(); return NULL;
    }
    atomic_store(&s->running, 0);

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator, (void **)&s->enumerator);
    if (FAILED(hr)) { LOGE("MMDeviceEnumerator: 0x%08lx", (unsigned long)hr); goto fail; }

    IMMDeviceCollection *coll = NULL;
    UINT count = 0;
    if (enumerate_render_endpoints(s->enumerator, &coll, &count) != 0) goto fail;
    if ((UINT)lb_index >= count) {
        LOGE("lb index %d out of range (have %u)", lb_index, count);
        IMMDeviceCollection_Release(coll);
        goto fail;
    }
    hr = IMMDeviceCollection_Item(coll, (UINT)lb_index, &s->immDevice);
    IMMDeviceCollection_Release(coll);
    if (FAILED(hr)) { LOGE("IMMDeviceCollection_Item: 0x%08lx", (unsigned long)hr); goto fail; }

    char name[256] = {0};
    immdevice_friendly_name(s->immDevice, name, sizeof(name));

    hr = IMMDevice_Activate(s->immDevice, &IID_IAudioClient, CLSCTX_ALL, NULL,
                            (void **)&s->audioClient);
    if (FAILED(hr)) { LOGE("IMMDevice_Activate: 0x%08lx", (unsigned long)hr); goto fail; }

    hr = IAudioClient_GetMixFormat(s->audioClient, &s->mixFormat);
    if (FAILED(hr)) { LOGE("GetMixFormat: 0x%08lx", (unsigned long)hr); goto fail; }

    /* Detect format */
    s->channels = s->mixFormat->nChannels;
    s->sampleRate = s->mixFormat->nSamplesPerSec;
    s->bytesPerFrame = s->mixFormat->nBlockAlign;

    if (s->mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *ext = (const WAVEFORMATEXTENSIBLE *)s->mixFormat;
        s->is_float = IsEqualGUID(&ext->SubFormat, &WL_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    } else {
        s->is_float = (s->mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    }

    LOGI("wasapi-lb: '%s' mix=%u Hz ch=%u %s, ring %zu samples",
         name, s->sampleRate, s->channels,
         s->is_float ? "float32" : "int16", ring_capacity(&s->ring));

    if (s->sampleRate != WL_SR) {
        LOGE("wasapi-lb: endpoint mix rate %u Hz != required %d Hz "
             "(no resampling support)", s->sampleRate, WL_SR);
        goto fail;
    }
    if (s->channels < WL_CHANNELS) {
        LOGE("wasapi-lb: endpoint has %u channels, need at least %d",
             s->channels, WL_CHANNELS);
        goto fail;
    }

    /* 1 second buffer (100 ns units) */
    const REFERENCE_TIME hnsDur = 10000000LL;
    hr = IAudioClient_Initialize(s->audioClient,
                                 AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK |
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 hnsDur, 0, s->mixFormat, NULL);
    if (FAILED(hr)) {
        /* Some drivers reject EVENTCALLBACK on loopback — retry without it. */
        hr = IAudioClient_Initialize(s->audioClient,
                                     AUDCLNT_SHAREMODE_SHARED,
                                     AUDCLNT_STREAMFLAGS_LOOPBACK,
                                     hnsDur, 0, s->mixFormat, NULL);
        if (FAILED(hr)) {
            LOGE("IAudioClient_Initialize: 0x%08lx", (unsigned long)hr);
            goto fail;
        }
    } else {
        s->event = CreateEventW(NULL, FALSE, FALSE, NULL);
        IAudioClient_SetEventHandle(s->audioClient, s->event);
    }
    /* If no event, we still wait using a fallback event with short timeout. */
    if (!s->event) {
        s->event = CreateEventW(NULL, FALSE, FALSE, NULL);
    }

    hr = IAudioClient_GetService(s->audioClient, &IID_IAudioCaptureClient,
                                 (void **)&s->captureClient);
    if (FAILED(hr)) { LOGE("GetService(CaptureClient): 0x%08lx", (unsigned long)hr); goto fail; }

    atomic_store(&s->running, 1);
    s->thread = CreateThread(NULL, 0, capture_thread, s, 0, NULL);
    if (!s->thread) { LOGE("CreateThread failed"); atomic_store(&s->running, 0); goto fail; }

    return s;

fail:
    if (s->captureClient) IAudioCaptureClient_Release(s->captureClient);
    if (s->mixFormat)     CoTaskMemFree(s->mixFormat);
    if (s->audioClient)   IAudioClient_Release(s->audioClient);
    if (s->immDevice)     IMMDevice_Release(s->immDevice);
    if (s->enumerator)    IMMDeviceEnumerator_Release(s->enumerator);
    if (s->event)         CloseHandle(s->event);
    ring_free(&s->ring);
    free(s);
    if (com_owned) CoUninitialize();
    return NULL;
}

/* ---- close helpers (called from pa_source_close if stream == NULL) ---- */

void wasapi_loopback_close(pa_source_t *s) {
    if (!s) return;
    atomic_store(&s->running, 0);
    if (s->event) SetEvent(s->event);
    if (s->thread) {
        WaitForSingleObject(s->thread, 2000);
        CloseHandle(s->thread);
    }
    if (s->captureClient) IAudioCaptureClient_Release(s->captureClient);
    if (s->mixFormat)     CoTaskMemFree(s->mixFormat);
    if (s->audioClient)   IAudioClient_Release(s->audioClient);
    if (s->immDevice)     IMMDevice_Release(s->immDevice);
    if (s->enumerator)    IMMDeviceEnumerator_Release(s->enumerator);
    if (s->event)         CloseHandle(s->event);
    ring_free(&s->ring);
    free(s);
    CoUninitialize();
}

/* ---- open by friendly name (called from pa_source_open) --------------- */
/*
 * PortAudio lists WASAPI output endpoints with friendly names like
 * "Realtek Digital Output (2- Realtek(R) Audio)". Those names are what
 * IMMDevice::OpenPropertyStore(PKEY_Device_FriendlyName) also returns,
 * so we can identify the right endpoint without reinventing an index
 * mapping between PortAudio and COM.
 */
pa_source_t *wasapi_loopback_open_by_name(const char *wanted_name,
                                          size_t ring_samples_pow2) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    int com_owned = SUCCEEDED(hr);

    IMMDeviceEnumerator *enumerator = NULL;
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator, (void **)&enumerator);
    if (FAILED(hr)) {
        LOGE("CoCreateInstance MMDeviceEnumerator: 0x%08lx", (unsigned long)hr);
        if (com_owned) CoUninitialize();
        return NULL;
    }

    IMMDeviceCollection *coll = NULL;
    UINT count = 0;
    if (enumerate_render_endpoints(enumerator, &coll, &count) != 0) {
        IMMDeviceEnumerator_Release(enumerator);
        if (com_owned) CoUninitialize();
        return NULL;
    }

    /* Match by prefix so that PortAudio name truncation (often 40 chars)
     * does not break identification. */
    const size_t wanted_len = strlen(wanted_name);
    int matched = -1;
    for (UINT i = 0; i < count; ++i) {
        IMMDevice *dev = NULL;
        if (FAILED(IMMDeviceCollection_Item(coll, i, &dev))) continue;
        char name[256] = {0};
        immdevice_friendly_name(dev, name, sizeof(name));
        IMMDevice_Release(dev);
        /* Accept exact prefix match or the reverse (full COM name starts
         * with the truncated PortAudio name). */
        if (strncmp(name, wanted_name, wanted_len) == 0) { matched = (int)i; break; }
    }
    IMMDeviceCollection_Release(coll);
    IMMDeviceEnumerator_Release(enumerator);
    if (com_owned) CoUninitialize();

    if (matched < 0) {
        LOGE("wasapi-lb: no render endpoint matches '%s'", wanted_name);
        return NULL;
    }
    return wasapi_loopback_open(matched, ring_samples_pow2);
}

/* Suppress unused helper warning */
static int count_wasapi_outputs(void) { return 0; }
