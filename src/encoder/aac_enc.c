#include "aac_enc.h"
#include "common/log.h"

#include <fdk-aac/aacenc_lib.h>
#include <stdlib.h>
#include <string.h>

struct aac_enc {
    HANDLE_AACENCODER h;
    AACENC_InfoStruct info;
    int mode;
    int calls_per_sf;  /* input blocks of 960 PCM samples per channel */
    int channels;
};

static int set_param(HANDLE_AACENCODER h, AACENC_PARAM p, UINT v, const char *name) {
    if (aacEncoder_SetParam(h, p, v) != AACENC_OK) {
        LOGE("fdk-aac: SetParam(%s=%u) failed", name, v);
        return -1;
    }
    return 0;
}

aac_enc_t *aac_enc_open_ex_channels(int mode, int bitrate_bps, int sample_rate, int channels) {
    if (channels != 1 && channels != 2) return NULL;
    aac_enc_t *e = (aac_enc_t *)calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->mode = mode;
    e->channels = channels;

    /*
     * Module allocation flags for aacEncOpen:
     *   0x01 = AAC core, 0x02 = SBR, 0x04 = PS
     * Only allocate modules actually needed — allocating SBR/PS modules
     * when not used can cause AACENC_INIT_SBR_ERROR (0x42) at high bitrates.
     */
    UINT enc_modules;
    switch (mode) {
    case DABTX_AAC_MODE_DABPLUS_SBR: enc_modules = 0x01 | 0x02; break; /* AAC + SBR */
    case DABTX_AAC_MODE_DABPLUS_PS:  enc_modules = 0x01 | 0x02 | 0x04; break;
    case DABTX_AAC_MODE_DABPLUS_LC:   /* fall through */
    case DABTX_AAC_MODE_RAW_LC:       /* fall through */
    default:                          enc_modules = 0; break;
    }
    if (aacEncOpen(&e->h, enc_modules, (UINT)channels) != AACENC_OK) {
        LOGE("fdk-aac: aacEncOpen failed");
        free(e); return NULL;
    }

    int ok = 0;
    UINT aot, transmux;
    const char *mode_name;

    switch (mode) {
    case DABTX_AAC_MODE_RAW_LC:
        aot      = AOT_AAC_LC;
        transmux = TT_MP4_RAW;
        e->calls_per_sf = 6;
        mode_name = "AAC-LC raw";
        break;
    case DABTX_AAC_MODE_DABPLUS_LC:
        aot      = AOT_DABPLUS_AAC_LC;   /* 135 */
        transmux = TT_DABPLUS;           /* 13  */
        e->calls_per_sf = sample_rate / 8000;
        mode_name = "AAC-LC DAB+";
        break;
    case DABTX_AAC_MODE_DABPLUS_SBR:
        aot      = AOT_DABPLUS_SBR;      /* 136 */
        transmux = TT_DABPLUS;           /* 13  */
        /* FDK's SBR AU is 1920 samples/channel, while this wrapper feeds
         * 960-sample PCM blocks.  A DAB+ superframe therefore takes six
         * calls at 48 kHz (four at 32 kHz), just like AAC-LC. */
        e->calls_per_sf = sample_rate / 8000;
        mode_name = "HE-AACv1 DAB+";
        break;
    case DABTX_AAC_MODE_DABPLUS_PS:
        aot      = AOT_DABPLUS_PS;
        transmux = TT_DABPLUS;
        e->calls_per_sf = sample_rate / 8000;
        mode_name = "HE-AACv2 DAB+";
        break;
    default:
        LOGE("fdk-aac: unknown mode %d", mode);
        aacEncClose(&e->h); free(e); return NULL;
    }

    LOGI("fdk-aac: open modules=0x%x, aot=%u, transmux=%u, sr=%d, br=%d",
         (unsigned)enc_modules, (unsigned)aot, (unsigned)transmux,
         sample_rate, bitrate_bps);

    ok |= set_param(e->h, AACENC_AOT,            aot,               "AOT");
    ok |= set_param(e->h, AACENC_SAMPLERATE,     sample_rate,       "SAMPLERATE");
    ok |= set_param(e->h, AACENC_CHANNELMODE,    channels == 1 ? MODE_1 : MODE_2, "CHANNELMODE");
    ok |= set_param(e->h, AACENC_CHANNELORDER,   1,                 "CHANNELORDER");
    ok |= set_param(e->h, AACENC_BITRATE,        (UINT)bitrate_bps, "BITRATE");
    ok |= set_param(e->h, AACENC_TRANSMUX,       transmux,          "TRANSMUX");
    ok |= set_param(e->h, AACENC_GRANULE_LENGTH, DABTX_AAC_GRANULE, "GRANULE_LENGTH");
    ok |= set_param(e->h, AACENC_AFTERBURNER,    1,                 "AFTERBURNER");
    /* No AACENC_ANCILLARY_BITRATE — we pass numAncBytes per frame.
     * fdk-aac-dabplus with AC_DAB writes DSE before audio elements. */
    if (ok != 0) {
        LOGE("fdk-aac: SetParam chain failed (ok=%d)", ok);
        aacEncClose(&e->h); free(e); return NULL;
    }
    LOGI("fdk-aac: all SetParam OK, priming...");

    /* Prime internal state. */
    AACENC_ERROR prime_err = aacEncEncode(e->h, NULL, NULL, NULL, NULL);
    if (prime_err != AACENC_OK) {
        LOGE("fdk-aac: initial aacEncEncode failed (err=0x%x)", (unsigned)prime_err);
        aacEncClose(&e->h); free(e); return NULL;
    }
    if (aacEncInfo(e->h, &e->info) != AACENC_OK) {
        LOGE("fdk-aac: aacEncInfo failed");
        aacEncClose(&e->h); free(e); return NULL;
    }

    LOGI("aac: %s %d Hz %s, %d bps, frameLength=%u, maxOutBufBytes=%u, maxAncBytes=%u, calls/sf=%d",
         mode_name, sample_rate, channels == 1 ? "mono" : "stereo", bitrate_bps,
         (unsigned)e->info.frameLength, (unsigned)e->info.maxOutBufBytes,
         (unsigned)e->info.maxAncBytes, e->calls_per_sf);

    return e;
}

aac_enc_t *aac_enc_open_ex(int mode, int bitrate_bps, int sample_rate) {
    return aac_enc_open_ex_channels(mode, bitrate_bps, sample_rate, DABTX_AAC_CHANNELS);
}

aac_enc_t *aac_enc_open(int mode, int bitrate_bps) {
    return aac_enc_open_ex(mode, bitrate_bps, DABTX_AAC_SR);
}

void aac_enc_close(aac_enc_t *e) {
    if (!e) return;
    aacEncClose(&e->h);
    free(e);
}

int aac_enc_calls_per_sf(const aac_enc_t *e) {
    return e ? e->calls_per_sf : 6;
}

int aac_enc_is_dabplus(const aac_enc_t *e) {
    return e && (e->mode == DABTX_AAC_MODE_DABPLUS_LC ||
                 e->mode == DABTX_AAC_MODE_DABPLUS_SBR ||
                 e->mode == DABTX_AAC_MODE_DABPLUS_PS);
}

int aac_enc_frame(aac_enc_t *e,
                  const int16_t *pcm_in,
                  uint8_t *out, size_t out_cap, size_t *out_len,
                  const uint8_t *anc_data, size_t anc_len) {
    if (out_len) *out_len = 0;

    const INT in_samples = DABTX_AAC_GRANULE * e->channels;

    /* Input: audio PCM + optional ancillary (PAD) data. */
    INT   in_ids[2]    = { IN_AUDIO_DATA, IN_ANCILLRY_DATA };
    INT   in_sizes[2]  = { in_samples * (INT)sizeof(int16_t), (INT)anc_len };
    INT   in_elsizes[2]= { (INT)sizeof(int16_t), 1 };
    void *in_ptrs[2]   = { (void *)pcm_in, (void *)anc_data };

    int n_in = (anc_data && anc_len > 0) ? 2 : 1;

    INT out_id      = OUT_BITSTREAM_DATA;
    INT out_size    = (INT)out_cap;
    INT out_elsize  = 1;
    void *out_ptr   = out;

    AACENC_BufDesc inBuf  = {
        .numBufs           = n_in,
        .bufs              = in_ptrs,
        .bufferIdentifiers = in_ids,
        .bufSizes          = in_sizes,
        .bufElSizes        = in_elsizes,
    };
    AACENC_BufDesc outBuf = {
        .numBufs           = 1,
        .bufs              = &out_ptr,
        .bufferIdentifiers = &out_id,
        .bufSizes          = &out_size,
        .bufElSizes        = &out_elsize,
    };

    AACENC_InArgs  inargs  = {
        .numInSamples = in_samples,
        .numAncBytes  = (INT)anc_len,
    };
    AACENC_OutArgs outargs = { 0 };

    AACENC_ERROR err = aacEncEncode(e->h, &inBuf, &outBuf, &inargs, &outargs);
    if (err != AACENC_OK) {
        LOGE("fdk-aac: aacEncEncode err=0x%x", (unsigned)err);
        return -1;
    }
    if (out_len) *out_len = (size_t)outargs.numOutBytes;
    return 0;
}
