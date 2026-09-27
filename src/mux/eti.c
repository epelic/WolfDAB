#include "eti.h"
#include "common/crc16.h"
#include "common/log.h"

#include <string.h>

/*
 * Field layout summary (ETS 300 799 Figure 5, cross-checked against
 * ODR-DabMux DabMultiplexer.cpp). Lengths are in bytes unless stated.
 *
 *   0..3     SYNC    : ERR(1) + FSYNC(3)
 *   4..7     FC      : FCT(1) + FICF|NST(1) + FP|MID|FL_hi(1) + FL_lo(1)
 *   8..+4N   STC[N]  : 4 bytes per subchannel
 *             byte 0 = SCID<<2 | SAD_hi(2)
 *             byte 1 = SAD_lo
 *             byte 2 = TPL<<2 | STL_hi(2)       (STL in 64-bit groups)
 *             byte 3 = STL_lo
 *   +4       EOH     : MNSC(2) + HCRC(2)
 *   +FIC     FIC     : FICL*4 bytes (96 for Mode I, 128 for Mode III)
 *   +MSC     MSC     : payload bytes per subchannel, concatenated
 *   +4       EOF     : MSTCRC(2) + RFU(2)
 *   +4       TIST    : 4 bytes (0xFFFFFFFF when disabled)
 *
 * HCRC covers bytes [4 .. 4+4+4*NST+2-1] i.e. FC + STC + MNSC.
 * MSTCRC covers the FIC+MSC region.
 * Both CRCs use the "FIB" variant (CCITT register, inverted output).
 */

void eti_builder_init(eti_builder_t *b, const eti_subch_t *sub) {
    memset(b, 0, sizeof(*b));
    b->sub        = *sub;
    b->mode_id    = ETI_MODE_I;
    b->ficl_words = ETI_FICL_MODE_I;
}

static inline void put_u16_be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

size_t eti_builder_frame(eti_builder_t *b,
                         const uint8_t *fibs,
                         const uint8_t *payload,
                         uint8_t *out) {
    if (b->sub.size_bytes == 0 || (b->sub.size_bytes & 0x7) != 0) {
        LOGE("eti: subchannel size %u not multiple of 8", b->sub.size_bytes);
        return 0;
    }

    const unsigned NST      = 1;                 /* single subchannel */
    const unsigned FICL_w   = b->ficl_words;     /* 24 for Mode I     */
    const unsigned STL_dw   = b->sub.size_bytes / 8u;   /* in 64-bit words */
    const unsigned FL_w     = 1u + FICL_w + NST + (b->sub.size_bytes / 4u);

    const size_t frame_size = ((size_t)FL_w + 4u) * 4u;
    if (frame_size > ETI_MAX_FRAME) {
        LOGE("eti: frame size %zu exceeds max", frame_size);
        return 0;
    }
    memset(out, 0, frame_size);

    /* ---- SYNC ---------------------------------------------------------- */
    out[0] = 0xFF;        /* ERR = 0xFF (no error) */
    uint32_t fsync = (b->current_frame & 1u) ? ETI_FSYNC1_INV : ETI_FSYNC1;
    out[1] = (uint8_t)((fsync >> 16) & 0xFF);
    out[2] = (uint8_t)((fsync >>  8) & 0xFF);
    out[3] = (uint8_t)((fsync >>  0) & 0xFF);

    /* ---- FC ------------------------------------------------------------ */
    /* byte 4: FCT */
    out[4] = (uint8_t)(b->current_frame % 250u);
    /* byte 5: FICF(1)<<7 | NST(7) */
    out[5] = (uint8_t)((1u << 7) | (NST & 0x7F));
    /* byte 6: FP(3)<<5 | MID(2)<<3 | FL_hi(3) */
    uint8_t fp  = (uint8_t)(b->current_frame & 0x07);
    out[6] = (uint8_t)(((fp & 0x07) << 5) |
                       ((b->mode_id & 0x03) << 3) |
                       ((FL_w >> 8) & 0x07));
    out[7] = (uint8_t)(FL_w & 0xFF);

    /* ---- STC (1 subchannel) ------------------------------------------- */
    size_t idx = 8;
    out[idx + 0] = (uint8_t)(((b->sub.sub_ch_id & 0x3F) << 2) |
                             ((b->sub.start_addr_cus >> 8) & 0x03));
    out[idx + 1] = (uint8_t)(b->sub.start_addr_cus & 0xFF);
    out[idx + 2] = (uint8_t)(((b->sub.tpl & 0x3F) << 2) |
                             ((STL_dw >> 8) & 0x03));
    out[idx + 3] = (uint8_t)(STL_dw & 0xFF);
    idx += 4;

    /* ---- EOH: MNSC + HCRC --------------------------------------------- */
    /* MNSC = 0 for "no signalling"; HCRC covers bytes [4..idx+2-1] */
    put_u16_be(out + idx, 0x0000);        /* MNSC */
    uint16_t hcrc = crc16_fib(out + 4, (idx + 2) - 4);
    put_u16_be(out + idx + 2, hcrc);
    idx += 4;

    /* ---- MST: FIC + MSC ----------------------------------------------- */
    size_t mst_start = idx;
    size_t fic_len   = (size_t)FICL_w * 4u;
    memcpy(out + idx, fibs, fic_len);
    idx += fic_len;

    memcpy(out + idx, payload, b->sub.size_bytes);
    idx += b->sub.size_bytes;

    /* ---- EOF: MSTCRC + RFU -------------------------------------------- */
    uint16_t mstcrc = crc16_fib(out + mst_start, idx - mst_start);
    put_u16_be(out + idx, mstcrc);
    put_u16_be(out + idx + 2, 0xFFFF);    /* RFU */
    idx += 4;

    /* ---- TIST ---------------------------------------------------------- */
    out[idx + 0] = 0xFF;                  /* RFA */
    out[idx + 1] = 0xFF;
    out[idx + 2] = 0xFF;
    out[idx + 3] = 0xFF;
    idx += 4;

    if (idx != frame_size) {
        LOGE("eti: framed %zu bytes, expected %zu", idx, frame_size);
        return 0;
    }

    b->current_frame++;
    return frame_size;
}
