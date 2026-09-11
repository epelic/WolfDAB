#include "superframe.h"
#include "common/bitbuf.h"
#include "common/crc16.h"
#include "common/log.h"

#include "fec.h"      /* libfec: RS(120,110) via init_rs_char(8,0x11d,0,1,10,135) */

#include <stdlib.h>
#include <string.h>

/* DAB+ superframe header layout, 48 kHz AAC-LC, 6 AUs:
 *
 *   bytes 0..1   firecode (CRC-16 over bytes 2..10)
 *   byte  2      rfa(1) | dac_rate(1) | sbr_flag(1) | aac_channel_mode(1)
 *                 | ps_flag(1) | mpeg_surround_config(3)
 *   bytes 3..10  au_start[1..5]  (5 × 12 bits = 60 bits, padded with 4 zero bits)
 *
 * For other num_aus values the au_start table has a different size, so the
 * header shrinks or grows accordingly. We compute it from num_aus.
 */

struct dabplus_sf {
    dabplus_cfg_t cfg;
    void         *rs;        /* libfec RS(120,110) handle */
    size_t        sf_size;   /* total bytes per protected super frame */
    size_t        data_size; /* 110 * R (R columns) */
    size_t        header_sz;
};

static size_t header_size_bytes(int num_aus) {
    /* 2 (firecode) + 1 (config) + ceil((num_aus-1)*12 / 8) */
    int au_bits = (num_aus - 1) * 12;
    int au_bytes = (au_bits + 7) / 8;
    return (size_t)(2 + 1 + au_bytes);
}

size_t dabplus_sf_size(const dabplus_cfg_t *cfg)      { return (size_t)cfg->bitrate_kbps * 15u; }
size_t dabplus_sf_data_size(const dabplus_cfg_t *cfg) { return 110u * ((size_t)cfg->bitrate_kbps / 8u); }
size_t dabplus_sf_header_size(const dabplus_cfg_t *cfg) { return header_size_bytes(cfg->num_aus); }

dabplus_sf_t *dabplus_sf_new(const dabplus_cfg_t *cfg) {
    if (!cfg || cfg->bitrate_kbps <= 0 || (cfg->bitrate_kbps % 8) != 0) return NULL;
    if (cfg->num_aus != 2 && cfg->num_aus != 3 &&
        cfg->num_aus != 4 && cfg->num_aus != 6) return NULL;

    dabplus_sf_t *sf = (dabplus_sf_t *)calloc(1, sizeof(*sf));
    if (!sf) return NULL;
    sf->cfg       = *cfg;
    sf->sf_size   = dabplus_sf_size(cfg);
    sf->data_size = dabplus_sf_data_size(cfg);
    sf->header_sz = header_size_bytes(cfg->num_aus);

    /* RS(120,110): shortened RS(255,245), GF(2^8), poly 0x11d,
     * fcr=0, prim=1, nroots=10, pad=135. */
    sf->rs = init_rs_char(8, 0x11d, 0, 1, 10, 135);
    if (!sf->rs) { free(sf); return NULL; }
    return sf;
}

void dabplus_sf_free(dabplus_sf_t *sf) {
    if (!sf) return;
    if (sf->rs) free_rs_char(sf->rs);
    free(sf);
}

/*
 * Pack the superframe header into data[0..header_sz-1]. The firecode in
 * bytes 0..1 is filled LAST because it depends on bytes 2..10.
 *
 * Per ETSI TS 102 563 §5.2, the firecode is always computed over the 9
 * bytes at data[2..10], regardless of num_aus. For num_aus=6 those 9
 * bytes are entirely header (config + 8 au_start bytes). For smaller
 * num_aus (e.g. 3), the logical header is shorter (6 bytes) and bytes
 * 6..10 hold the beginning of AU 0 — the firecode still guards them.
 * qt-dab's firecodeChecker::crc16 hardcodes the 11-byte span, so any
 * num_aus that doesn't write a full 11-byte firecode-guarded region
 * will be silently rejected. Caller must therefore fill AU 0 content
 * BEFORE calling write_header() so bytes 6..10 are already populated.
 */
static int write_header(const dabplus_sf_t *sf,
                        const size_t *au_start,   /* byte offsets of each AU */
                        uint8_t *data) {
    const dabplus_cfg_t *c = &sf->cfg;

    /* Byte 2: rfa(1)=0 | dac_rate(1) | sbr_flag(1) | aac_channel_mode(1)
     *         | ps_flag(1) | mpeg_surround_config(3) */
    data[2] = (uint8_t)(
        ((c->dac_rate         & 0x01) << 6) |
        ((c->sbr_flag         & 0x01) << 5) |
        ((c->aac_channel_mode & 0x01) << 4) |
        ((c->ps_flag          & 0x01) << 3) |
        ((c->mpeg_surround    & 0x07) << 0));

    /* Bytes 3..: au_start[1..num_aus-1], 12 bits each, MSB-first. */
    bitbuf_t bb;
    size_t   au_field_bytes = sf->header_sz - 3;
    bitbuf_init(&bb, data + 3, au_field_bytes);
    for (int i = 1; i < c->num_aus; ++i) {
        if (au_start[i] > 0xFFFu) return -1;
        if (bitbuf_put(&bb, (uint32_t)au_start[i], 12) != 0) return -1;
    }
    bitbuf_align_byte(&bb);

    /* Firecode over the fixed 9-byte guarded region data[2..10]. */
    uint16_t fc = crc16_firecode(data + 2, 9);
    data[0] = (uint8_t)(fc >> 8);
    data[1] = (uint8_t)(fc & 0xFF);
    return 0;
}

/*
 * ETSI TS 102 563 §5.1: the superframe is arranged as a 120 rows × R cols
 * matrix (110 data rows + 10 parity rows, each column is an RS(120,110)
 * codeword). The logical byte stream (header + AUs + CRCs) fills the
 * 110-row data area in *row-major* order, so logical byte k lives at
 * matrix position (row k/R, col k%R). Transmission is also row-wise, so
 * the transmitted byte at position k is the matrix byte at (row k/R,
 * col k%R) — exactly the same linear index as the logical one for the
 * data area; the 10 parity rows are appended afterwards.
 *
 * That means the internal `data` buffer we fill linearly already matches
 * the row-major output layout for the data part. RS encoding reads each
 * column with stride R, and the parity goes to the bottom 10 rows.
 *
 * Layout of `data` (sf->data_size = 110 * R bytes, row-major):
 *   row0[col0..col{R-1}] row1[col0..col{R-1}] ... row109[col0..col{R-1}]
 * Output `out` (sf->sf_size = 120 * R bytes, row-major):
 *   rows 0..109 are a verbatim copy of `data`
 *   rows 110..119 hold the 10 RS parity bytes of each column
 */
int dabplus_sf_build(dabplus_sf_t *sf,
                     const uint8_t *const *au_bytes,
                     const size_t *au_sizes,
                     uint8_t *out, size_t out_cap) {
    if (out_cap < sf->sf_size) return -1;

    const size_t R         = sf->cfg.bitrate_kbps / 8;
    const size_t data_size = sf->data_size;
    const size_t header_sz = sf->header_sz;

    uint8_t *data = (uint8_t *)calloc(data_size, 1);
    if (!data) return -1;

    /* Compute AU start offsets and write AU payloads + CRCs.
     *
     * For AUs 0..num_aus-2 the CRC sits immediately after the AU payload
     * (2 bytes). The cursor advances past each AU+CRC so the next AU's
     * start address is simply the current cursor.
     *
     * The LAST AU is special: qt-dab (and every spec-compliant decoder)
     * hard-codes au_start[num_aus] = 110*R as the end of the data area
     * and computes the last AU's length as `110*R - au_start[last] - 2`.
     * That means the CRC must live at absolute offset `data_size - 2`
     * and cover every byte from au_start[last] up to (but not including)
     * that position — INCLUDING whatever zero padding is left between
     * the actual AAC bytes and the CRC.
     *
     * All AU CRCs are CRC-16/CCITT-FALSE with the final XOR 0xFFFF that
     * qt-dab's calc_crc applies (`return ~crc`). Without that inversion
     * no AU CRC check ever succeeds.
     */
    size_t au_start[8] = { 0 };    /* max num_aus = 6, use 8 for safety */
    size_t cursor = header_sz;
    const int last = sf->cfg.num_aus - 1;
    for (int i = 0; i <= last; ++i) {
        au_start[i] = cursor;
        if (i < last) {
            size_t need = au_sizes[i] + 2;
            if (cursor + need > data_size) {
                LOGE("sf: AU %d of %zu bytes does not fit (free=%zu)",
                     i, au_sizes[i], data_size - cursor);
                free(data);
                return -1;
            }
            memcpy(data + cursor, au_bytes[i], au_sizes[i]);
            uint16_t crc =
                (uint16_t)(crc16_ccitt(au_bytes[i], au_sizes[i]) ^ 0xFFFF);
            data[cursor + au_sizes[i] + 0] = (uint8_t)(crc >> 8);
            data[cursor + au_sizes[i] + 1] = (uint8_t)(crc & 0xFF);
            cursor += need;
        } else {
            /* Last AU: fixed end at data_size - 2 (CRC) / data_size. */
            if (cursor + au_sizes[i] + 2 > data_size) {
                LOGE("sf: last AU %zu bytes does not fit (free=%zu)",
                     au_sizes[i], data_size - cursor);
                free(data);
                return -1;
            }
            memcpy(data + cursor, au_bytes[i], au_sizes[i]);
            /* bytes [cursor+au_sizes[i] .. data_size-3] stay zero from calloc */
            size_t crc_pos  = data_size - 2;
            size_t crc_len  = crc_pos - cursor;   /* covers data + padding */
            uint16_t crc =
                (uint16_t)(crc16_ccitt(data + cursor, crc_len) ^ 0xFFFF);
            data[crc_pos + 0] = (uint8_t)(crc >> 8);
            data[crc_pos + 1] = (uint8_t)(crc & 0xFF);
            cursor = data_size;
        }
    }

    if (write_header(sf, au_start, data) != 0) {
        free(data);
        return -1;
    }

    /*
     * Column-wise RS encoding.
     *
     * The spec arranges the R × 110 data bytes as 110 rows × R columns
     * ROW-MAJOR in transmission, but since RS is applied per column the
     * simplest memory layout is: contiguous column 0, then column 1, ...
     * which is exactly what calloc(data_size, 1) gives us if we fed each
     * column sequentially. However the usual DAB+ interpretation is that
     * "byte k" of the unencoded superframe lives at row (k mod 110),
     * column (k / 110); that is equivalent to storing data column-wise,
     * so the memcpy()s above are correct.
     */
    /* Copy the 110 data rows verbatim into the first 110 rows of out. */
    memcpy(out, data, data_size);

    for (size_t j = 0; j < R; ++j) {
        /* Extract column j from row-major data with stride R. */
        uint8_t col_data[110];
        uint8_t parity[10];
        for (size_t row = 0; row < 110; ++row) {
            col_data[row] = data[row * R + j];
        }
        encode_rs_char(sf->rs, col_data, parity);
        /* Write the 10 parity bytes to rows 110..119 of column j. */
        for (size_t row = 0; row < 10; ++row) {
            out[(110 + row) * R + j] = parity[row];
        }
    }

    free(data);
    return 0;
}

int dabplus_sf_from_raw(dabplus_sf_t *sf,
                        const uint8_t *raw, size_t raw_len,
                        uint8_t *out, size_t out_cap) {
    if (out_cap < sf->sf_size)   return -1;
    if (raw_len != sf->data_size) return -1;

    const size_t R = sf->cfg.bitrate_kbps / 8;

    /* Data rows 0..109 are already in row-major order in `raw`. */
    memcpy(out, raw, sf->data_size);

    for (size_t j = 0; j < R; ++j) {
        uint8_t col_data[110];
        uint8_t parity[10];
        for (size_t row = 0; row < 110; ++row) {
            col_data[row] = raw[row * R + j];
        }
        encode_rs_char(sf->rs, col_data, parity);
        for (size_t row = 0; row < 10; ++row) {
            out[(110 + row) * R + j] = parity[row];
        }
    }
    return 0;
}
