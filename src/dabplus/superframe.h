#pragma once
/*
 * DAB+ audio superframe builder (ETSI TS 102 563).
 *
 * A 120 ms audio superframe contains:
 *   - 11-byte header for our config (2 firecode + 1 config + 8 au_start bytes)
 *   - num_aus access units, each followed by a 16-bit CRC
 *   - zero padding up to the data capacity (110 * R bytes, where R = bitrate/8)
 *   - per-column Reed-Solomon(120,110) parity appended to each of R columns
 *
 * The output buffer size is fixed at bitrate_kbps * 15 bytes per superframe.
 * It is laid out as R consecutive RS codewords:
 *   [col0 110 data][col0 10 parity][col1 110 data][col1 10 parity]...
 *
 * Only the 48 kHz AAC-LC stereo, no-SBR, no-PS configuration is wired up
 * for now (num_aus = 6). The struct below is designed so other profiles
 * can be added later without changing the API.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int bitrate_kbps;    /* subchannel bitrate, must be multiple of 8 */
    int num_aus;         /* 2, 3, 4, or 6 — derived from (dac_rate, sbr_flag) */
    int dac_rate;        /* 0 = 32 kHz audio, 1 = 48 kHz audio  */
    int sbr_flag;        /* 0 / 1 */
    int aac_channel_mode;/* 0 = mono, 1 = stereo */
    int ps_flag;         /* 0 / 1 (parametric stereo, requires sbr_flag = 1) */
    int mpeg_surround;   /* 0..7, 0 = not used */
} dabplus_cfg_t;

typedef struct dabplus_sf dabplus_sf_t;

dabplus_sf_t *dabplus_sf_new(const dabplus_cfg_t *cfg);
void          dabplus_sf_free(dabplus_sf_t *sf);

/* Total protected superframe size in bytes: bitrate_kbps * 15. */
size_t dabplus_sf_size(const dabplus_cfg_t *cfg);

/* Usable data size (before RS parity): 110 * (bitrate_kbps / 8). */
size_t dabplus_sf_data_size(const dabplus_cfg_t *cfg);

/*
 * Build one superframe from num_aus access units and RS-encode it in place.
 *
 *   au_bytes[i] / au_sizes[i] : i-th AAC access unit (raw, without CRC).
 *   out                        : destination buffer, exactly dabplus_sf_size(cfg) bytes.
 *   out_cap                    : capacity check.
 *
 * Returns 0 on success, -1 on error (AUs too large, out buffer too small, …).
 */
int dabplus_sf_build(dabplus_sf_t *sf,
                     const uint8_t *const *au_bytes,
                     const size_t *au_sizes,
                     uint8_t *out, size_t out_cap);

/* Expose the header length in bytes for a given cfg (firecode+cfg+au_start). */
size_t dabplus_sf_header_size(const dabplus_cfg_t *cfg);

/*
 * RS-encode a raw superframe body that already contains Firecode header
 * and per-AU CRCs — produced by fdk-aac-dabplus in TT_DABPLUS mode. All
 * this function does is read the 110*R data bytes from `raw`, compute
 * the column-wise RS(120,110) parity, and write the full row-major
 * 120*R byte output to `out`.
 *   raw       : fdk-aac-dabplus output, expected size dabplus_sf_data_size(cfg)
 *   raw_len   : must equal dabplus_sf_data_size(cfg)
 *   out       : destination, must fit dabplus_sf_size(cfg)
 *   out_cap   : capacity check
 * Returns 0 on success, -1 on error.
 */
int dabplus_sf_from_raw(dabplus_sf_t *sf,
                        const uint8_t *raw, size_t raw_len,
                        uint8_t *out, size_t out_cap);

#ifdef __cplusplus
}
#endif
