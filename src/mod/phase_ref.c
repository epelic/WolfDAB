#include "mod/phase_ref.h"

/* ETSI EN 300 401 Table 43: h_{i,k} values, i in {0..3}, k in {0..31}.
 * The table is repeated twice (64 entries) to let the fill loop ignore
 * the carrier-k' offset arithmetic — ODR-DabMod uses the same trick. */
static const unsigned char H_TABLE[4][32] = {
    { 0, 2, 0, 0, 0, 0, 1, 1, 2, 0, 0, 0, 2, 2, 1, 1,
      0, 2, 0, 0, 0, 0, 1, 1, 2, 0, 0, 0, 2, 2, 1, 1 },
    { 0, 3, 2, 3, 0, 1, 3, 0, 2, 1, 2, 3, 2, 3, 3, 0,
      0, 3, 2, 3, 0, 1, 3, 0, 2, 1, 2, 3, 2, 3, 3, 0 },
    { 0, 0, 0, 2, 0, 2, 1, 3, 2, 2, 0, 2, 2, 0, 1, 3,
      0, 0, 0, 2, 0, 2, 1, 3, 2, 2, 0, 2, 2, 0, 1, 3 },
    { 0, 1, 2, 1, 0, 3, 3, 2, 2, 3, 2, 1, 2, 1, 3, 2,
      0, 1, 2, 1, 0, 3, 3, 2, 2, 3, 2, 1, 2, 1, 3, 2 },
};

/* EN 300 401 Table 44 (Mode I): 48 (i, n) pairs — 24 for the positive
 * carriers (k' = 1, 33, 65, ..., 737) and 24 for the negative carriers
 * (k' = -768, -736, ..., -32). Each pair generates 32 consecutive
 * carriers in order.
 *
 * The output order produced by this table is
 *   carriers +1..+768 followed by -768..-1
 * which matches the logical ordering used by ofdm_symbol_build(). */
static const unsigned char MODE1_TABLE[48][2] = {
    /* Positive carriers k = +1..+768 */
    { 0, 3 }, { 3, 1 }, { 2, 1 }, { 1, 1 }, { 0, 2 }, { 3, 2 },
    { 2, 1 }, { 1, 0 }, { 0, 2 }, { 3, 2 }, { 2, 3 }, { 1, 3 },
    { 0, 0 }, { 3, 2 }, { 2, 1 }, { 1, 3 }, { 0, 3 }, { 3, 3 },
    { 2, 3 }, { 1, 0 }, { 0, 3 }, { 3, 0 }, { 2, 1 }, { 1, 1 },
    /* Negative carriers k = -768..-1 */
    { 0, 1 }, { 1, 2 }, { 2, 0 }, { 3, 1 }, { 0, 3 }, { 1, 2 },
    { 2, 2 }, { 3, 3 }, { 0, 2 }, { 1, 1 }, { 2, 2 }, { 3, 3 },
    { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 3 }, { 0, 2 }, { 1, 2 },
    { 2, 2 }, { 3, 1 }, { 0, 1 }, { 1, 3 }, { 2, 1 }, { 3, 2 },
};

static float _Complex qpsk_value(unsigned phi) {
    /* z = j^phi for phi in {0,1,2,3}. */
    switch (phi & 3u) {
        case 0: return  1.0f + 0.0f * (float _Complex)I;
        case 1: return  0.0f + 1.0f * (float _Complex)I;
        case 2: return -1.0f + 0.0f * (float _Complex)I;
        default: return 0.0f - 1.0f * (float _Complex)I;
    }
}

void dab_phase_ref_mode1(float _Complex *out) {
    size_t index = 0;
    for (size_t offset = 0; offset < 48; ++offset) {
        unsigned i = MODE1_TABLE[offset][0];
        unsigned n = MODE1_TABLE[offset][1];
        for (size_t k = 0; k < 32; ++k) {
            out[index++] = qpsk_value(H_TABLE[i][k] + n);
        }
    }
}
