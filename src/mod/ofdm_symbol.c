#include "ofdm_symbol.h"

#include <fftw3.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct ofdm_symbol_ctx {
    fftwf_plan    plan;
    fftwf_complex *freq;    /* IFFT input  (length DAB_MODE_I_IFFT) */
    fftwf_complex *time;    /* IFFT output (length DAB_MODE_I_IFFT) */
    float         scale;    /* 1 / sqrt(N) normalisation factor     */
};

ofdm_symbol_ctx_t *ofdm_symbol_new(void) {
    ofdm_symbol_ctx_t *ctx = (ofdm_symbol_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->freq = fftwf_alloc_complex(DAB_MODE_I_IFFT);
    ctx->time = fftwf_alloc_complex(DAB_MODE_I_IFFT);
    if (!ctx->freq || !ctx->time) {
        if (ctx->freq) fftwf_free(ctx->freq);
        if (ctx->time) fftwf_free(ctx->time);
        free(ctx);
        return NULL;
    }

    /* FFTW_BACKWARD = inverse DFT (no normalisation — we apply 1/sqrt(N)
     * explicitly so both forward and inverse transforms keep unit energy). */
    ctx->plan = fftwf_plan_dft_1d(DAB_MODE_I_IFFT, ctx->freq, ctx->time,
                                  FFTW_BACKWARD, FFTW_MEASURE);
    if (!ctx->plan) {
        fftwf_free(ctx->freq); fftwf_free(ctx->time); free(ctx); return NULL;
    }
    ctx->scale = 1.0f / sqrtf((float)DAB_MODE_I_IFFT);
    return ctx;
}

void ofdm_symbol_free(ofdm_symbol_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->plan) fftwf_destroy_plan(ctx->plan);
    if (ctx->freq) fftwf_free(ctx->freq);
    if (ctx->time) fftwf_free(ctx->time);
    free(ctx);
}

void ofdm_symbol_build(ofdm_symbol_ctx_t *ctx,
                       const float _Complex *carriers,
                       float _Complex *out_iq) {
    /* fftwf_complex resolves to `float _Complex` here because <complex.h>
     * was included before <fftw3.h>. We treat the arrays as native complex. */
    float _Complex *freq = (float _Complex *)ctx->freq;
    float _Complex *time = (float _Complex *)ctx->time;

    memset(freq, 0, sizeof(*freq) * DAB_MODE_I_IFFT);
    const int N = DAB_MODE_I_IFFT;
    for (int i = 0; i < 768; ++i) freq[1 + i]           = carriers[i];
    for (int i = 0; i < 768; ++i) freq[N - 768 + i]     = carriers[768 + i];

    fftwf_execute(ctx->plan);

    const int CP = DAB_MODE_I_CP;
    for (int i = 0; i < CP; ++i) out_iq[i]       = time[N - CP + i] * ctx->scale;
    for (int i = 0; i < N;  ++i) out_iq[CP + i]  = time[i]          * ctx->scale;
}
