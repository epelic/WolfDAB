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

/* DLS charset 0 is the broadly-supported EBU Latin alphabet, not UTF-8.
 * Metadata from HTTP streams is UTF-8, so passing its bytes through makes
 * accents appear as two garbage characters on ordinary receivers.  Keep the
 * Western-European repertoire as one byte and use readable replacements for
 * punctuation outside it. */
static int dls_utf8_to_ebu_latin(uint8_t *out, int cap, const char *text) {
    const uint8_t *s = (const uint8_t *)text;
    int n = 0;
    while (*s && n < cap) {
        uint32_t cp;
        if (s[0] < 0x80) { cp = *s++; }
        else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
            cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F); s += 2;
        } else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
            cp = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F); s += 3;
        } else { ++s; cp = '?'; }
        if (cp >= 0x20 && cp <= 0x7F) out[n++] = (uint8_t)cp;
        /* ETSI TS 101 756 Annex C: EBU Latin is not ISO-8859-1. */
        else switch (cp) {
        case 0x00E1: out[n++]=0x80; break; case 0x00E0: out[n++]=0x81; break;
        case 0x00E9: out[n++]=0x82; break; case 0x00E8: out[n++]=0x83; break;
        case 0x00ED: out[n++]=0x84; break; case 0x00EC: out[n++]=0x85; break;
        case 0x00F3: out[n++]=0x86; break; case 0x00F2: out[n++]=0x87; break;
        case 0x00FA: out[n++]=0x88; break; case 0x00F9: out[n++]=0x89; break;
        case 0x00D1: out[n++]=0x8A; break; case 0x00C7: out[n++]=0x8B; break;
        case 0x00DF: out[n++]=0x8D; break;
        case 0x00E2: out[n++]=0x90; break; case 0x00E4: out[n++]=0x91; break;
        case 0x00EA: out[n++]=0x92; break; case 0x00EB: out[n++]=0x93; break;
        case 0x00EE: out[n++]=0x94; break; case 0x00EF: out[n++]=0x95; break;
        case 0x00F4: out[n++]=0x96; break; case 0x00F6: out[n++]=0x97; break;
        case 0x00FB: out[n++]=0x98; break; case 0x00FC: out[n++]=0x99; break;
        case 0x00F1: out[n++]=0x9A; break; case 0x00E7: out[n++]=0x9B; break;
        case 0x00C1: out[n++]=0xC0; break; case 0x00C0: out[n++]=0xC1; break;
        case 0x00C9: out[n++]=0xC2; break; case 0x00C8: out[n++]=0xC3; break;
        case 0x00CD: out[n++]=0xC4; break; case 0x00CC: out[n++]=0xC5; break;
        case 0x00D3: out[n++]=0xC6; break; case 0x00D2: out[n++]=0xC7; break;
        case 0x00DA: out[n++]=0xC8; break; case 0x00D9: out[n++]=0xC9; break;
        case 0x00C2: out[n++]=0xD0; break; case 0x00C4: out[n++]=0xD1; break;
        case 0x00CA: out[n++]=0xD2; break; case 0x00CB: out[n++]=0xD3; break;
        case 0x00CE: out[n++]=0xD4; break; case 0x00CF: out[n++]=0xD5; break;
        case 0x00D4: out[n++]=0xD6; break; case 0x00D6: out[n++]=0xD7; break;
        case 0x00DB: out[n++]=0xD8; break; case 0x00DC: out[n++]=0xD9; break;
        case 0x2018: case 0x2019: case 0x201A: out[n++]='\''; break;
        case 0x201C: case 0x201D: case 0x00AB: case 0x00BB: out[n++]='"'; break;
        case 0x2013: case 0x2014: case 0x2212: out[n++]='-'; break;
        default: out[n++]='?'; break;
        }
    }
    return n;
}

void dls_enc_set_text(dls_enc_t *e, const char *text) {
    if (!e || !text) return;
    e->text_len = dls_utf8_to_ebu_latin(e->text, DLS_MAX_TEXT, text);
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
