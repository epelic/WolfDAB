#pragma once
/*
 * ETI(NI) frame builder — variable-length form.
 *
 * Assembles one 24 ms logical ETI frame according to ETS 300 799 §5 using
 * the byte layouts cross-referenced against ODR-DabMux. For Transmission
 * Mode I the FIC carries 3 FIBs per logical frame (FICL = 24 × 4 = 96 bytes).
 *
 * The "network independent" variant produces exactly the number of bytes
 * that the frame actually uses:
 *     frame_size = (FL + 4) × 4 bytes,     FL = 1 + FICL + NST + sum(STL)
 * where FL is in 32-bit words and STL is in dwords (64 bit). The G.703
 * transport layer would further pad this to 6144 bytes. We stick with the
 * unpadded form because every ODR tool consumes it unchanged.
 *
 * Scope for this milestone: a single audio subchannel with EEP protection.
 * Multiple subchannels require only extending the STC loop and the MST
 * copy, which we do when we need it.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ETI_FSYNC1        0x49C5F8u   /* frame counter even */
#define ETI_FSYNC1_INV    0xB63A07u   /* frame counter odd  */
#define ETI_MAX_FRAME     6144u

#define ETI_MODE_I        1
#define ETI_FICL_MODE_I   24          /* FIC length in 32-bit words */

typedef struct {
    /* Sub-channel characterization written into the STC field. */
    uint8_t  sub_ch_id;      /* 6 bits */
    uint16_t start_addr_cus; /* 10 bits */
    uint8_t  tpl;            /* 6 bits, e.g. 0x22 = EEP-3A (0x20 | Opt<<2 | Level) */
    uint16_t size_bytes;     /* payload bytes per frame (multiple of 8)  */
} eti_subch_t;

typedef struct {
    uint32_t     current_frame; /* FCT counter, grows with each frame */
    eti_subch_t  sub;           /* single subchannel for now           */
    uint8_t      mode_id;       /* ETI MID: 1 = Mode I                 */
    uint8_t      ficl_words;    /* FIC length in words, 24 for Mode I  */
} eti_builder_t;

void eti_builder_init(eti_builder_t *b, const eti_subch_t *sub);

/*
 * Build one ETI(NI) frame. The caller supplies:
 *   - `fibs`:     pointer to ficl_words*4 bytes (3 × 32 byte FIBs for Mode I)
 *   - `payload`:  sub.size_bytes bytes of pre-coded subchannel data
 *   - `out`:      destination buffer (must hold at least ETI_MAX_FRAME bytes)
 *
 * Returns the number of bytes actually written (variable, without G.703 pad)
 * or 0 on error. Advances current_frame on success.
 */
size_t eti_builder_frame(eti_builder_t *b,
                         const uint8_t *fibs,
                         const uint8_t *payload,
                         uint8_t *out);

#ifdef __cplusplus
}
#endif
