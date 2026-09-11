#include "dls.h"
#include "common/crc16.h"
#include "common/log.h"

#include <stdlib.h>
#include <string.h>

/*
 * DLS Data Group format (per ETSI EN 300 401 §7.4.5.2):
 *
 *   Prefix Byte 0: T(1) | F(1) | L(1) | C(1) | SegLen-1(4)
 *   Prefix Byte 1: if F=1: Charset(4) | Rfu(4)
 *                  if F=0: SegIndex(4) | Rfu(4)
 *   Character Field: 1..16 bytes of text
 *   CRC: 2 bytes CRC-16 CCITT (init 0xFFFF, inverted)
 *
 * PAD frame layout for fdk-aac ancillary data:
 *   [X-PAD data bytes] [F-PAD byte 0] [F-PAD byte 1]
 *
 * X-PAD (Variable Size with CI):
 *   Data subfield (DG bytes) | zero-pad | CI end marker | CI byte
 *   <-- low address                              high address -->
 *
 * With maxAncBytes=245 at 72 kbps, we have plenty of room to pack
 * the entire DG (max 20 bytes) in a single X-PAD subfield.
 */

struct dls_enc {
    /* Segmented text. */
    uint8_t  text[DLS_MAX_TEXT];
    int      text_len;
    int      charset;
    int      toggle;

    /* Pre-built Data Groups. */
    uint8_t  dg[DLS_MAX_SEGS][DLS_MAX_DG];
    int      dg_len[DLS_MAX_SEGS];
    int      n_segs;

    /* Current position in the cycle. */
    int      cur_seg;
};

/* CI Length-Index → subfield size table. */
static const int ci_sizes[] = { 4, 6, 8, 12, 16, 24, 32, 48 };

/* Find the smallest CI length index that fits `need` bytes. */
static int ci_len_index(int need) {
    for (int i = 0; i < 8; ++i)
        if (ci_sizes[i] >= need) return i;
    return 7; /* 48, maximum */
}

static void build_data_groups(dls_enc_t *e) {
    int len = e->text_len;
    if (len == 0) { e->n_segs = 0; return; }

    e->n_segs = (len + 15) / 16;
    int offset = 0;

    for (int s = 0; s < e->n_segs; ++s) {
        int seg_len = len - offset;
        if (seg_len > 16) seg_len = 16;

        int first = (s == 0);
        int last  = (s == e->n_segs - 1);

        /* Prefix Byte 0: T | F | L | C=0 | (seg_len-1) */
        uint8_t p0 = (uint8_t)(
            ((e->toggle & 1) << 7) |
            (first           << 6) |
            (last            << 5) |
            ((seg_len - 1)   << 0));

        /* Prefix Byte 1: charset or segment index */
        uint8_t p1;
        if (first)
            p1 = (uint8_t)((e->charset & 0x0F) << 4);
        else
            p1 = (uint8_t)((s & 0x0F) << 4);

        uint8_t *dg = e->dg[s];
        dg[0] = p0;
        dg[1] = p1;
        memcpy(dg + 2, e->text + offset, (size_t)seg_len);

        /* CRC-16 CCITT over prefix + text, then invert. */
        int body_len = 2 + seg_len;
        uint16_t crc = (uint16_t)(crc16_ccitt(dg, (size_t)body_len) ^ 0xFFFF);
        dg[body_len + 0] = (uint8_t)(crc >> 8);
        dg[body_len + 1] = (uint8_t)(crc & 0xFF);

        e->dg_len[s] = body_len + 2;
        offset += seg_len;
    }
}

dls_enc_t *dls_enc_new(const char *text, int charset) {
    dls_enc_t *e = (dls_enc_t *)calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->charset = charset;
    e->toggle  = 0;
    dls_enc_set_text(e, text);
    return e;
}

void dls_enc_free(dls_enc_t *e) {
    free(e);
}

void dls_enc_set_text(dls_enc_t *e, const char *text) {
    if (!e || !text) return;
    e->text_len = (int)strlen(text);
    if (e->text_len > DLS_MAX_TEXT) e->text_len = DLS_MAX_TEXT;
    memcpy(e->text, text, (size_t)e->text_len);
    e->toggle ^= 1;
    e->cur_seg = 0;
    build_data_groups(e);
}

int dls_enc_get_xpad(dls_enc_t *e,
                     uint8_t *xpad_out, size_t xpad_cap,
                     uint8_t *fpad0, uint8_t *fpad1) {
    if (!e || e->n_segs == 0 || xpad_cap < DLS_XPAD_MAX) return 0;

    int seg = e->cur_seg;
    int dg_sz = e->dg_len[seg];

    /* Find CI length index for this DG. */
    int li = ci_len_index(dg_sz);
    int subfield_sz = ci_sizes[li];

    /* X-PAD = subfield + CI byte + CI end marker. */
    int xpad_sz = subfield_sz + 2;

    memset(xpad_out, 0, (size_t)xpad_sz);

    /*
     * X-PAD layout in reversed byte order (qt-dab reads from high to low):
     *
     * Index:  0 ... (zero-pad) ... (DG last) ... (DG first) (end=0x00) (CI)
     *         low addr                                                 high addr
     *
     * CI byte at the highest index (nearest to F-PAD in the AU).
     * End marker just below CI.
     * DG data: first logical byte nearest to end marker.
     */
    xpad_out[xpad_sz - 1] = (uint8_t)((li << 5) | 0x02); /* CI: LenIndex | AppType=2 */
    xpad_out[xpad_sz - 2] = 0x00;                         /* CI end marker */

    for (int j = 0; j < dg_sz; ++j)
        xpad_out[xpad_sz - 3 - j] = e->dg[seg][j];

    /* F-PAD values for Variable Size X-PAD with CI. */
    *fpad0 = 0x20;  /* X-PAD indicator = 10 (Variable Size) */
    *fpad1 = 0x02;  /* CI flag = 1 */

    /* Debug: hex dump first segment once. */
    static int dbg = 0;
    if (!dbg) {
        LOGI("dls xpad seg%d: xpad_sz=%d dg_sz=%d li=%d subfield=%d",
             seg, xpad_sz, dg_sz, li, subfield_sz);
        /* Dump DG bytes */
        char hex[128] = {0};
        int pos = 0;
        for (int j = 0; j < dg_sz && pos < 120; ++j)
            pos += snprintf(hex + pos, sizeof(hex) - (size_t)pos, "%02x ", e->dg[seg][j]);
        LOGI("dls DG[%d]: %s", dg_sz, hex);
        /* Dump X-PAD output */
        pos = 0;
        hex[0] = 0;
        for (int j = 0; j < xpad_sz && pos < 120; ++j)
            pos += snprintf(hex + pos, sizeof(hex) - (size_t)pos, "%02x ", xpad_out[j]);
        LOGI("dls XPAD[%d]: %s", xpad_sz, hex);
        dbg = 1;
    }

    /* Advance to next segment. */
    e->cur_seg = (seg + 1) % e->n_segs;

    return xpad_sz;
}
