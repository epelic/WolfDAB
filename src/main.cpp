#include "audio/pa_source.h"
#include "audio/source.h"
#include "common/config.h"
#include "common/crc16.h"
#include "common/log.h"
#include "dabplus/superframe.h"
#include "encoder/aac_enc.h"
#include "mod/fic_code.h"
#include "mod/frame.h"
#include "mod/msc_code.h"
#include "mux/eti.h"
#include "mux/fib.h"
#include "mux/eep_profile.h"
#include "pad/dls.h"
#include "pad/epg.h"
#include "pad/mot.h"
#include "pad/pad_inject.h"
#include "pad/pad_sched.h"
#include "radio/channels.h"
#include "radio/hackrf_tx.h"

#include <array>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cmath>
#include <complex>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <string>
#include <thread>
#include <ctime>
#include <vector>
#include <windows.h>
#include <twolame.h>

/* --- Signal handling ---------------------------------------------------- */

static volatile std::sig_atomic_t g_stop = 0;
static HANDLE g_stop_event = NULL;

static void sigint_handler(int /*sig*/) {
    g_stop = 1;
}

static bool stop_requested() {
    return g_stop || (g_stop_event && WaitForSingleObject(g_stop_event, 0) == WAIT_OBJECT_0);
}

/* Pad or truncate a string to exactly 16 bytes (DAB label size). */
static void dab_label_pad(const char *src, char out[17]) {
    std::memset(out, ' ', 16);
    out[16] = '\0';
    if (src) {
        size_t len = std::strlen(src);
        if (len > 16) len = 16;
        std::memcpy(out, src, len);
    }
}

static void usage(const char *argv0) {
    std::fprintf(stderr,
        "dabtx — DAB+ transmitter for HackRF One\n"
        "Usage:\n"
        "  %s --list-audio\n"
        "  %s --list-channels\n"
        "  %s --probe-hackrf\n"
        "  %s --capture-test <device_idx> <seconds>\n"
        "        Open device, print peak/RMS every 200 ms, show drops.\n"
        "  %s --encode-test  <device_idx> <seconds> <out.aac>\n"
        "        Capture audio, encode with libfdk-aac, dump raw AUs to file.\n"
        "  %s --eti-test     <device_idx> <seconds> <out.eti>\n"
        "        Capture → AAC → DAB+ superframe → ETI(NI). Dumps one ETI\n"
        "        logical frame (312 bytes) every 24 ms to file.\n"
        "  %s --tx-zero      <channel> <seconds> [txvga_db]\n"
        "        Send zero-amplitude IQ at the channel frequency to smoke\n"
        "        test the HackRF TX path. Dummy load / attenuator required.\n"
        "  %s --tx           <channel> <audio_idx> <seconds> [txvga_db]\n"
        "        Full TX: live audio → HE-AAC v1 → DAB+ → Mode I → HackRF.\n"
        "        Dummy load / attenuator required.\n"
        "  %s --tx-file      <channel> <audio_idx> <seconds> <out.raw>\n"
        "        Same pipeline as --tx but writes osmocom-style 8-bit\n"
        "        unsigned IQ to a file instead of touching the HackRF.\n"
        "        Replayable in qt-dab / welle.io for offline debugging.\n"
        "\n"
        "  Optional (before any command):\n"
        "    --ensemble <label>   Ensemble name, max 16 chars (default: dabtx Ensemble)\n"
        "    --service  <label>   Service 1 name, max 16 chars (default: dabtx Service)\n"
        "    --audio2   <idx>     Audio device for service 2 (enables dual-service MUX)\n"
        "    --source  <spec>    device:N | tone:Hz,dB | file:path | stream:http(s)://...\n"
        "    --source2 <spec>    Source for service 2 (enables dual-service MUX)\n"
        "    --stop-event <name> Internal graceful-stop event used by WolfDAB GUI\n"
        "    --service2 <label>   Service 2 name, max 16 chars (default: spaces)\n"
        "    --dls      <text>    DLS scrolling text for service 1 (default: station info)\n"
    "    --dls2     <text>    DLS scrolling text for service 2\n"
    "    --slide    <file>    MOT SlideShow image (JPEG/PNG, max 32 KB)\n"
    "    --config   <file>    Config file (key=value, default: dabtx.cfg)\n"
    "\n"
    "  Config file keys (same names as CLI flags without --):\n"
    "    ensemble, service, service2, audio2, dls, dls2, slide\n",
        argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0);
}

static int cmd_list_channels() {
    std::size_t n = 0;
    const dab_channel_t *ch = dab_channels(&n);
    std::printf("%-4s  %-9s\n", "ch", "MHz");
    for (std::size_t i = 0; i < n; ++i) {
        std::printf("%-4s  %9.3f\n", ch[i].label, ch[i].freq_hz / 1e6);
    }
    return 0;
}

static void level_meter(const int16_t *samples, size_t n, int &peak, double &rms) {
    int64_t sum_sq = 0;
    int p = 0;
    for (size_t i = 0; i < n; ++i) {
        int v = samples[i];
        if (v < 0) v = -v;
        if (v > p) p = v;
        sum_sq += (int64_t)samples[i] * samples[i];
    }
    peak = p;
    rms  = n ? std::sqrt((double)sum_sq / (double)n) : 0.0;
}

static int cmd_capture_test(int dev, int seconds) {
    pa_source_t *src = pa_source_open(dev, 1u << 16);  /* 65 536 samples ≈ 680 ms stereo */
    if (!src) return 1;

    const size_t block = DABTX_AAC_GRANULE * DABTX_AAC_CHANNELS;  /* 1920 */
    std::vector<int16_t> buf(block);

    auto t_start = std::chrono::steady_clock::now();
    auto t_next  = t_start + std::chrono::milliseconds(200);
    int    peak_acc = 0;
    double rms_acc  = 0.0;
    size_t rms_n    = 0;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (now - t_start >= std::chrono::seconds(seconds)) break;

        size_t got = pa_source_read(src, buf.data(), buf.size());
        if (got == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        int    p  = 0;
        double r2 = 0.0;
        level_meter(buf.data(), got, p, r2);
        if (p > peak_acc) peak_acc = p;
        rms_acc += r2 * r2 * got;
        rms_n   += got;

        if (now >= t_next) {
            double rms = rms_n ? std::sqrt(rms_acc / rms_n) : 0.0;
            pa_source_stats_t st;
            pa_source_get_stats(src, &st);
            std::fprintf(stderr,
                "[cap] peak=%5d rms=%6.0f  ring=%5zu/%5zu  in=%8llu drop=%llu\n",
                peak_acc, rms, st.ring_fill, st.ring_capacity,
                (unsigned long long)st.frames_in,
                (unsigned long long)st.frames_dropped);
            peak_acc = 0; rms_acc = 0.0; rms_n = 0;
            t_next += std::chrono::milliseconds(200);
        }
    }
    pa_source_close(src);
    return 0;
}

static int cmd_encode_test(int dev, int seconds, const char *out_path) {
    pa_source_t *src = pa_source_open(dev, 1u << 17);  /* ≈ 1.3 s of stereo */
    if (!src) return 1;
    aac_enc_t *enc = aac_enc_open(DABTX_AAC_MODE_RAW_LC, 64000);
    if (!enc) { pa_source_close(src); return 1; }

    FILE *fp = std::fopen(out_path, "wb");
    if (!fp) { LOGE("fopen %s: %s", out_path, std::strerror(errno));
               aac_enc_close(enc); pa_source_close(src); return 1; }

    const size_t block = DABTX_AAC_GRANULE * DABTX_AAC_CHANNELS;
    std::vector<int16_t> pcm(block);
    std::vector<uint8_t> aac(8192);

    auto t_start = std::chrono::steady_clock::now();
    size_t frames = 0, bytes = 0;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (now - t_start >= std::chrono::seconds(seconds)) break;

        size_t got = 0;
        while (got < block) {
            size_t r = pa_source_read(src, pcm.data() + got, block - got);
            if (r == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            got += r;
        }
        size_t out_len = 0;
        if (aac_enc_frame(enc, pcm.data(), aac.data(), aac.size(), &out_len, NULL, 0) != 0) break;
        if (out_len > 0) {
            std::fwrite(aac.data(), 1, out_len, fp);
            bytes += out_len;
        }
        ++frames;
    }

    std::fclose(fp);
    aac_enc_close(enc);

    pa_source_stats_t st;
    pa_source_get_stats(src, &st);
    pa_source_close(src);

    LOGI("aac: %zu AUs, %zu bytes → %s (%.1f kbps avg, %llu drops)",
         frames, bytes, out_path,
         bytes * 8.0 / 1000.0 / (double)seconds,
         (unsigned long long)st.frames_dropped);
    return 0;
}

/* Per-service descriptor for the FIC builder. */
struct dab_svc_desc {
    uint16_t    service_id;
    uint8_t     sub_ch_id;         /* 1..63 */
    uint8_t     sc_ids;            /* short component ID, 0..15 */
    uint16_t    start_addr_cu;     /* CU offset in CIF */
    uint16_t    sub_ch_size_cus;   /* CU count (bitrate * 6 / 8) */
    dab_eep_profile_t eep_profile;
    const char *label;             /* exactly 16 chars, space-padded */
    uint16_t short_label_flag;
    bool mot_slideshow;
    uint8_t pty;
    uint8_t asc_type;
};

static uint16_t short_label_flag(const char *label,const char *short_label){
    if(!short_label||!*short_label)return 0;
    uint16_t flag=0;int from=0,count=0;
    for(const char*p=short_label;*p&&count<8;++p){for(int i=from;i<16;++i)if(label[i]==*p){flag|=(uint16_t)(0x8000u>>i);from=i+1;++count;break;}}
    return flag;
}

static void build_fic_frame(uint8_t *fics /* 96 bytes */, uint32_t frame_count,
                            uint16_t ensemble_id, uint8_t ecc, const char *ens_label,
                            const dab_svc_desc *svcs, int n_svcs) {
    fib_t fib; int page=(int)(frame_count%4u), first=page*4;
    fib_reset(&fib);
    fig0_0_t f00={.ensemble_id=ensemble_id,.change_flag=0,.al_flag=0,.cif_count_high=(uint8_t)((frame_count/250u)%20u),.cif_count_low=(uint8_t)(frame_count%250u)};
    fig0_0_write(&fib,&f00);
    for(int i=first;i<n_svcs&&i<first+4;++i){unsigned option=0,level=0;dab_eep_fig01_fields(svcs[i].eep_profile,&option,&level);fig0_1_eep_t f={.sub_ch_id=svcs[i].sub_ch_id,.start_addr=svcs[i].start_addr_cu,.eep_option=option,.prot_level=level,.sub_ch_size_cus=svcs[i].sub_ch_size_cus};fig0_1_eep_write(&fib,&f);}
    fib_finalize(&fib);std::memcpy(fics,fib.bytes,32);
    fib_reset(&fib);
    for(int i=first;i<n_svcs&&i<first+4;++i){fig0_2_audio_t f={.service_id=svcs[i].service_id,.ca_id=0,.local_flag=0,.asc_type=svcs[i].asc_type,.sub_ch_id=svcs[i].sub_ch_id};fig0_2_audio_write(&fib,&f);}
    fib_finalize(&fib);std::memcpy(fics+32,fib.bytes,32);
    fib_reset(&fib);
    const unsigned carousel=(unsigned)(frame_count%5u);
    if(carousel==0){
        fig0_9_write(&fib,ecc);
        for(int i=first;i<n_svcs&&i<first+4;++i){fig0_8_audio_t f={.service_id=svcs[i].service_id,.sc_ids=svcs[i].sc_ids,.sub_ch_id=svcs[i].sub_ch_id};fig0_8_audio_write(&fib,&f);}
    }
    else if(carousel==1){
        fig0_9_write(&fib,ecc);
        int slot=(int)(((frame_count/4u)%(unsigned)(n_svcs+1)));
        if(slot==0)fig1_0_write(&fib,ensemble_id,ens_label,0);else fig1_1_write(&fib,svcs[slot-1].service_id,svcs[slot-1].label,svcs[slot-1].short_label_flag);
    }
    else if(carousel==2) {
        /* TS 101 499 requires MOT SlideShow to be announced in FIG 0/13.
         * Audio X-PAD application data: AppTy=12, DG=0, DSCTy=60 (MOT). */
        int mot_count=0;for(int i=0;i<n_svcs;++i)if(svcs[i].mot_slideshow)++mot_count;
        if(mot_count==0)fig0_9_write(&fib,ecc);
        else {
            int page13=(int)((frame_count/4u)%((unsigned)(mot_count+2)/3u));int skip=page13*3,written=0;
            for(int i=0;i<n_svcs&&written<3;++i)if(svcs[i].mot_slideshow){if(skip){--skip;continue;}fig0_13_app_t f={.service_id=svcs[i].service_id,.sc_ids=svcs[i].sc_ids,.ua_type=0x002,.ua_data={0x0C,0x3C},.ua_data_len=2};if(fig0_13_write(&fib,&f)==0)++written;}
        }
    }
    else if(carousel==3) {
        int first17=(int)(((frame_count/4u)%((unsigned)(n_svcs+4)/5u))*5u);
        for(int i=first17;i<n_svcs&&i<first17+5;++i){fig0_17_t f={.service_id=svcs[i].service_id,.pty=svcs[i].pty};fig0_17_write(&fib,&f);}
    }
    else {
        std::time_t now=std::time(nullptr);std::tm utc{};gmtime_s(&utc,&now);
        fig0_10_t f={.mjd=(uint32_t)(now/86400+40587),.hour=(uint8_t)utc.tm_hour,.minute=(uint8_t)utc.tm_min};
        fig0_10_write(&fib,&f);
    }
    fib_finalize(&fib);std::memcpy(fics+64,fib.bytes,32);
}

/* Single-service convenience wrapper (used by eti-test). */
static void build_fic_frame_single(uint8_t *fics, uint32_t frame_count,
                                   uint16_t sub_ch_size_cus,
                                   const char *ens_label, const char *svc_label) {
    dab_svc_desc svc = {
        .service_id     = 0xE001,
        .sub_ch_id      = 1,
        .start_addr_cu  = 0,
        .sub_ch_size_cus = sub_ch_size_cus,
        .eep_profile    = DAB_EEP_3A,
        .label          = svc_label,
    };
    build_fic_frame(fics, frame_count, 0xE001, 0xE0, ens_label, &svc, 1);
}

static int cmd_eti_test(int dev, int seconds, const char *out_path) {
    /*
     * DAB+ RS(120,110), 11-byte header and 2-byte AU CRCs reduce the usable
     * AAC bitrate below the nominal subchannel rate. For a 128 kbps EEP-3A
     * subchannel the useful AAC budget is 1737 bytes per superframe, which
     * is only 115.8 kbps — and fdk-aac's per-AU burstiness can briefly
     * exceed that. Giving the encoder 96 kbps leaves comfortable headroom.
     */
    /* AAC-LC 48 kHz stereo at 64 kbps in a 128 kbps EEP-3A subchannel.
     * The 2:1 budget leaves plenty of headroom for fdk-aac bit-reservoir
     * bursts so the last-AU-fits-in-data-area check never trips. */
    const int bitrate_kbps  = 128;
    const int aac_kbps      = 64;
    pa_source_t *src = pa_source_open(dev, 1u << 17);
    if (!src) return 1;
    aac_enc_t *enc = aac_enc_open(DABTX_AAC_MODE_RAW_LC, aac_kbps * 1000);
    if (!enc) { pa_source_close(src); return 1; }

    static const int AU_PER_SF = 6;  /* AAC-LC: 6 AUs per 120 ms superframe */

    dabplus_cfg_t sf_cfg = {
        .bitrate_kbps     = bitrate_kbps,
        .num_aus          = AU_PER_SF,
        .dac_rate         = 1,
        .sbr_flag         = 0,
        .aac_channel_mode = 1,
        .ps_flag          = 0,
        .mpeg_surround    = 0,
    };
    dabplus_sf_t *sf = dabplus_sf_new(&sf_cfg);
    if (!sf) { aac_enc_close(enc); pa_source_close(src); return 1; }

    const uint16_t bytes_per_frame = (uint16_t)(bitrate_kbps * 3); /* 384 for 128 kbps */
    eti_subch_t esub = {
        .sub_ch_id      = 1,
        .start_addr_cus = 0,
        .tpl            = 0x22,     /* EEP-A, level 2 → EEP-3A */
        .size_bytes     = bytes_per_frame,
    };
    eti_builder_t eti;
    eti_builder_init(&eti, &esub);

    FILE *fp = std::fopen(out_path, "wb");
    if (!fp) {
        LOGE("fopen %s: %s", out_path, std::strerror(errno));
        dabplus_sf_free(sf); aac_enc_close(enc); pa_source_close(src);
        return 1;
    }

    const size_t block = DABTX_AAC_GRANULE * DABTX_AAC_CHANNELS;  /* 1920 */
    std::vector<int16_t> pcm(block);
    std::vector<uint8_t> au_bufs[AU_PER_SF];
    const uint8_t *au_ptrs[AU_PER_SF];
    size_t au_sizes[AU_PER_SF];
    for (int i = 0; i < AU_PER_SF; ++i) au_bufs[i].resize(1536);

    std::vector<uint8_t> sf_bytes(bitrate_kbps * 15);  /* 1920 for 128 kbps */
    uint8_t fics[96];
    uint8_t eti_frame[ETI_MAX_FRAME];

    auto t_start = std::chrono::steady_clock::now();
    size_t sf_count = 0, eti_count = 0, total_bytes = 0;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (now - t_start >= std::chrono::seconds(seconds)) break;

        /* Produce 6 AUs. */
        for (int a = 0; a < AU_PER_SF; ++a) {
            size_t got = 0;
            while (got < block) {
                size_t r = pa_source_read(src, pcm.data() + got, block - got);
                if (r == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                got += r;
            }
            size_t out_len = 0;
            if (aac_enc_frame(enc, pcm.data(),
                              au_bufs[a].data(), au_bufs[a].size(), &out_len, NULL, 0) != 0) {
                LOGE("encoder error");
                goto done;
            }
            au_sizes[a] = out_len;
            au_ptrs[a]  = au_bufs[a].data();
            if (out_len == 0) { a--; continue; } /* fdk-aac priming frames */
        }

        if (dabplus_sf_build(sf, au_ptrs, au_sizes, sf_bytes.data(), sf_bytes.size()) != 0) {
            LOGE("superframe build failed");
            break;
        }
        sf_count++;

        /* Slice the superframe into 5 per-frame ETI payloads. */
        for (int k = 0; k < 5; ++k) {
            build_fic_frame_single(fics, eti.current_frame, 96,
                                   "dabtx Ensemble  ", "dabtx Service   ");
            size_t n = eti_builder_frame(&eti, fics,
                                         sf_bytes.data() + k * bytes_per_frame,
                                         eti_frame);
            if (n == 0) { LOGE("eti build failed"); goto done; }
            std::fwrite(eti_frame, 1, n, fp);
            total_bytes += n;
            eti_count++;
        }
    }
done:
    std::fclose(fp);
    dabplus_sf_free(sf);
    aac_enc_close(enc);
    pa_source_close(src);

    LOGI("eti: %zu superframes, %zu ETI frames, %zu bytes → %s",
         sf_count, eti_count, total_bytes, out_path);
    return 0;
}

/*
 * Per-service audio pipeline: audio source → encoder → superframe → CIF queue.
 * One instance per service in the MUX.
 */
struct svc_pipe {
    wolfdab_source_t *src;
    aac_enc_t      *enc;
    dabplus_sf_t   *sf;
    dab_msc_subch_t *subch;
    pad_sched_t    *pad;           /* PAD scheduler (DLS + MOT) */
    dab_svc_desc    desc;

    size_t          bytes_per_cif;
    size_t          sf_data_size;
    std::vector<uint8_t> sf_raw;
    std::vector<uint8_t> sf_bytes;
    std::deque<std::array<uint8_t, 2048>> cif_queue;
    uint64_t        sf_produced;
    std::string     last_metadata;
    std::string     mot_folder;
    unsigned        mot_interval{10};
    int             channels{2};
    twolame_options *mp2{};
    std::vector<uint8_t> mp2_pending;
    unsigned mp2_cifs_per_frame{1};
    std::vector<std::filesystem::path> mot_files;
    size_t          mot_index{};
    uint64_t        mot_fingerprint{};
    uint16_t        mot_transport_id{10};
    std::chrono::steady_clock::time_point mot_next_scan{}, mot_next_slide{};
};

static std::wstring wide_arg(const char *s) {
    if (!s) return {};
    int n=MultiByteToWideChar(CP_ACP,0,s,-1,nullptr,0); std::wstring w(n? n:0,L'\0');
    if(n>1){MultiByteToWideChar(CP_ACP,0,s,-1,w.data(),n);w.resize((size_t)n-1);} return w;
}

static wolfdab_source_t *open_source_spec(const char *spec, int sample_rate) {
    if (!spec || std::strncmp(spec,"device:",7)==0) return wolfdab_source_open_device_ex(spec?std::atoi(spec+7):0,1u<<17,sample_rate);
    if (std::strncmp(spec,"tone:",5)==0) { double hz=1000.0,db=-18.0;std::sscanf(spec+5,"%lf,%lf",&hz,&db);return wolfdab_source_open_tone_ex(hz,db,sample_rate); }
    const char *value=nullptr;if(std::strncmp(spec,"file:",5)==0)value=spec+5;else if(std::strncmp(spec,"stream:",7)==0)value=spec+7;else return nullptr;
    wchar_t exe[MAX_PATH];GetModuleFileNameW(nullptr,exe,MAX_PATH);wchar_t*slash=wcsrchr(exe,L'\\');if(slash)wcscpy_s(slash+1,MAX_PATH-(slash+1-exe),L"ffmpeg.exe");
    std::wstring media=wide_arg(value);return wolfdab_source_open_media_ex(exe,media.c_str(),sample_rate);
}

static bool svc_pipe_init(svc_pipe &p, const char *source_spec, int bitrate_kbps,
                          int codec, int sample_rate, int channels,
                          dab_eep_profile_t eep_profile,
                          uint8_t sub_ch_id, uint16_t start_cu,
                          uint16_t service_id, const char *label,
                          const char *dls_text, const char *slide_path,
                          const char *mot_folder, unsigned mot_interval,
                          epg_t *epg, spi_enc_t *spi) {
    p.desc.service_id     = service_id;
    p.desc.sub_ch_id      = sub_ch_id;
    p.desc.start_addr_cu  = start_cu;
    p.desc.sub_ch_size_cus = (uint16_t)dab_eep_cu_for_bitrate((unsigned)bitrate_kbps, eep_profile);
    p.desc.eep_profile = eep_profile;
    p.desc.label          = label;
    p.bytes_per_cif       = (size_t)bitrate_kbps * 3u;
    p.sf_produced         = 0;

    p.src = open_source_spec(source_spec, sample_rate);
    if (!p.src) return false;

    int actual_codec=codec;
    p.channels = channels;
    if(codec==3){
        if(sample_rate!=24000&&sample_rate!=48000){LOGE("DAB MP2 requires 24 or 48 kHz");wolfdab_source_close(p.src);p.src=nullptr;return false;}
        p.mp2_cifs_per_frame=sample_rate==24000?2u:1u;
        p.mp2=twolame_init();
        if(!p.mp2||twolame_set_in_samplerate(p.mp2,sample_rate)||twolame_set_out_samplerate(p.mp2,sample_rate)||
           twolame_set_num_channels(p.mp2,channels)||twolame_set_mode(p.mp2,channels==1?TWOLAME_MONO:TWOLAME_STEREO)||
           twolame_set_bitrate(p.mp2,bitrate_kbps)||twolame_set_DAB(p.mp2,1)||
           twolame_set_DAB_xpad_length(p.mp2,PAD_SCHED_XPAD_MAX+2)||twolame_set_error_protection(p.mp2,1)||
           twolame_init_params(p.mp2)<0||twolame_set_DAB_scf_crc_length(p.mp2)<0){
            LOGE("unsupported DAB MP2 format: %d kbps",bitrate_kbps);if(p.mp2)twolame_close(&p.mp2);wolfdab_source_close(p.src);p.src=nullptr;return false;
        }
        p.subch=dab_msc_subch_new(bitrate_kbps,start_cu,eep_profile);
        if(!p.subch){twolame_close(&p.mp2);wolfdab_source_close(p.src);p.src=nullptr;return false;}
        p.pad=pad_sched_new(dls_text,0,slide_path);p.mot_folder=mot_folder?mot_folder:"";p.mot_interval=mot_interval?mot_interval:10;
        p.desc.mot_slideshow=(slide_path&&*slide_path)||!p.mot_folder.empty();p.desc.asc_type=0;
        if(p.pad&&epg)pad_sched_set_epg(p.pad,epg);if(p.pad&&spi)pad_sched_set_spi(p.pad,spi);
        return true;
    }
    const int enc_mode=actual_codec==0?DABTX_AAC_MODE_DABPLUS_LC:actual_codec==2?DABTX_AAC_MODE_DABPLUS_PS:DABTX_AAC_MODE_DABPLUS_SBR;
    p.desc.asc_type=63;
    p.enc = aac_enc_open_ex_channels(enc_mode, bitrate_kbps * 1000, sample_rate, channels);
    if(!p.enc&&actual_codec==2){
        LOGW("HE-AAC v2 is not accepted at %d kbps/%d Hz; using HE-AAC v1",bitrate_kbps,sample_rate);
        actual_codec=1;
        p.enc=aac_enc_open_ex_channels(DABTX_AAC_MODE_DABPLUS_SBR,bitrate_kbps*1000,sample_rate,channels);
    }
    if (!p.enc) { wolfdab_source_close(p.src); p.src = nullptr; return false; }

    dabplus_cfg_t sf_cfg = {
        .bitrate_kbps     = bitrate_kbps,
        .num_aus          = actual_codec==0 ? sample_rate/8000 : sample_rate/16000,
        .dac_rate         = sample_rate==48000,
        .sbr_flag         = actual_codec==0 ? 0 : 1,
        .aac_channel_mode = channels==1 || actual_codec==2 ? 0 : 1,
        .ps_flag          = channels==2 && actual_codec==2 ? 1 : 0,
        .mpeg_surround    = 0,
    };
    p.sf = dabplus_sf_new(&sf_cfg);
    if (!p.sf) { aac_enc_close(p.enc); wolfdab_source_close(p.src); return false; }

    p.subch = dab_msc_subch_new(bitrate_kbps, start_cu, eep_profile);
    if (!p.subch) { dabplus_sf_free(p.sf); aac_enc_close(p.enc); wolfdab_source_close(p.src); return false; }

    p.pad = pad_sched_new(dls_text, 0, slide_path);
    p.mot_folder = mot_folder ? mot_folder : "";
    p.mot_interval = mot_interval ? mot_interval : 10;
    p.desc.mot_slideshow = (slide_path && *slide_path) || !p.mot_folder.empty();
    if (p.pad && epg)
        pad_sched_set_epg(p.pad, epg);
    if (p.pad && spi)
        pad_sched_set_spi(p.pad, spi);

    p.sf_data_size = dabplus_sf_data_size(&sf_cfg);
    p.sf_raw.resize(p.sf_data_size + 256);
    p.sf_bytes.resize((size_t)bitrate_kbps * 15u);
    return true;
}

static void svc_pipe_close(svc_pipe &p) {
    if (p.pad)   pad_sched_free(p.pad);
    if (p.subch) dab_msc_subch_free(p.subch);
    if (p.sf)    dabplus_sf_free(p.sf);
    if (p.enc)   aac_enc_close(p.enc);
    if (p.src)   wolfdab_source_close(p.src);
    if (p.mp2)   twolame_close(&p.mp2);
}

/* fdk-aac with TT_DABPLUS writes the same DSE (ancillary/PAD) into every
 * AU of a superframe.  Qt-dab processes each AU independently, so duplicate
 * DLS segments cause segment-number mismatches.  Fix: zero the DSE payload
 * in all AUs except the last one.  With zeroed F-PAD bytes qt-dab sees
 * x_padInd=0 and skips PAD processing for those AUs.  AU-CRC is recomputed
 * because we modified AU content. */
static void strip_pad_early_aus(uint8_t *sf, size_t sf_len) {
    uint8_t dac = (sf[2] >> 6) & 1;
    uint8_t sbr = (sf[2] >> 5) & 1;
    int num_aus, au_start[7];
    int R = (int)(sf_len / 110);
    bool modified = false;

    switch (2 * dac + sbr) {
    case 0: num_aus = 4; au_start[0] = 8;  break;
    case 1: num_aus = 2; au_start[0] = 5;  break;
    case 2: num_aus = 6; au_start[0] = 11; break;
    case 3: num_aus = 3; au_start[0] = 6;  break;
    }
    int hdr_bits = 24; /* 16 firecode + 8 config */
    for (int a = 1; a < num_aus; ++a) {
        int bp = hdr_bits / 8, bo = hdr_bits % 8;
        au_start[a] = (((sf[bp] << 8) | sf[bp + 1]) >> (4 - bo)) & 0xFFF;
        hdr_bits += 12;
    }
    au_start[num_aus] = R * 110;

    for (int a = 0; a < num_aus - 1; ++a) {
        int off = au_start[a];
        if (off >= (int)sf_len) break;
        if (((sf[off] >> 5) & 7) != 4) continue;   /* not DSE */
        int count = sf[off + 1];
        if (off + 2 + count > au_start[a + 1]) continue;
        memset(&sf[off + 2], 0, (size_t)count);     /* zero PAD data */
        /* Recompute AU-CRC (inverted CCITT at AU end). */
        int au_end  = au_start[a + 1];
        int au_dlen = au_end - off - 2;
        uint16_t crc = crc16_ccitt(&sf[off], (size_t)au_dlen) ^ 0xFFFF;
        sf[au_end - 2] = (uint8_t)(crc >> 8);
        sf[au_end - 1] = (uint8_t)(crc & 0xFF);
        modified = true;
    }

    /* Recompute Firecode CRC — stripping may have changed bytes in the
     * guarded region (bytes 2-10).  Without this, qt-dab's
     * checkAndCorrect rejects the superframe. */
    if (modified) {
        uint16_t fc = crc16_firecode(sf + 2, 9);
        sf[0] = (uint8_t)(fc >> 8);
        sf[1] = (uint8_t)(fc & 0xFF);
    }
}

static void svc_pipe_update_mot(svc_pipe &p) {
    if (stop_requested() || !p.pad || p.mot_folder.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    bool changed = false;
    if (now >= p.mot_next_scan) {
        p.mot_next_scan = now + std::chrono::seconds(1);
        std::vector<std::filesystem::path> files;
        uint64_t fingerprint = 1469598103934665603ull;
        try {
            for (const auto& entry : std::filesystem::directory_iterator(p.mot_folder)) {
                if (stop_requested()) return;
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){return (char)std::tolower(c);});
                if (ext != ".jpg" && ext != ".jpeg" && ext != ".png") continue;
                files.push_back(entry.path());
            }
            std::sort(files.begin(), files.end());
            for (const auto& file : files) {
                if (stop_requested()) return;
                const std::string key=file.string();
                for(unsigned char c:key){fingerprint^=c;fingerprint*=1099511628211ull;}
                fingerprint^=(uint64_t)std::filesystem::file_size(file);fingerprint*=1099511628211ull;
                fingerprint^=(uint64_t)std::filesystem::last_write_time(file).time_since_epoch().count();fingerprint*=1099511628211ull;
            }
        } catch (const std::exception& e) { LOGW("MOT folder scan failed: %s",e.what()); return; }
        changed = fingerprint != p.mot_fingerprint || files != p.mot_files;
        if (changed) { p.mot_files=std::move(files);p.mot_fingerprint=fingerprint;p.mot_index=0; }
    }
    if (p.mot_files.empty() || (!changed && (now < p.mot_next_slide || !pad_sched_slide_complete(p.pad)))) return;
    if (p.mot_index >= p.mot_files.size()) p.mot_index=0;
    const auto& file=p.mot_files[p.mot_index];
    const std::string path=file.string(), name=file.filename().string();
    if (pad_sched_set_slide(p.pad,path.c_str(),name.c_str(),++p.mot_transport_id)==0) {
        /* The service-level folder carousel, rather than the PAD scheduler,
         * owns repetition and image advancement.  Leaving automatic repeat
         * enabled here keeps restarting this same object forever, so
         * pad_sched_slide_complete() never lets the carousel advance. */
        pad_sched_set_slide_auto_retx(p.pad,0);
        LOGI("MOT service %u: %s",p.desc.service_id,path.c_str());
        p.mot_index=(p.mot_index+1)%p.mot_files.size();
        p.mot_next_slide=now+std::chrono::seconds(p.mot_interval);
    }
}

/* Produce one superframe for a service, slice into CIFs, push to queue.
 * Returns 0 on success, -1 on encoder error. */
static int svc_pipe_produce_mp2(svc_pipe &p) {
    constexpr size_t samples=TWOLAME_SAMPLES_PER_FRAME;
    std::vector<int16_t> pcm(samples*2),left(samples),right(samples);int guard=0;
    while(p.cif_queue.size()<5 && !stop_requested() && guard++<12){
        svc_pipe_update_mot(p);
        const char *metadata=wolfdab_source_metadata(p.src);
        if(metadata&&p.last_metadata!=metadata){p.last_metadata=metadata;if(p.pad)pad_sched_set_dls(p.pad,metadata);}
        size_t got=0;while(got<pcm.size()&&!stop_requested()){size_t n=wolfdab_source_read(p.src,pcm.data()+got,pcm.size()-got);if(!n){memset(pcm.data()+got,0,(pcm.size()-got)*sizeof(int16_t));break;}got+=n;}
        for(size_t i=0;i<samples;++i){left[i]=pcm[i*2];right[i]=pcm[i*2+1];}
        std::vector<uint8_t> frame(p.bytes_per_cif+256);
        int n=twolame_encode_buffer(p.mp2,left.data(),p.channels==1?left.data():right.data(),(int)samples,frame.data(),(int)frame.size());
        if(n<0)return -1;if(n==0)continue;frame.resize((size_t)n);
        if(!p.mp2_pending.empty()){
            twolame_set_DAB_scf_crc(p.mp2,p.mp2_pending.data(),(int)p.mp2_pending.size());
            uint8_t xp[PAD_SCHED_XPAD_MAX],f0=0,f1=0;int xl=p.pad?pad_sched_get_xpad(p.pad,xp,sizeof(xp),&f0,&f1):0;
            if(p.mp2_pending.size()!=p.bytes_per_cif*p.mp2_cifs_per_frame)return -1;
            if(xl>0&&(size_t)xl+2<=p.mp2_pending.size()){size_t off=p.mp2_pending.size()-(size_t)xl-2;memcpy(p.mp2_pending.data()+off,xp,(size_t)xl);p.mp2_pending[p.mp2_pending.size()-2]=f0;p.mp2_pending[p.mp2_pending.size()-1]=f1;}
            for(unsigned c=0;c<p.mp2_cifs_per_frame;++c){std::array<uint8_t,2048> chunk{};memcpy(chunk.data(),p.mp2_pending.data()+(size_t)c*p.bytes_per_cif,p.bytes_per_cif);p.cif_queue.push_back(chunk);}
        }
        p.mp2_pending=std::move(frame);
    }
    return p.cif_queue.size()>=5?0:-1;
}

static int svc_pipe_produce(svc_pipe &p) {
    if(p.mp2)return svc_pipe_produce_mp2(p);
    const size_t block = DABTX_AAC_GRANULE * DABTX_AAC_CHANNELS;
    std::vector<int16_t> pcm(block);
    std::vector<int16_t> mono(DABTX_AAC_GRANULE);
    svc_pipe_update_mot(p);

    const char *metadata = wolfdab_source_metadata(p.src);
    if (metadata && p.last_metadata != metadata) {
        p.last_metadata = metadata;
        if (p.pad) pad_sched_set_dls(p.pad, p.last_metadata.c_str());
        LOGI("DLS metadata: %s", p.last_metadata.c_str());
    }

    /* Generate PAD once per superframe.  With TT_DABPLUS the encoder
     * needs multiple aacEncEncode() calls to fill one superframe (e.g. 3
     * for HE-AAC v1 SBR).  Only the LAST call actually encodes — earlier
     * calls just buffer samples and discard ancillary data.  If we called
     * dls_enc_get_xpad() on every iteration the segment counter would
     * advance too fast and qt-dab would see out-of-order DLS segments. */
    uint8_t xpad[PAD_SCHED_XPAD_MAX];
    uint8_t fpad0 = 0, fpad1 = 0;
    int xpad_len = 0;
    const uint8_t *anc = NULL;
    size_t anc_len = 0;
    uint8_t anc_buf[PAD_SCHED_XPAD_MAX + 2]; /* +2 for F-PAD */

    if (p.pad) {
        xpad_len = pad_sched_get_xpad(p.pad, xpad, sizeof(xpad),
                                      &fpad0, &fpad1);
        if (xpad_len > 0) {
            memcpy(anc_buf, xpad, (size_t)xpad_len);
            anc_buf[xpad_len + 0] = fpad0;
            anc_buf[xpad_len + 1] = fpad1;
            anc = anc_buf;
            anc_len = (size_t)(xpad_len + 2);
        }
    }

    /* Debug: dump first ancillary buffer per service. */
    {
        static int anc_dbg[4] = {};
        int si = p.desc.sub_ch_id - 1;
        if (si >= 0 && si < 4 && !anc_dbg[si] && anc_len > 0) {
            char hex[256] = {0};
            int hpos = 0;
            for (size_t j = 0; j < anc_len && hpos < 240; ++j)
                hpos += snprintf(hex + hpos, sizeof(hex) - (size_t)hpos,
                                "%02x ", anc[j]);
            LOGI("svc%d anc[%zu]: %s", si + 1, anc_len, hex);
            anc_dbg[si] = 1;
        }
    }

    size_t sf_out_len = 0;
    while (sf_out_len == 0 && !stop_requested()) {
        size_t got = 0;
        /* A DAB ensemble must remain on-air even when a local capture device
         * has no routed source.  Audio reads are non-blocking; pad a missing
         * block immediately with digital silence so the RF clock never starves. */
        while (got < block && !stop_requested()) {
            size_t r = wolfdab_source_read(p.src, pcm.data() + got, block - got);
            if (r == 0) {
                std::memset(pcm.data() + got, 0,
                            (block - got) * sizeof(pcm[0]));
                got = block;
                break;
            }
            got += r;
        }
        if (stop_requested()) break;
        const int16_t *encoder_pcm=pcm.data();
        if(p.channels==1){for(size_t n=0;n<DABTX_AAC_GRANULE;++n)mono[n]=(int16_t)(((int32_t)pcm[n*2]+(int32_t)pcm[n*2+1])/2);encoder_pcm=mono.data();}
        if (aac_enc_frame(p.enc, encoder_pcm,
                          p.sf_raw.data(), p.sf_raw.size(),
                          &sf_out_len, anc, anc_len) != 0) {
            return -1;
        }
    }
    if (stop_requested()) return -1;

    if (anc_len > 0 && sf_out_len > 0)
        strip_pad_early_aus(p.sf_raw.data(), sf_out_len);


    if (dabplus_sf_from_raw(p.sf, p.sf_raw.data(), sf_out_len,
                            p.sf_bytes.data(), p.sf_bytes.size()) != 0) {
        LOGE("svc %u: RS failed (got %zu, expected %zu)",
             p.desc.sub_ch_id, sf_out_len, p.sf_data_size);
        return -1;
    }
    ++p.sf_produced;

    for (int k = 0; k < 5; ++k) {
        std::array<uint8_t, 2048> chunk{};
        std::memcpy(chunk.data(),
                    p.sf_bytes.data() + (size_t)k * p.bytes_per_cif,
                    p.bytes_per_cif);
        p.cif_queue.push_back(chunk);
    }
    return 0;
}

static int cmd_tx(const char *const *source_specs, int n_svcs,
                  const int *bitrates, const int *codecs, const int *sample_rates, const int *channels,
                  const dab_eep_profile_t *eep_profiles,
                  const uint16_t *service_ids, const uint8_t *scids,
                  const uint8_t *subch_ids, const uint8_t *ptys,
                  const char *channel_label, int seconds,
                  unsigned txvga_db, int amp_api_flag,
                  uint16_t ensemble_id, uint8_t ecc, const char *ens_label, const char *const *svc_labels,
                  const char *const *dls_texts, const char *const *short_labels,
                  const char *slide_path, const char *const *mot_folders,
                  const unsigned *mot_intervals, epg_t *epg, spi_enc_t *spi) {
    /*
     * Multi-service TX: N independent audio→encoder→superframe→CIF pipelines
     * feed into a shared MSC frame via dab_msc_frame_build_multi.
     *
     * Rate balance per service: 1 superframe = 120 ms → 5 CIFs.
     * Drain all queues every 4 CIFs → 1 Mode I RF frame (96 ms).
     */
    const dab_channel_t *ch = dab_channel_find(channel_label);
    if (!ch) { LOGE("unknown DAB channel: %s", channel_label); return 2; }
    if (n_svcs < 1 || n_svcs > 64) { LOGE("n_svcs must be 1..64"); return 2; }

    svc_pipe pipes[64] = {};
    uint16_t next_cu = 0;
    for (int i = 0; i < n_svcs; ++i) {
        const unsigned cu = dab_eep_cu_for_bitrate((unsigned)bitrates[i], eep_profiles[i]);
        const uint16_t start_cu = next_cu;
        if (!cu || next_cu + cu > 864 ||
            !svc_pipe_init(pipes[i], source_specs[i], bitrates[i], codecs[i], sample_rates[i], channels ? channels[i] : 2, eep_profiles[i],
                           subch_ids[i], start_cu, service_ids[i], svc_labels[i],
                           dls_texts[i], slide_path, mot_folders ? mot_folders[i] : nullptr,
                           mot_intervals ? mot_intervals[i] : 10, epg, spi)) {
            LOGE("failed to init service %d (%s)", i + 1, source_specs[i]);
            for (int j = 0; j < i; ++j) svc_pipe_close(pipes[j]);
            return 1;
        }
        next_cu = (uint16_t)(next_cu + cu);
        pipes[i].desc.sc_ids = scids[i];
        pipes[i].desc.pty = ptys ? ptys[i] : 0;
        pipes[i].desc.short_label_flag=short_label_flag(svc_labels[i],short_labels?short_labels[i]:nullptr);
    }

    dab_frame_ctx_t *fctx = dab_frame_new();
    if (!fctx) {
        for (int i = 0; i < n_svcs; ++i) svc_pipe_close(pipes[i]);
        return 1;
    }

    hackrf_tx_t *tx = hackrf_tx_open_ex(ch->freq_hz, txvga_db, amp_api_flag);
    if (!tx) {
        dab_frame_free(fctx);
        for (int i = 0; i < n_svcs; ++i) svc_pipe_close(pipes[i]);
        return 1;
    }

    /* Pre-fill only a short lead-in.  The ring itself is deliberately much
     * larger, so a late audio/FFmpeg worker cannot drain the RF clock. */
    {
        const size_t prefill = std::min(hackrf_tx_writable(tx),
                                        (size_t)DAB_MODE_I_FRAME_SAMPLES * 3u);
        std::vector<std::complex<float>> silence(prefill);
        hackrf_tx_push(tx,
            reinterpret_cast<const float _Complex *>(silence.data()),
            prefill);
    }

    if (hackrf_tx_start(tx) != 0) {
        hackrf_tx_close(tx);
        dab_frame_free(fctx);
        for (int i = 0; i < n_svcs; ++i) svc_pipe_close(pipes[i]);
        return 1;
    }

    LOGI("tx start: %s %.3f MHz, %d services, txvga %u dB",
         ch->label, ch->freq_hz / 1e6, n_svcs, txvga_db);
    for (int i = 0; i < n_svcs; ++i)
        LOGI("  svc %d: source %s, subch %u, CU %u..%u, \"%.*s\"",
             i + 1, source_specs[i], pipes[i].desc.sub_ch_id,
             pipes[i].desc.start_addr_cu,
             pipes[i].desc.start_addr_cu + pipes[i].desc.sub_ch_size_cus - 1,
             16, pipes[i].desc.label);

    /* Build service descriptor array for FIC. */
    dab_svc_desc descs[64];
    dab_msc_subch_t *subchs[64];
    for (int i = 0; i < n_svcs; ++i) {
        descs[i]  = pipes[i].desc;
        subchs[i] = pipes[i].subch;
    }

    uint8_t fic_in_frame[DAB_FIC_FRAME_IN_BYTES];
    uint8_t fic_out_frame[DAB_FIC_FRAME_OUT_BYTES];
    static uint8_t msc_out_frame[DAB_MSC_FRAME_BYTES];
    static std::complex<float> iq_frame[DAB_MODE_I_FRAME_SAMPLES];

    uint32_t cif_counter = 0;
    uint64_t rf_frames   = 0;

    auto t_start = std::chrono::steady_clock::now();
    auto t_next  = t_start + std::chrono::milliseconds(500);

    while (!stop_requested()) {
        auto now = std::chrono::steady_clock::now();
        if (seconds > 0 && now - t_start >= std::chrono::seconds(seconds)) break;

        /* Produce one superframe per service. */
        for (int i = 0; i < n_svcs; ++i) {
            if (svc_pipe_produce(pipes[i]) != 0) {
                LOGE("encoder error svc %d", i + 1);
                goto done;
            }
        }

        /* Drain when all queues have >= 4 CIFs. */
        while (true) {
            bool all_ready = true;
            for (int i = 0; i < n_svcs; ++i)
                if (pipes[i].cif_queue.size() < 4) { all_ready = false; break; }
            if (!all_ready) break;

            /* Build FIC for 4 CIFs. */
            for (unsigned c = 0; c < 4; ++c)
                build_fic_frame(fic_in_frame + (size_t)c * 96u,
                                cif_counter + c, ensemble_id, ecc, ens_label, descs, n_svcs);
            dab_fic_encode_frame(fic_in_frame, fic_out_frame);

            /* Build MSC: merge all subchannels into the frame. */
            uint8_t msc_in_bufs[64][4 * 1024];
            const uint8_t *msc_in_ptrs[64];
            for (int i = 0; i < n_svcs; ++i) {
                for (unsigned c = 0; c < 4; ++c)
                    std::memcpy(msc_in_bufs[i] + (size_t)c * pipes[i].bytes_per_cif,
                                pipes[i].cif_queue[c].data(),
                                pipes[i].bytes_per_cif);
                msc_in_ptrs[i] = msc_in_bufs[i];
            }
            dab_msc_frame_build_multi(subchs, msc_in_ptrs, n_svcs, msc_out_frame);

            dab_frame_build(fctx, fic_out_frame, msc_out_frame,
                            reinterpret_cast<float _Complex *>(iq_frame));

            /* Push with backoff. */
            size_t to_push = DAB_MODE_I_FRAME_SAMPLES;
            const float _Complex *p =
                reinterpret_cast<const float _Complex *>(iq_frame);
            while (to_push > 0 && !stop_requested()) {
                size_t w = hackrf_tx_push(tx, p, to_push);
                if (w == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }
                p       += w;
                to_push -= w;
            }

            for (int i = 0; i < n_svcs; ++i)
                for (int j = 0; j < 4; ++j)
                    pipes[i].cif_queue.pop_front();
            cif_counter += 4;
            ++rf_frames;
        }

        /* Periodic stats. */
        if (now >= t_next) {
            hackrf_tx_stats_t s;
            hackrf_tx_get_stats(tx, &s);
            uint64_t total_sf = 0;
            for (int i = 0; i < n_svcs; ++i) total_sf += pipes[i].sf_produced;
            LOGI("[tx] sf=%llu rf=%llu "
                 "pushed=%llu consumed=%llu under=%llu",
                 (unsigned long long)total_sf,
                 (unsigned long long)rf_frames,
                 (unsigned long long)s.samples_pushed,
                 (unsigned long long)s.samples_consumed,
                 (unsigned long long)s.underruns);
            t_next += std::chrono::milliseconds(500);
        }
    }
done:
    if (stop_requested()) LOGI("interrupted");
    {
        hackrf_tx_stats_t s;
        hackrf_tx_get_stats(tx, &s);
        uint64_t total_sf = 0;
        for (int i = 0; i < n_svcs; ++i) total_sf += pipes[i].sf_produced;
        LOGI("tx done: sf=%llu rf=%llu pushed=%llu consumed=%llu under=%llu",
             (unsigned long long)total_sf,
             (unsigned long long)rf_frames,
             (unsigned long long)s.samples_pushed,
             (unsigned long long)s.samples_consumed,
             (unsigned long long)s.underruns);
    }

    hackrf_tx_close(tx);
    dab_frame_free(fctx);
    for (int i = 0; i < n_svcs; ++i) svc_pipe_close(pipes[i]);
    return 0;
}

/*
 * cmd_tx_file: same producer pipeline as cmd_tx, but the IQ frames are
 * written to disk in osmocom raw format (8-bit unsigned, interleaved I/Q
 * @ 2.048 MS/s) instead of being pushed into the HackRF. The output file
 * is directly playable by qt-dab's "raw file" input and welle.io, which
 * lets us inspect the modulator output offline against a real DAB+
 * decoder without burning RF airtime or fighting hardware quirks.
 *
 * Realtime is still bound by the live PortAudio capture, so generating N
 * seconds of file takes N seconds wallclock.
 */
static int cmd_tx_file(int dev, const char *channel_label, int seconds,
                       const char *out_path,
                       const char *ens_label, const char *svc_label,
                       const char *dls_text, const char *slide_path,
                       epg_t *epg, spi_enc_t *spi) {
    const int bitrate_kbps = 72;    /* matches cmd_tx */
    const size_t BYTES_PER_CIF = (size_t)bitrate_kbps * 3u;  /* 216 */
    const uint16_t sub_ch_size_cus = (uint16_t)(bitrate_kbps * 6 / 8); /* 54 */

    const dab_channel_t *ch = dab_channel_find(channel_label);
    if (!ch) { LOGE("unknown DAB channel: %s", channel_label); return 2; }

    pa_source_t *src = pa_source_open(dev, 1u << 17);
    if (!src) return 1;

    aac_enc_t *enc = aac_enc_open(DABTX_AAC_MODE_DABPLUS_SBR,
                                  bitrate_kbps * 1000);
    if (!enc) { pa_source_close(src); return 1; }

    dabplus_cfg_t sf_cfg = {
        .bitrate_kbps     = bitrate_kbps,
        .num_aus          = 3,
        .dac_rate         = 1,
        .sbr_flag         = 1,
        .aac_channel_mode = 1,
        .ps_flag          = 0,
        .mpeg_surround    = 0,
    };
    dabplus_sf_t *sf = dabplus_sf_new(&sf_cfg);
    if (!sf) { aac_enc_close(enc); pa_source_close(src); return 1; }

    /* PAD scheduler (DLS + optional MOT SlideShow + EPG + SPI) for tx-file. */
    pad_sched_t *pad = pad_sched_new(dls_text, 0, slide_path);
    if (pad && epg)
        pad_sched_set_epg(pad, epg);
    if (pad && spi)
        pad_sched_set_spi(pad, spi);

    dab_msc_subch_t *subch = dab_msc_subch_new(bitrate_kbps, /*start_cu=*/0, DAB_EEP_3A);
    dab_frame_ctx_t *fctx  = dab_frame_new();
    if (!subch || !fctx) {
        if (subch) dab_msc_subch_free(subch);
        if (fctx)  dab_frame_free(fctx);
        dabplus_sf_free(sf); aac_enc_close(enc); pa_source_close(src);
        return 1;
    }

    FILE *fp = std::fopen(out_path, "wb");
    if (!fp) {
        LOGE("cannot open '%s' for writing: %s", out_path, std::strerror(errno));
        dab_msc_subch_free(subch); dab_frame_free(fctx);
        dabplus_sf_free(sf); aac_enc_close(enc); pa_source_close(src);
        return 1;
    }

    LOGI("tx-file start: %s %.3f MHz, audio dev %d, %d kbps EEP-3A HE-AACv1 → %s",
         ch->label, ch->freq_hz / 1e6, dev, bitrate_kbps, out_path);
    LOGI("       format: osmocom raw, 8-bit unsigned IQ, 2.048 MS/s");

    const size_t block = DABTX_AAC_GRANULE * DABTX_AAC_CHANNELS;
    std::vector<int16_t> pcm(block);

    const size_t sf_data_size = dabplus_sf_data_size(&sf_cfg);
    std::vector<uint8_t> sf_raw(sf_data_size + 256);
    std::vector<uint8_t> sf_bytes((size_t)bitrate_kbps * 15u);

    std::deque<std::array<uint8_t, 1024>> cif_queue;

    uint8_t fic_in_frame[DAB_FIC_FRAME_IN_BYTES];
    uint8_t fic_out_frame[DAB_FIC_FRAME_OUT_BYTES];
    uint8_t msc_in_frame[4 * 1024];
    static uint8_t msc_out_frame[DAB_MSC_FRAME_BYTES];
    static std::complex<float> iq_frame[DAB_MODE_I_FRAME_SAMPLES];
    static uint8_t iq_bytes[2u * DAB_MODE_I_FRAME_SAMPLES];

    uint32_t cif_counter = 0;
    uint64_t rf_frames   = 0;
    uint64_t sf_produced = 0;
    uint64_t bytes_written = 0;

    auto t_start = std::chrono::steady_clock::now();
    auto t_next  = t_start + std::chrono::milliseconds(500);

    while (!stop_requested()) {
        auto now = std::chrono::steady_clock::now();
        if (seconds > 0 && now - t_start >= std::chrono::seconds(seconds)) break;

        /* Generate PAD once per superframe — see svc_pipe_produce comment. */
        uint8_t xpad[PAD_SCHED_XPAD_MAX];
        uint8_t fp0 = 0, fp1 = 0;
        const uint8_t *anc_p = NULL;
        size_t anc_n = 0;
        uint8_t anc_b[PAD_SCHED_XPAD_MAX + 2];
        if (pad) {
            int xl = pad_sched_get_xpad(pad, xpad, sizeof(xpad), &fp0, &fp1);
            if (xl > 0) {
                memcpy(anc_b, xpad, (size_t)xl);
                anc_b[xl + 0] = fp0;
                anc_b[xl + 1] = fp1;
                anc_p = anc_b;
                anc_n = (size_t)(xl + 2);
            }
        }

        /* Feed PCM blocks until the encoder emits a complete superframe. */
        size_t sf_out_len = 0;
        while (sf_out_len == 0 && !stop_requested()) {
            size_t got = 0;
            while (got < block && !stop_requested()) {
                size_t r = pa_source_read(src, pcm.data() + got, block - got);
                if (r == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                got += r;
            }
            if (stop_requested()) break;
            if (aac_enc_frame(enc, pcm.data(),
                              sf_raw.data(), sf_raw.size(),
                              &sf_out_len, anc_p, anc_n) != 0) {
                LOGE("encoder error"); goto done;
            }
        }
        if (stop_requested()) break;

        if (anc_n > 0 && sf_out_len > 0)
            strip_pad_early_aus(sf_raw.data(), sf_out_len);

        if (dabplus_sf_from_raw(sf, sf_raw.data(), sf_out_len,
                                sf_bytes.data(), sf_bytes.size()) != 0) {
            LOGE("superframe RS failed (got %zu, expected %zu)",
                 sf_out_len, sf_data_size);
            break;
        }
        ++sf_produced;

        for (int k = 0; k < 5; ++k) {
            std::array<uint8_t, 1024> chunk{};
            std::memcpy(chunk.data(),
                        sf_bytes.data() + (size_t)k * BYTES_PER_CIF,
                        BYTES_PER_CIF);
            cif_queue.push_back(chunk);
        }

        while (cif_queue.size() >= 4) {
            for (unsigned c = 0; c < 4; ++c) {
                build_fic_frame_single(fic_in_frame + (size_t)c * 96u,
                                       cif_counter + c, sub_ch_size_cus,
                                       ens_label, svc_label);
                std::memcpy(msc_in_frame + (size_t)c * BYTES_PER_CIF,
                            cif_queue[c].data(), BYTES_PER_CIF);
            }
            dab_fic_encode_frame(fic_in_frame, fic_out_frame);
            dab_msc_frame_build_single(subch, msc_in_frame, msc_out_frame);
            dab_frame_build(fctx, fic_out_frame, msc_out_frame,
                            reinterpret_cast<float _Complex *>(iq_frame));

            for (size_t i = 0; i < DAB_MODE_I_FRAME_SAMPLES; ++i) {
                float re = iq_frame[i].real() * 46.0f;
                float im = iq_frame[i].imag() * 46.0f;
                if (re >  127.0f) re =  127.0f; else if (re < -127.0f) re = -127.0f;
                if (im >  127.0f) im =  127.0f; else if (im < -127.0f) im = -127.0f;
                iq_bytes[2 * i + 0] = (uint8_t)(lrintf(re) + 128);
                iq_bytes[2 * i + 1] = (uint8_t)(lrintf(im) + 128);
            }
            size_t want = sizeof(iq_bytes);
            if (std::fwrite(iq_bytes, 1, want, fp) != want) {
                LOGE("fwrite failed: %s", std::strerror(errno));
                break;
            }
            bytes_written += want;

            for (int i = 0; i < 4; ++i) cif_queue.pop_front();
            cif_counter += 4;
            ++rf_frames;
        }

        if (now >= t_next) {
            pa_source_stats_t as;
            pa_source_get_stats(src, &as);
            LOGI("[tx-file] sf=%llu rf=%llu cifQ=%zu bytes=%llu audDrop=%llu",
                 (unsigned long long)sf_produced,
                 (unsigned long long)rf_frames,
                 cif_queue.size(),
                 (unsigned long long)bytes_written,
                 (unsigned long long)as.frames_dropped);
            t_next += std::chrono::milliseconds(500);
        }
    }
done:
    if (stop_requested()) LOGI("interrupted");
    LOGI("tx-file done: sf=%llu rf=%llu bytes=%llu",
         (unsigned long long)sf_produced,
         (unsigned long long)rf_frames,
         (unsigned long long)bytes_written);

    std::fclose(fp);
    pad_sched_free(pad);
    dab_frame_free(fctx);
    dab_msc_subch_free(subch);
    dabplus_sf_free(sf);
    aac_enc_close(enc);
    pa_source_close(src);
    return 0;
}

static int cmd_tx_zero(const char *channel_label, int seconds, unsigned txvga_db) {
    const dab_channel_t *ch = dab_channel_find(channel_label);
    if (!ch) { LOGE("unknown DAB channel: %s", channel_label); return 2; }

    hackrf_tx_t *tx = hackrf_tx_open(ch->freq_hz, txvga_db);
    if (!tx) return 1;

    /* Push 24 ms worth of zeros per iteration until time expires. */
    const size_t CHUNK = 49152;  /* 24 ms @ 2.048 MS/s */
    std::vector<std::complex<float>> zeros(CHUNK);  /* zero-initialised */

    /* Pre-fill the ring to avoid the cold-start underrun (~256 ms). */
    {
        const size_t prefill = hackrf_tx_writable(tx);
        std::vector<std::complex<float>> silence(prefill);
        hackrf_tx_push(tx,
            reinterpret_cast<const float _Complex *>(silence.data()),
            prefill);
    }
    if (hackrf_tx_start(tx) != 0) { hackrf_tx_close(tx); return 1; }

    auto t_start = std::chrono::steady_clock::now();
    auto t_next  = t_start + std::chrono::milliseconds(500);

    while (!stop_requested() && std::chrono::steady_clock::now() - t_start < std::chrono::seconds(seconds)) {
        size_t to_push = CHUNK;
        while (to_push > 0) {
            size_t w = hackrf_tx_push(
                tx,
                reinterpret_cast<const float _Complex *>(zeros.data() + (CHUNK - to_push)),
                to_push);
            if (w == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            to_push -= w;
        }
        auto now = std::chrono::steady_clock::now();
        if (now >= t_next) {
            hackrf_tx_stats_t s;
            hackrf_tx_get_stats(tx, &s);
            LOGI("[tx-zero] pushed=%llu consumed=%llu underruns=%llu",
                 (unsigned long long)s.samples_pushed,
                 (unsigned long long)s.samples_consumed,
                 (unsigned long long)s.underruns);
            t_next += std::chrono::milliseconds(500);
        }
    }

    if (stop_requested()) LOGI("interrupted");

    hackrf_tx_stats_t s;
    hackrf_tx_get_stats(tx, &s);
    LOGI("tx-zero done: pushed=%llu consumed=%llu underruns=%llu",
         (unsigned long long)s.samples_pushed,
         (unsigned long long)s.samples_consumed,
         (unsigned long long)s.underruns);

    hackrf_tx_close(tx);
    return 0;
}

int main(int argc, char **argv) {
    std::signal(SIGINT, sigint_handler);

    if (argc < 2) { usage(argv[0]); return 2; }

    /* Pre-pass 1: find --config to load before other options. */
    const char *config_path = "dabtx.cfg";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        }
    }
    dabtx_cfg_t *file_cfg = dabtx_cfg_load(config_path);

    /* Defaults — overridden first by config file, then by CLI. */
    char ens_buf[17], svc_buf[17], svc2_buf[17];
    const char *cfg_ens  = dabtx_cfg_get(file_cfg, "ensemble");
    const char *cfg_svc  = dabtx_cfg_get(file_cfg, "service");
    const char *cfg_svc2 = dabtx_cfg_get(file_cfg, "service2");
    dab_label_pad(cfg_ens  ? cfg_ens  : "dabtx Ensemble", ens_buf);
    dab_label_pad(cfg_svc  ? cfg_svc  : "dabtx Service", svc_buf);
    dab_label_pad(cfg_svc2 ? cfg_svc2 : "dabtx Service 2", svc2_buf);

    int audio2_dev = dabtx_cfg_get_int(file_cfg, "audio2", -1);
    int amp_api_flag = 0; /* Standard libhackrf semantics: 1 = ON, 0 = OFF. */
    int codec1 = 1, codec2 = 1, sampling1 = 48000, sampling2 = 48000;
    unsigned ensemble_id = 0xE001;
    unsigned ensemble_ecc = 0xE0;
    unsigned sid1 = 0xE001, sid2 = 0xE002, scids1 = 0, scids2 = 0, subch1 = 1, subch2 = 2;
    struct cli_svc { std::string label,source,dls,short_label,mot_folder; unsigned sid,scids,subch,bitrate,codec,sampling,eep,mot_interval,pty,channels; };
    std::vector<cli_svc> cli_services;
    const char *source1 = dabtx_cfg_get(file_cfg, "source");
    const char *source2 = dabtx_cfg_get(file_cfg, "source2");

    const char *cfg_dls  = dabtx_cfg_get(file_cfg, "dls");
    const char *cfg_dls2 = dabtx_cfg_get(file_cfg, "dls2");
    const char *dls_text  = cfg_dls  ? cfg_dls  : "dabtx - DAB+ Sender fuer HackRF One";
    const char *dls_text2 = cfg_dls2 ? cfg_dls2 : "dabtx Service 2";

    const char *cfg_slide = dabtx_cfg_get(file_cfg, "slide");
    const char *slide_path = cfg_slide ? cfg_slide : NULL;

    /* Pre-pass 2: extract CLI overrides and strip them from arg list. */
    std::vector<char *> args;
    args.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            ++i; /* already handled */
        } else if (std::strcmp(argv[i], "--svc") == 0 && i + 1 < argc) {
            std::string packed=argv[++i];std::vector<std::string> f;size_t p=0;
            while(true){size_t q=packed.find("~|~",p);f.push_back(packed.substr(p,q==std::string::npos?q:q-p));if(q==std::string::npos)break;p=q+3;}
            if(f.size()!=10&&f.size()!=11&&f.size()!=13&&f.size()!=14&&f.size()!=15){LOGE("invalid --svc record");return 2;}
            cli_services.push_back({f[7],f[8],f[9],f.size()>=11?f[10]:"",f.size()>=13?f[11]:"",(unsigned)std::strtoul(f[0].c_str(),nullptr,0),(unsigned)std::strtoul(f[1].c_str(),nullptr,0),(unsigned)std::strtoul(f[2].c_str(),nullptr,0),(unsigned)std::strtoul(f[3].c_str(),nullptr,0),(unsigned)std::strtoul(f[4].c_str(),nullptr,0),(unsigned)std::strtoul(f[5].c_str(),nullptr,0),(unsigned)std::strtoul(f[6].c_str(),nullptr,0),f.size()>=13?(unsigned)std::strtoul(f[12].c_str(),nullptr,10):10u,f.size()>=14?(unsigned)std::strtoul(f[13].c_str(),nullptr,10):0u,f.size()==15?(unsigned)std::strtoul(f[14].c_str(),nullptr,10):2u});
        } else if (std::strcmp(argv[i], "--ensemble") == 0 && i + 1 < argc) {
            dab_label_pad(argv[++i], ens_buf);
        } else if (std::strcmp(argv[i], "--ensemble-id") == 0 && i + 1 < argc) {
            ensemble_id = (unsigned)std::strtoul(argv[++i], nullptr, 0);
        } else if (std::strcmp(argv[i], "--ecc") == 0 && i + 1 < argc) {
            ensemble_ecc = (unsigned)std::strtoul(argv[++i], nullptr, 0);
        } else if (std::strcmp(argv[i], "--service") == 0 && i + 1 < argc) {
            dab_label_pad(argv[++i], svc_buf);
        } else if (std::strcmp(argv[i], "--service2") == 0 && i + 1 < argc) {
            dab_label_pad(argv[++i], svc2_buf);
        } else if (std::strcmp(argv[i], "--audio2") == 0 && i + 1 < argc) {
            audio2_dev = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
            source1 = argv[++i];
        } else if (std::strcmp(argv[i], "--source2") == 0 && i + 1 < argc) {
            source2 = argv[++i];
        } else if (std::strcmp(argv[i], "--stop-event") == 0 && i + 1 < argc) {
            std::wstring event_name = wide_arg(argv[++i]);
            g_stop_event = OpenEventW(SYNCHRONIZE, FALSE, event_name.c_str());
            if (!g_stop_event) LOGE("cannot open GUI stop event");
        } else if (std::strcmp(argv[i], "--amp-flag") == 0 && i + 1 < argc) {
            amp_api_flag = std::atoi(argv[++i]) ? 1 : 0;
        } else if (std::strcmp(argv[i], "--codec") == 0 && i + 1 < argc) {
            codec1 = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--codec2") == 0 && i + 1 < argc) {
            codec2 = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--sampling") == 0 && i + 1 < argc) {
            sampling1 = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--sampling2") == 0 && i + 1 < argc) {
            sampling2 = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--sid") == 0 && i + 1 < argc) {
            sid1 = (unsigned)std::strtoul(argv[++i], nullptr, 0);
        } else if (std::strcmp(argv[i], "--sid2") == 0 && i + 1 < argc) {
            sid2 = (unsigned)std::strtoul(argv[++i], nullptr, 0);
        } else if (std::strcmp(argv[i], "--scids") == 0 && i + 1 < argc) {
            scids1 = (unsigned)std::strtoul(argv[++i], nullptr, 0);
        } else if (std::strcmp(argv[i], "--scids2") == 0 && i + 1 < argc) {
            scids2 = (unsigned)std::strtoul(argv[++i], nullptr, 0);
        } else if (std::strcmp(argv[i], "--subch") == 0 && i + 1 < argc) {
            subch1 = (unsigned)std::strtoul(argv[++i], nullptr, 0);
        } else if (std::strcmp(argv[i], "--subch2") == 0 && i + 1 < argc) {
            subch2 = (unsigned)std::strtoul(argv[++i], nullptr, 0);
        } else if (std::strcmp(argv[i], "--dls") == 0 && i + 1 < argc) {
            dls_text = argv[++i];
        } else if (std::strcmp(argv[i], "--dls2") == 0 && i + 1 < argc) {
            dls_text2 = argv[++i];
        } else if (std::strcmp(argv[i], "--slide") == 0 && i + 1 < argc) {
            slide_path = argv[++i];
        } else {
            args.push_back(argv[i]);
        }
    }
    int nargs = (int)args.size();

    /* Load EPG programme schedule and SPI encoder from config file. */
    epg_t *epg = epg_load(file_cfg);
    spi_enc_t *spi = epg ? spi_enc_new(epg, 0xE001, 0xE001) : NULL;

    for (int i = 1; i < nargs; ++i) {
        std::string a = args[i];
        if (a == "-h" || a == "--help") { usage(args[0]); return 0; }
        if (a == "--list-audio")         return pa_list_input_devices();
        if (a == "--list-channels")      return cmd_list_channels();
        if (a == "--probe-hackrf")       return hackrf_tx_probe();
        if (a == "--capture-test" && i + 2 < nargs) {
            int dev = std::atoi(args[i + 1]);
            int sec = std::atoi(args[i + 2]);
            return cmd_capture_test(dev, sec);
        }
        if (a == "--encode-test" && i + 3 < nargs) {
            int dev = std::atoi(args[i + 1]);
            int sec = std::atoi(args[i + 2]);
            return cmd_encode_test(dev, sec, args[i + 3]);
        }
        if (a == "--eti-test" && i + 3 < nargs) {
            int dev = std::atoi(args[i + 1]);
            int sec = std::atoi(args[i + 2]);
            return cmd_eti_test(dev, sec, args[i + 3]);
        }
        if (a == "--tx-zero" && i + 2 < nargs) {
            const char *ch = args[i + 1];
            int sec = std::atoi(args[i + 2]);
            unsigned vga = (i + 3 < nargs && args[i + 3][0] != '-') ?
                           (unsigned)std::atoi(args[i + 3]) : 0u;
            return cmd_tx_zero(ch, sec, vga);
        }
        if (a == "--tx" && i + 3 < nargs) {
            const char *ch = args[i + 1];
            int dev = std::atoi(args[i + 2]);
            int sec = std::atoi(args[i + 3]);
            unsigned vga = (i + 4 < nargs && args[i + 4][0] != '-') ?
                           (unsigned)std::atoi(args[i + 4]) : 0u;
            if(!cli_services.empty()){
                int n=(int)cli_services.size();std::vector<const char*> ss(n),ls(n),ds(n),sls(n),mf(n);std::vector<int> br(n),co(n),sr(n),chans(n);std::vector<unsigned> mi(n);std::vector<dab_eep_profile_t> ep(n);std::vector<uint16_t> si(n);std::vector<uint8_t> sc(n),su(n),pt(n);std::vector<std::array<char,17>> lp(n);
                for(int k=0;k<n;++k){auto&v=cli_services[k];bool sampling_ok=v.codec==3?(v.sampling==24000||v.sampling==48000):(v.sampling==32000||v.sampling==48000);if(v.codec>3||!sampling_ok||v.eep>DAB_EEP_4B||v.pty>31||(v.channels!=1&&v.channels!=2)){LOGE("invalid service format");return 2;}dab_label_pad(v.label.c_str(),lp[k].data());ss[k]=v.source.c_str();ls[k]=lp[k].data();ds[k]=v.dls.c_str();sls[k]=v.short_label.c_str();mf[k]=v.mot_folder.c_str();mi[k]=v.mot_interval;br[k]=(int)v.bitrate;co[k]=(int)v.codec;sr[k]=(int)v.sampling;chans[k]=(int)v.channels;ep[k]=(dab_eep_profile_t)v.eep;si[k]=(uint16_t)v.sid;sc[k]=(uint8_t)v.scids;su[k]=(uint8_t)v.subch;pt[k]=(uint8_t)v.pty;}
                if(ensemble_id>0xffff||ensemble_ecc>0xff){LOGE("invalid ensemble identity");return 2;}
                return cmd_tx(ss.data(),n,br.data(),co.data(),sr.data(),chans.data(),ep.data(),si.data(),sc.data(),su.data(),pt.data(),ch,sec,vga,amp_api_flag,(uint16_t)ensemble_id,(uint8_t)ensemble_ecc,ens_buf,ls.data(),ds.data(),sls.data(),slide_path,mf.data(),mi.data(),epg,spi);
            }
            int n_svcs;
            const char *sources[2];
            std::string default1="device:"+std::to_string(dev);
            std::string default2;
            const char *labels[2];
            sources[0] = source1 ? source1 : default1.c_str();
            labels[0] = svc_buf;
            if (source2 || audio2_dev >= 0) {
                if (!source2) default2="device:"+std::to_string(audio2_dev);
                sources[1] = source2 ? source2 : default2.c_str();
                labels[1] = svc2_buf;
                n_svcs    = 2;
            } else {
                n_svcs = 1;
            }
            const char *dls[2] = { dls_text, dls_text2 };
            int codecs[2] = {codec1,codec2}; int sample_rates[2] = {sampling1,sampling2};
            if ((sampling1!=32000&&sampling1!=48000)||(sampling2!=32000&&sampling2!=48000)||codec1<0||codec1>3||codec2<0||codec2>3) { LOGE("invalid codec or sampling option"); return 2; }
            uint16_t service_ids[2]={(uint16_t)sid1,(uint16_t)sid2}; uint8_t scids[2]={(uint8_t)scids1,(uint8_t)scids2}; uint8_t subchs[2]={(uint8_t)subch1,(uint8_t)subch2};
            if(sid1>0xffff||sid2>0xffff||scids1>15||scids2>15||subch1>63||subch2>63||(n_svcs==2&&(sid1==sid2||subch1==subch2))){LOGE("invalid or duplicate service identity");return 2;}
            int bitrates[2] = {72,72}; dab_eep_profile_t eep[2] = {DAB_EEP_3A,DAB_EEP_3A};
            if(ensemble_id>0xffff||ensemble_ecc>0xff){LOGE("invalid ensemble identity");return 2;}
            return cmd_tx(sources, n_svcs, bitrates, codecs, sample_rates, nullptr, eep, service_ids, scids, subchs, nullptr, ch, sec, vga, amp_api_flag, (uint16_t)ensemble_id, (uint8_t)ensemble_ecc, ens_buf, labels, dls, nullptr,
                          slide_path, nullptr, nullptr, epg, spi);
        }
        if (a == "--tx-file" && i + 4 < nargs) {
            const char *ch = args[i + 1];
            int dev = std::atoi(args[i + 2]);
            int sec = std::atoi(args[i + 3]);
            const char *out = args[i + 4];
            return cmd_tx_file(dev, ch, sec, out, ens_buf, svc_buf,
                               dls_text, slide_path, epg, spi);
        }
        LOGE("unknown arg: %s", args[i]);
        usage(args[0]);
        spi_enc_free(spi);
        epg_free(epg);
        dabtx_cfg_free(file_cfg);
        return 2;
    }

    usage(args[0]);
    spi_enc_free(spi);
    epg_free(epg);
    dabtx_cfg_free(file_cfg);
    return 2;
}
