#include "mod/frame.h"

#include "mod/dqpsk.h"
#include "mod/freq_il.h"
#include "mod/ofdm_symbol.h"
#include "mod/phase_ref.h"

#include <stdlib.h>
#include <string.h>

#define CARRIERS  DAB_MODE_I_CARRIERS

struct dab_frame_ctx {
    ofdm_symbol_ctx_t *ofdm;
    size_t             fi_indices[CARRIERS];
    float _Complex     prs[CARRIERS];      /* fixed PRS, physical order */
    float _Complex     prev[CARRIERS];     /* differential state */
    float _Complex     base[CARRIERS];     /* logical base QPSK buffer */
    float _Complex     base_phys[CARRIERS];/* after freq-il */
};

dab_frame_ctx_t *dab_frame_new(void) {
    dab_frame_ctx_t *ctx = (dab_frame_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->ofdm = ofdm_symbol_new();
    if (!ctx->ofdm) { free(ctx); return NULL; }
    dab_freq_il_build_mode1(ctx->fi_indices);
    dab_phase_ref_mode1(ctx->prs);
    return ctx;
}

void dab_frame_free(dab_frame_ctx_t *ctx) {
    if (!ctx) return;
    ofdm_symbol_free(ctx->ofdm);
    free(ctx);
}

/* Process one OFDM data symbol into `out_time` and advance `prev`. */
static void build_data_symbol(dab_frame_ctx_t *ctx,
                              const uint8_t *iq_bytes,
                              float _Complex *out_time) {
    dab_qpsk_base_mode1(iq_bytes, ctx->base);
    dab_freq_il_apply(ctx->fi_indices, ctx->base, ctx->base_phys);
    for (size_t k = 0; k < CARRIERS; ++k) {
        ctx->prev[k] = ctx->prev[k] * ctx->base_phys[k];
    }
    ofdm_symbol_build(ctx->ofdm, ctx->prev, out_time);
}

void dab_frame_build(dab_frame_ctx_t *ctx,
                     const uint8_t *fic_bits_frame,
                     const uint8_t *msc_bits_frame,
                     float _Complex *out) {
    /* 1. Null symbol: 2656 zero samples. */
    memset(out, 0, DAB_MODE_I_NULL_SAMPLES * sizeof(*out));
    float _Complex *p = out + DAB_MODE_I_NULL_SAMPLES;

    /* 2. Phase Reference Symbol: fixed carriers, no frequency interleaving
     *    (the PRS lives directly on physical positions). It also seeds the
     *    differential modulator for the first data symbol. */
    memcpy(ctx->prev, ctx->prs, sizeof(ctx->prs));
    ofdm_symbol_build(ctx->ofdm, ctx->prs, p);
    p += DAB_MODE_I_SYMBOL_SAMPLES;

    /* 3. FIC symbols (3). */
    for (int s = 0; s < DAB_MODE_I_FIC_SYMBOLS; ++s) {
        build_data_symbol(ctx,
                          fic_bits_frame + (size_t)s * DAB_MODE_I_SYMBOL_BITS_BYTES,
                          p);
        p += DAB_MODE_I_SYMBOL_SAMPLES;
    }

    /* 4. MSC symbols (72). */
    for (int s = 0; s < DAB_MODE_I_MSC_SYMBOLS; ++s) {
        build_data_symbol(ctx,
                          msc_bits_frame + (size_t)s * DAB_MODE_I_SYMBOL_BITS_BYTES,
                          p);
        p += DAB_MODE_I_SYMBOL_SAMPLES;
    }

    /* Sanity: the write cursor lands exactly at end of frame buffer. */
    /* (2656 + 75 * 2552 = 196608) */
}
