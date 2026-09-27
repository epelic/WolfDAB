#include "mod/puncture.h"

const uint32_t DAB_PI[25] = {
    0u,            /* PI[0] unused */
    0xc8888888u,   /* PI_1  */
    0xc888c888u,   /* PI_2  */
    0xc8c8c888u,   /* PI_3  */
    0xc8c8c8c8u,   /* PI_4  */
    0xccc8c8c8u,   /* PI_5  */
    0xccc8ccc8u,   /* PI_6  */
    0xccccccc8u,   /* PI_7  */
    0xccccccccu,   /* PI_8  */
    0xecccccccu,   /* PI_9  */
    0xeccceccc,    /* PI_10 */
    0xecececccu,   /* PI_11 */
    0xecececec,    /* PI_12 */
    0xeeececec,    /* PI_13 */
    0xeeeceeec,    /* PI_14 */
    0xeeeeeeec,    /* PI_15 */
    0xeeeeeeee,    /* PI_16 */
    0xfeeeeeee,    /* PI_17 */
    0xfeeefeee,    /* PI_18 */
    0xfefefeee,    /* PI_19 */
    0xfefefefe,    /* PI_20 */
    0xfffefefe,    /* PI_21 */
    0xfffefffe,    /* PI_22 */
    0xfffffffe,    /* PI_23 */
    0xffffffff,    /* PI_24 */
};

static void write_bit(uint8_t *out, size_t bit_index, unsigned bit) {
    size_t B = bit_index >> 3;
    unsigned b = 7u - (unsigned)(bit_index & 7u);
    out[B] = (uint8_t)((out[B] & (uint8_t)~(1u << b)) | ((bit & 1u) << b));
}

size_t dab_punct_apply(const dab_punct_program_t *prog,
                       const uint8_t *mother, size_t mother_bytes,
                       uint8_t *out) {
    size_t in_idx = 0;
    size_t out_bit = 0;

    /* Main rules. */
    for (size_t r = 0; r < prog->n_rules; ++r) {
        size_t len_remaining = prog->rules[r].length_bytes;
        uint32_t pattern = prog->rules[r].pattern;
        while (len_remaining > 0) {
            uint32_t mask = 0x80000000u;
            for (int i = 0; i < 4; ++i) {
                uint8_t data = mother[in_idx++];
                for (int j = 0; j < 8; ++j) {
                    if (pattern & mask) {
                        write_bit(out, out_bit, (unsigned)(data >> 7) & 1u);
                        ++out_bit;
                    }
                    data = (uint8_t)(data << 1);
                    mask >>= 1;
                }
            }
            len_remaining -= 4;
        }
    }

    /* Tail rule: exactly 3 bytes, 24-bit mask. */
    (void)mother_bytes;
    {
        uint32_t mask = 0x800000u;
        uint32_t pattern = prog->tail_pattern;
        for (int i = 0; i < 3; ++i) {
            uint8_t data = mother[in_idx++];
            for (int j = 0; j < 8; ++j) {
                if (pattern & mask) {
                    write_bit(out, out_bit, (unsigned)(data >> 7) & 1u);
                    ++out_bit;
                }
                data = (uint8_t)(data << 1);
                mask >>= 1;
            }
        }
    }

    size_t logical_bits = out_bit;
    /* Clear unused bits in the final output byte. */
    while (out_bit & 7u) {
        write_bit(out, out_bit, 0);
        ++out_bit;
    }
    return logical_bits;
}

size_t dab_punct_out_bits(const dab_punct_program_t *prog) {
    size_t bits = 0;
    for (size_t r = 0; r < prog->n_rules; ++r) {
        size_t iters = prog->rules[r].length_bytes / 4u;
        bits += iters * (size_t)__builtin_popcount(prog->rules[r].pattern);
    }
    bits += (size_t)__builtin_popcount(prog->tail_pattern & 0xffffffu);
    return bits;
}

int dab_eep3a_program(int bitrate_kbps,
                      dab_punct_rule_t rules_out[2],
                      dab_punct_program_t *prog) {
    if (bitrate_kbps < 8 || bitrate_kbps > 384 || bitrate_kbps % 8 != 0) {
        return -1;
    }
    /* EN 300 401 Table 8, EEP-A level 3:
     *   rule 1: ((6n - 3) * 16) bytes, PI_8
     *   rule 2: (     3 * 16) bytes, PI_7
     *   tail:   0xcccccc
     * where n = bitrate_kbps / 8 (= subchannel input bytes / 24 / 16 ...
     * but concretely ODR uses `6 * bitrate / 8` directly). */
    rules_out[0].length_bytes = (size_t)(((6 * bitrate_kbps / 8) - 3) * 16);
    rules_out[0].pattern      = DAB_PI[8];
    rules_out[1].length_bytes = (size_t)(3 * 16);
    rules_out[1].pattern      = DAB_PI[7];
    prog->rules        = rules_out;
    prog->n_rules      = 2;
    prog->tail_pattern = 0xccccccu;
    return 0;
}

int dab_eep_program(int bitrate_kbps, dab_eep_profile_t profile,
                    dab_punct_rule_t rules_out[2], dab_punct_program_t *prog) {
    if (!rules_out || !prog || bitrate_kbps < 8 || bitrate_kbps > 384) return -1;
    unsigned option, level;
    if (dab_eep_fig01_fields(profile, &option, &level) != 0 ||
        dab_eep_cu_for_bitrate((unsigned)bitrate_kbps, profile) == 0) return -1;
    const int n = option ? bitrate_kbps / 32 : bitrate_kbps / 8;
    unsigned p1 = 0, p2 = 0;
    int l1 = 0, l2 = 3 * 16;
    if (!option) {
        switch (level) {
        case 0: l1 = (6 * n - 3) * 16; p1 = 24; p2 = 23; break;
        case 1: l1 = (2 * n - 3) * 16; l2 = (4 * n + 3) * 16; p1 = 14; p2 = 13; break;
        case 2: l1 = (6 * n - 3) * 16; p1 = 8;  p2 = 7;  break;
        case 3: l1 = (4 * n - 3) * 16; l2 = (2 * n + 3) * 16; p1 = 3;  p2 = 2;  break;
        default: return -1;
        }
    } else {
        static const unsigned first[] = {10, 6, 4, 2};
        static const unsigned second[] = {9, 5, 3, 1};
        l1 = (24 * n - 3) * 16; p1 = first[level]; p2 = second[level];
    }
    if (l1 < 0 || l2 < 0) return -1;
    rules_out[0] = (dab_punct_rule_t){ (size_t)l1, DAB_PI[p1] };
    rules_out[1] = (dab_punct_rule_t){ (size_t)l2, DAB_PI[p2] };
    prog->rules = rules_out; prog->n_rules = 2; prog->tail_pattern = 0xccccccu;
    return 0;
}
