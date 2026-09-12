#pragma once
#include <stddef.h>
#include <stdint.h>
#include "epg.h"
#include "spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PAD scheduler — coordinates DLS text and MOT SlideShow delivery
 * over X-PAD.  Alternates between DLS segments and MOT chunks.
 *
 * When MOT data is pending: 8 MOT fields per 1 DLS field.
 * When MOT is complete: DLS only, retransmit MOT every ~60 s.
 * When EPG is active: rotates station DLS ↔ EPG DLS.
 */

#define PAD_SCHED_XPAD_MAX 50  /* must be >= MOT_XPAD_MAX */

typedef struct pad_sched pad_sched_t;

/* Create scheduler.  dls_text may be NULL (no DLS).
 * slide_path may be NULL (no MOT SlideShow). */
pad_sched_t *pad_sched_new(const char *dls_text, int charset,
                           const char *slide_path);
void         pad_sched_free(pad_sched_t *s);

/* Attach EPG for automatic DLS rotation (station ↔ EPG text).
 * epg is NOT owned by the scheduler — caller must keep it alive. */
void pad_sched_set_epg(pad_sched_t *s, epg_t *epg);

/* Attach SPI encoder for X-PAD delivery alongside SlideShow.
 * spi is NOT owned by the scheduler — caller must keep it alive. */
void pad_sched_set_spi(pad_sched_t *s, spi_enc_t *spi);
void pad_sched_set_dls(pad_sched_t *s, const char *text);
int pad_sched_set_slide(pad_sched_t *s, const char *image_path,
                         const char *content_name, uint16_t transport_id);
int pad_sched_slide_complete(const pad_sched_t *s);

/* Get next X-PAD + F-PAD.  Automatically interleaves DLS and MOT.
 * Returns X-PAD byte count, or 0 if nothing to send. */
int pad_sched_get_xpad(pad_sched_t *s,
                       uint8_t *xpad_out, size_t xpad_cap,
                       uint8_t *fpad0, uint8_t *fpad1);

#ifdef __cplusplus
}
#endif
