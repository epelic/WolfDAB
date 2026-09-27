#pragma once
/*
 * DAB puncturing for the mother convolutional encoder output
 * (ETSI EN 300 401 §11.1.2, puncturing vectors Table in Annex B).
 *
 * A puncturing rule consumes a contiguous span of the mother-encoder output
 * stream and applies a 32-bit pattern repeatedly, 4 mother bytes per
 * iteration. Bits set in the pattern (counting from MSB = bit 31) are kept,
 * bits cleared are discarded.
 *
 * All bit packings are MSB-first within bytes, matching the rest of the DAB
 * pipeline.
 */

#include <stddef.h>
#include <stdint.h>
#include "mux/eep_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DAB_PI[k] is the 32-bit mask for puncturing index PI_k (k = 1..24).
 * PI_24 = 0xFFFFFFFF = rate 1/4 (no puncturing).
 * PI_8  = 0xCCCCCCCC = every other bit (rate 1/2). */
extern const uint32_t DAB_PI[25];

typedef struct {
    size_t   length_bytes;  /* mother-encoder bytes consumed by this rule;
                               must be a multiple of 4 */
    uint32_t pattern;       /* 32-bit puncturing mask */
} dab_punct_rule_t;

typedef struct {
    const dab_punct_rule_t *rules;
    size_t                  n_rules;
    uint32_t                tail_pattern;    /* 24-bit tail pattern */
} dab_punct_program_t;

/* Apply a program to the mother-encoder output. Writes packed MSB-first
 * output bits to `out` and returns the total number of output bits.
 *
 * Invariant: sum(rules.length_bytes) + 3 (tail bytes) must equal
 * mother_bytes. The tail rule always consumes the final 3 mother bytes and
 * applies `tail_pattern` (24 bits, MSB = bit 23) once.
 *
 * `out` must hold at least ceil(out_bits / 8) bytes; trailing unused bits
 * in the final byte are cleared to zero. */
size_t dab_punct_apply(const dab_punct_program_t *prog,
                       const uint8_t *mother, size_t mother_bytes,
                       uint8_t *out);

/* Expected number of output bits for a program, computed from the
 * patterns (no mother data required). */
size_t dab_punct_out_bits(const dab_punct_program_t *prog);

/*
 * EEP-3A profile builder (EN 300 401 §11.3.2, Table 8, option A level 3).
 *
 * Populates `rules_out[0..1]` and `prog` for the given subchannel bitrate
 * (multiple of 8 kbps, 32..384). Subchannel input is `bitrate * 24 / 8`
 * bytes per logical frame (24 ms); output is exactly `bitrate * 6` bits,
 * i.e. the CU count `bitrate * 6 / 8`.
 *
 * Returns 0 on success, non-zero on invalid bitrate.
 */
int dab_eep3a_program(int bitrate_kbps,
                      dab_punct_rule_t rules_out[2],
                      dab_punct_program_t *prog);

/* Long-form EEP-A/EEP-B encoder configuration, EN 300 401 Table 8. */
int dab_eep_program(int bitrate_kbps, dab_eep_profile_t profile,
                    dab_punct_rule_t rules_out[2],
                    dab_punct_program_t *prog);

#ifdef __cplusplus
}
#endif
