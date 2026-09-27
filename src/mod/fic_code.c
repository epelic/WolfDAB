#include "mod/fic_code.h"

#include "common/prbs.h"
#include "mod/conv_enc.h"
#include "mod/puncture.h"

#include <string.h>

/*
 * FIC Mode I puncturing per EN 300 401 §11.2.
 *
 * Rule length counts in mother-encoder bytes consumed (see puncture.h):
 *   rule 1: 21 × 16 = 336 bytes, pattern 0xEEEEEEEE
 *   rule 2:  3 × 16 =  48 bytes, pattern 0xEEEEEEEC
 *   tail  :         3 bytes, pattern 0xCCCCCC
 *
 * Sum: 336 + 48 + 3 = 387 = 96 × 4 + 3 = mother output length ✓.
 * Bits out: 84·24 + 12·23 + 12 = 2304 ✓.
 */
static const dab_punct_rule_t FIC_RULES[2] = {
    { .length_bytes = 21u * 16u, .pattern = 0xEEEEEEEEu },
    { .length_bytes =  3u * 16u, .pattern = 0xEEEEEEECu },
};
static const dab_punct_program_t FIC_PROG = {
    .rules        = FIC_RULES,
    .n_rules      = 2,
    .tail_pattern = 0xCCCCCCu,
};

void dab_fic_encode_cif(const uint8_t *fic_in, uint8_t *fic_out) {
    uint8_t scrambled[DAB_FIC_BYTES_PER_CIF];
    uint8_t mother[DAB_CONV_OUT_BYTES(DAB_FIC_BYTES_PER_CIF)];  /* 387 */

    prbs9_t p;
    prbs9_reset(&p);
    prbs9_xor(&p, fic_in, scrambled, DAB_FIC_BYTES_PER_CIF);

    dab_conv_enc_mother(scrambled, DAB_FIC_BYTES_PER_CIF, mother);

    dab_punct_apply(&FIC_PROG, mother, sizeof(mother), fic_out);
}

void dab_fic_encode_frame(const uint8_t *fic_in_frame, uint8_t *fic_out_frame) {
    for (unsigned c = 0; c < DAB_FIC_CIFS_PER_FRAME; ++c) {
        dab_fic_encode_cif(
            fic_in_frame  + (size_t)c * DAB_FIC_BYTES_PER_CIF,
            fic_out_frame + (size_t)c * DAB_FIC_OUT_BYTES_PER_CIF);
    }
}
