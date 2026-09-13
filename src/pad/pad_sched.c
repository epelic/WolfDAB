#include "pad_sched.h"
#include "dls.h"
#include "epg.h"
#include "mot.h"
#include "spi.h"
#include "common/log.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * PAD scheduler — interleaves DLS and MOT X-PAD fields.
 *
 * Strategy:
 *   - While MOT is transmitting: 8 MOT fields, then 1 DLS field, repeat.
 *   - After MOT completes: DLS-only for MOT_RETX_INTERVAL calls, then restart.
 *   - When EPG is active: alternate station DLS and EPG DLS every
 *     EPG_ROTATE_INTERVAL calls.
 */

#define MOT_BURST_LEN       8     /* MOT fields per burst before one DLS */
#define MOT_RETX_INTERVAL   50    /* superframes between MOT retransmits (~6 s) */
#define EPG_ROTATE_INTERVAL 40    /* ~4.8 s per DLS text before switching */
#define EPG_CHECK_INTERVAL  10    /* re-check time every N rotations */

struct pad_sched {
    dls_enc_t *dls;
    mot_enc_t *mot;         /* SlideShow MOT (transport_id=1) */
    epg_t     *epg;
    spi_enc_t *spi;         /* SPI MOT (transport_id=2) */

    int  burst_count;      /* counts MOT fields in current burst */
    int  idle_count;       /* counts calls since last MOT completion */
    int  mot_active;       /* nonzero while MOT is transmitting */
    int  mot_auto_retx;    /* legacy single-slide automatic retransmission */
    int  spi_active;       /* nonzero while SPI MOT is transmitting */
    int  spi_next;         /* 1 = next MOT cycle should be SPI */

    /* EPG text rotation state. */
    char        station_dls[129];
    int         epg_dls_count;  /* calls since last DLS text switch */
    int         epg_showing;    /* 1 = currently showing EPG DLS */
    int         epg_check_ctr;  /* counts rotations to re-check time */
    char        epg_last[EPG_DLS_MAX]; /* last EPG text, detect changes */
};

pad_sched_t *pad_sched_new(const char *dls_text, int charset,
                           const char *slide_path) {
    pad_sched_t *s = (pad_sched_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;

    if (dls_text && dls_text[0])
        s->dls = dls_enc_new(dls_text, charset);

    if (slide_path && slide_path[0])
        s->mot = mot_enc_new(slide_path, "logo.jpg", 1);

    s->mot_active = (s->mot != NULL) ? 1 : 0;
    s->mot_auto_retx = 1;
    s->burst_count = 0;
    s->idle_count = 0;
    if (dls_text) {
        strncpy(s->station_dls, dls_text, sizeof(s->station_dls)-1);
        s->station_dls[sizeof(s->station_dls)-1] = '\0';
    }
    return s;
}

void pad_sched_set_epg(pad_sched_t *s, epg_t *e) {
    if (s) s->epg = e;
}

void pad_sched_set_spi(pad_sched_t *s, spi_enc_t *spi) {
    if (s) s->spi = spi;
}

void pad_sched_set_dls(pad_sched_t *s, const char *text) {
    if (!s || !text || !text[0] || strcmp(s->station_dls, text) == 0) return;
    strncpy(s->station_dls, text, sizeof(s->station_dls)-1);
    s->station_dls[sizeof(s->station_dls)-1] = '\0';
    if (!s->dls) s->dls = dls_enc_new(s->station_dls, 0);
    else dls_enc_set_text(s->dls, s->station_dls);
    s->epg_showing = 0;
}

int pad_sched_set_slide(pad_sched_t *s, const char *image_path,
                        const char *content_name, uint16_t transport_id) {
    if (!s || !image_path || !image_path[0]) return -1;
    mot_enc_t *next = mot_enc_new(image_path,
                                  content_name && content_name[0] ? content_name : "slide.jpg",
                                  transport_id ? transport_id : 1);
    if (!next) return -1;
    if (s->mot) mot_enc_free(s->mot);
    s->mot = next;
    s->mot_active = 1;
    s->mot_auto_retx = 1;
    s->spi_active = 0;
    s->burst_count = 0;
    s->idle_count = 0;
    return 0;
}

int pad_sched_slide_complete(const pad_sched_t *s) {
    return !s || !s->mot || mot_enc_complete(s->mot);
}

void pad_sched_free(pad_sched_t *s) {
    if (!s) return;
    if (s->dls) dls_enc_free(s->dls);
    if (s->mot) mot_enc_free(s->mot);
    /* epg is NOT owned by pad_sched — caller frees it. */
    free(s);
}

/* Rotate between station DLS and EPG DLS based on time. */
static void epg_rotate(pad_sched_t *s) {
    if (!s->epg || !s->dls) return;

    s->epg_dls_count++;
    if (s->epg_dls_count < EPG_ROTATE_INTERVAL) return;
    s->epg_dls_count = 0;

    if (s->epg_showing) {
        /* Switch back to station DLS. */
        if (s->station_dls && s->station_dls[0])
            dls_enc_set_text(s->dls, s->station_dls);
        s->epg_showing = 0;
    } else {
        /* Switch to EPG DLS. Re-check time periodically. */
        const char *epg_text = epg_get_dls(s->epg, time(NULL));
        if (epg_text && epg_text[0]) {
            /* Only update if text actually changed. */
            if (strcmp(s->epg_last, epg_text) != 0) {
                strncpy(s->epg_last, epg_text, EPG_DLS_MAX - 1);
                s->epg_last[EPG_DLS_MAX - 1] = '\0';
            }
            dls_enc_set_text(s->dls, s->epg_last);
            s->epg_showing = 1;
        }
        /* If no EPG text (outside schedule), stay on station DLS. */
    }
}

int pad_sched_get_xpad(pad_sched_t *s,
                       uint8_t *xpad_out, size_t xpad_cap,
                       uint8_t *fpad0, uint8_t *fpad1) {
    if (!s) return 0;

    /* EPG text rotation. */
    epg_rotate(s);

    /* Handle MOT/SPI retransmission timer. */
    if (!s->mot_active && !s->spi_active) {
        s->idle_count++;
        if (s->idle_count >= MOT_RETX_INTERVAL) {
            /* Alternate between SlideShow and SPI on each retransmission cycle. */
            if (s->spi_next && s->spi) {
                mot_enc_t *spi_mot = spi_enc_get_mot(s->spi);
                if (spi_mot) {
                    mot_enc_restart(spi_mot);
                    s->spi_active = 1;
                    s->spi_next = 0;
                }
            } else if (s->mot && s->mot_auto_retx) {
                mot_enc_restart(s->mot);
                s->mot_active = 1;
                s->spi_next = (s->spi != NULL) ? 1 : 0;
            } else if (s->spi) {
                /* No SlideShow, only SPI. */
                mot_enc_t *spi_mot = spi_enc_get_mot(s->spi);
                if (spi_mot) {
                    mot_enc_restart(spi_mot);
                    s->spi_active = 1;
                }
            }
            s->burst_count = 0;
            s->idle_count = 0;
        }
    }

    /* Get the currently active MOT encoder (SlideShow or SPI). */
    mot_enc_t *active_mot = NULL;
    if (s->mot_active && s->mot)
        active_mot = s->mot;
    else if (s->spi_active && s->spi)
        active_mot = spi_enc_get_mot(s->spi);

    /* Decide: MOT or DLS? */
    if (active_mot) {
        if (s->burst_count < MOT_BURST_LEN) {
            int r = mot_enc_get_xpad(active_mot, xpad_out, xpad_cap, fpad0, fpad1);
            if (r > 0) {
                s->burst_count++;
                static int mot_dbg = 0;
                if (mot_dbg < 3) { LOGI("pad_sched: MOT xpad r=%d burst=%d", r, s->burst_count); mot_dbg++; }
                return r;
            }
            /* MOT finished mid-burst or returned 0. */
            LOGI("pad_sched: MOT/SPI returned 0, deactivating");
            s->mot_active = 0;
            s->spi_active = 0;
            s->idle_count = 0;
            if (s->mot && s->mot_auto_retx) {
                mot_enc_restart(s->mot);
                s->mot_active = 1;
                r = mot_enc_get_xpad(s->mot, xpad_out, xpad_cap, fpad0, fpad1);
                if (r > 0) { s->burst_count = 1; return r; }
            }
        } else {
            /* Insert one DLS field, then reset burst counter. */
            s->burst_count = 0;
            if (s->dls) {
                int r = dls_enc_get_xpad(s->dls, xpad_out, xpad_cap, fpad0, fpad1);
                if (r > 0) return r;
            }
            /* DLS empty — fall through to MOT. */
            int r = mot_enc_get_xpad(active_mot, xpad_out, xpad_cap, fpad0, fpad1);
            if (r > 0) { s->burst_count++; return r; }
            s->mot_active = 0;
            s->spi_active = 0;
            s->idle_count = 0;
            if (s->mot && s->mot_auto_retx) {
                mot_enc_restart(s->mot);
                s->mot_active = 1;
                r = mot_enc_get_xpad(s->mot, xpad_out, xpad_cap, fpad0, fpad1);
                if (r > 0) { s->burst_count = 1; return r; }
            }
        }
    }

    /* DLS-only mode. */
    if (s->dls) {
        return dls_enc_get_xpad(s->dls, xpad_out, xpad_cap, fpad0, fpad1);
    }

    return 0;
}
