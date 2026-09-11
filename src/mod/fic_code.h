#pragma once
/*
 * DAB Fast Information Channel codec (ETSI EN 300 401 §11.2).
 *
 * Each FIC block (one CIF) holds 3 FIBs × 32 bytes = 96 bytes of logical
 * FIC data. The encoder applies
 *   1. PRBS energy dispersal (9-bit LFSR, polynomial x^9 + x^5 + 1,
 *      reset to all-ones per CIF — shared with src/common/prbs.h)
 *   2. mother convolutional encoder (rate 1/4, K = 7) with 6 zero tail bits
 *   3. FIC puncturing profile (Mode I): two rules
 *        (21 × 16 bytes, 0xEEEEEEEE) and (3 × 16 bytes, 0xEEEEEEEC)
 *      plus the shared tail pattern 0xCCCCCC.
 *
 * The output is 2304 bits (= 288 bytes) per CIF, and 9216 bits
 * (= 1152 bytes) per RF frame of 4 CIFs — exactly the three FIC OFDM
 * symbols (3 × 1536 × 2 bits) the frame assembler consumes.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAB_FIC_BYTES_PER_CIF        96u
#define DAB_FIC_OUT_BITS_PER_CIF     2304u
#define DAB_FIC_OUT_BYTES_PER_CIF    288u      /* 2304 / 8 */
#define DAB_FIC_CIFS_PER_FRAME       4u
#define DAB_FIC_FRAME_IN_BYTES       384u      /* 4 × 96 */
#define DAB_FIC_FRAME_OUT_BYTES      1152u     /* 4 × 288 */

/* Encode a single CIF's worth of FIC: 96 bytes in → 288 bytes out.
 * Output is packed MSB-first. */
void dab_fic_encode_cif(const uint8_t *fic_in, uint8_t *fic_out);

/* Encode the 4 CIFs of one RF frame: 384 bytes in → 1152 bytes out.
 * CIFs are processed independently and concatenated in-order. */
void dab_fic_encode_frame(const uint8_t *fic_in_frame, uint8_t *fic_out_frame);

#ifdef __cplusplus
}
#endif
