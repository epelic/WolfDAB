#include "mod/msc_code.h"

#include "common/prbs.h"
#include "mod/conv_enc.h"
#include "mod/puncture.h"
#include "mod/time_il.h"

#include <stdlib.h>
#include <string.h>

/* Maximum subchannel sizes for stack buffers (bitrate 384 kbps):
 *   in  = 384 * 3       = 1152
 *   mother = 1152*4 + 3 = 4611
 *   out = 384 * 6       = 2304
 */
#define DAB_MSC_MAX_IN      1152u
#define DAB_MSC_MAX_MOTHER  4611u
#define DAB_MSC_MAX_OUT     6912u

struct dab_msc_subch {
    int                 bitrate_kbps;
    dab_eep_profile_t   profile;
    unsigned            start_address_cu;
    size_t              in_bytes;
    size_t              out_bytes;
    size_t              mother_bytes;
    dab_punct_rule_t    rules[2];
    dab_punct_program_t prog;
    dab_time_il_t      *til;
};

dab_msc_subch_t *dab_msc_subch_new(int bitrate_kbps, unsigned start_address_cu,
                                   dab_eep_profile_t profile) {
    if (bitrate_kbps < 8 || bitrate_kbps > 384 ||
        (profile > DAB_EEP_4A && bitrate_kbps % 32 != 0) ||
        (profile <= DAB_EEP_4A && bitrate_kbps % 8 != 0)) {
        return NULL;
    }
    dab_msc_subch_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->bitrate_kbps     = bitrate_kbps;
    ctx->profile          = profile;
    ctx->start_address_cu = start_address_cu;
    ctx->in_bytes         = (size_t)bitrate_kbps * 3u;
    ctx->out_bytes        = (size_t)dab_eep_cu_for_bitrate((unsigned)bitrate_kbps, profile) * 8u;
    ctx->mother_bytes     = ctx->in_bytes * 4u + 3u;

    if (!ctx->out_bytes || dab_eep_program(bitrate_kbps, profile, ctx->rules, &ctx->prog) != 0 ||
        dab_punct_out_bits(&ctx->prog) != ctx->out_bytes * 8u) {
        free(ctx);
        return NULL;
    }
    /* dab_eep3a_program set prog.rules to the rules_out pointer we passed in,
     * which already points into ctx — no further fix-up needed. */

    ctx->til = dab_time_il_new(ctx->out_bytes);
    if (!ctx->til) {
        free(ctx);
        return NULL;
    }
    return ctx;
}

void dab_msc_subch_free(dab_msc_subch_t *ctx) {
    if (!ctx) return;
    dab_time_il_free(ctx->til);
    free(ctx);
}

size_t dab_msc_subch_in_bytes(const dab_msc_subch_t *ctx) {
    return ctx->in_bytes;
}
size_t dab_msc_subch_out_bytes(const dab_msc_subch_t *ctx) {
    return ctx->out_bytes;
}
unsigned dab_msc_subch_start_byte(const dab_msc_subch_t *ctx) {
    return ctx->start_address_cu * 8u;
}

void dab_msc_subch_encode_cif(dab_msc_subch_t *ctx,
                              const uint8_t *in, uint8_t *out) {
    uint8_t scrambled[DAB_MSC_MAX_IN];
    uint8_t mother   [DAB_MSC_MAX_MOTHER];
    uint8_t punctured[DAB_MSC_MAX_OUT];

    prbs9_t p;
    prbs9_reset(&p);
    prbs9_xor(&p, in, scrambled, ctx->in_bytes);

    dab_conv_enc_mother(scrambled, ctx->in_bytes, mother);

    dab_punct_apply(&ctx->prog, mother, ctx->mother_bytes, punctured);

    dab_time_il_process(ctx->til, punctured, out);
}

void dab_msc_frame_build_single(dab_msc_subch_t *ctx,
                                const uint8_t *in_frame,
                                uint8_t *out_frame) {
    const uint8_t *ins[1] = { in_frame };
    dab_msc_subch_t *subs[1] = { ctx };
    dab_msc_frame_build_multi(subs, ins, 1, out_frame);
}

void dab_msc_frame_build_multi(dab_msc_subch_t **subch,
                               const uint8_t *const *in_frames,
                               int n,
                               uint8_t *out_frame) {
    memset(out_frame, 0, DAB_MSC_FRAME_BYTES);

    for (int s = 0; s < n; ++s) {
        dab_msc_subch_t *ctx = subch[s];
        uint8_t subch_out[DAB_MSC_MAX_OUT];
        size_t offset = (size_t)ctx->start_address_cu * 8u;

        for (unsigned c = 0; c < DAB_MSC_CIFS_PER_FRAME; ++c) {
            dab_msc_subch_encode_cif(ctx,
                                     in_frames[s] + (size_t)c * ctx->in_bytes,
                                     subch_out);
            uint8_t *cif = out_frame + (size_t)c * DAB_MSC_CIF_BYTES;
            memcpy(cif + offset, subch_out, ctx->out_bytes);
        }
    }
}
