#pragma once
/*
 * DAB Fast Information Block (FIB) builder.
 *
 * One FIB is exactly 32 bytes: up to 30 bytes of FIG (Fast Information Group)
 * payload followed by a 2-byte CRC (CRC-16-CCITT register, complemented).
 * Unused bytes at the end of the payload area are filled with 0xFF (the
 * "end marker" convention from ETSI EN 300 401 §5.2.1.1).
 *
 * In DAB Transmission Mode I the Fast Information Channel of one 24 ms
 * logical frame carries 3 FIBs → 96 bytes total.
 *
 * This file only implements the FIB *framework* plus the FIG 0/0 writer
 * (ensemble information). The remaining FIGs we need for a minimal single-
 * service ensemble — 0/1, 0/2, 0/8, 1/0, 1/1 — are added in step 4b together
 * with the full ETI frame wrapper, where the resulting byte streams can be
 * validated against a reference decoder.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIB_SIZE        32
#define FIB_PAYLOAD_CAP 30     /* bytes available for FIG data (before CRC) */

typedef struct {
    uint8_t bytes[FIB_SIZE];
    size_t  used;              /* payload bytes written so far (< FIB_PAYLOAD_CAP) */
} fib_t;

void fib_reset(fib_t *fib);

/*
 * Reserve `nbytes` at the current position and return a pointer where the
 * caller can write the body of a FIG. The 1-byte FIG header is NOT reserved
 * here — the higher-level writers below handle that. Returns NULL if the FIB
 * would overflow.
 */
uint8_t *fib_alloc(fib_t *fib, size_t nbytes);

/*
 * Pad the remaining payload to 30 bytes with 0xFF (end marker) and append
 * the 16-bit FIB CRC. After this the fib_t is fully formed and fib->bytes
 * is a ready-to-transmit 32-byte FIB. Any further fib_alloc() is illegal.
 */
void fib_finalize(fib_t *fib);

/* ---- FIG 0/0: Ensemble information (EN 300 401 §6.4) ------------------- */

typedef struct {
    uint16_t ensemble_id;      /* EId (16 bits) */
    uint8_t  change_flag;      /* 2 bits: 00 = no change */
    uint8_t  al_flag;          /* 1 bit:  occurrence change warning */
    uint8_t  cif_count_high;   /* 5 bits: hours * 250 count, modulo 20 */
    uint8_t  cif_count_low;    /* 8 bits: seconds * 250 count, modulo 250 */
} fig0_0_t;

int fig0_0_write(fib_t *fib, const fig0_0_t *f);
int fig0_9_write(fib_t *fib, uint8_t ecc);

/* ---- FIG 0/1: Basic sub-channel organization (EEP long form) ----------- */

typedef struct {
    uint8_t  sub_ch_id;        /* 6 bits, 0..63                   */
    uint16_t start_addr;       /* 10 bits, CU address in CIF      */
    uint8_t  eep_option;       /* 3 bits: 0 = EEP_A, 1 = EEP_B    */
    uint8_t  prot_level;       /* 2 bits: 0..3 (e.g. EEP 3-A → 2) */
    uint16_t sub_ch_size_cus;  /* 10 bits, size in CUs            */
} fig0_1_eep_t;

/* Write a single-subchannel FIG 0/1 (long form). 6 bytes total. */
int fig0_1_eep_write(fib_t *fib, const fig0_1_eep_t *f);

/* ---- FIG 0/2: Service organization (audio programme, 1 component) ------ */

typedef struct {
    uint16_t service_id;       /* SId, 16 bits (programme service) */
    uint8_t  ca_id;            /* 3 bits, 0 = no conditional access */
    uint8_t  local_flag;       /* 1 bit, 0 = non-local */
    uint8_t  asc_type;         /* 6 bits, 63 = DAB+, 0 = DAB (MP2)  */
    uint8_t  sub_ch_id;        /* 6 bits  */
} fig0_2_audio_t;

/* 7 bytes total (header 2 + service 3 + component 2). */
int fig0_2_audio_write(fib_t *fib, const fig0_2_audio_t *f);

/* ---- FIG 0/8: Service component global definition (audio) -------------- */

typedef struct {
    uint16_t service_id;
    uint8_t  sc_ids;           /* 4 bits, service component id within service */
    uint8_t  sub_ch_id;        /* 6 bits, must match FIG 0/2 component        */
} fig0_8_audio_t;

/* 6 bytes total (header 2 + SId 2 + Ext/SCIdS 1 + LS/MscFic/Id 1). */
int fig0_8_audio_write(fib_t *fib, const fig0_8_audio_t *f);

/* ---- FIG 0/17: Programme Type (EN 300 401 §8.1.5) ----------------------- */

typedef struct {
    uint16_t service_id;
    uint8_t  pty;              /* 5-bit international code (0..31) */
} fig0_17_t;

/* 4 bytes total (header 1 + SId 2 + SD/PS/L/CC/Rfa/PTY 1). */
int fig0_17_write(fib_t *fib, const fig0_17_t *f);

/* ---- FIG 0/13: User Application Information (EN 300 401 §8.1.20) -------- */
/*
 * Announces a user application (e.g. EPG/SPI) on an audio service component.
 * UAType = 0x007 for EPG/SPI per ETSI TS 101 756 Table 16.
 *
 * Byte layout (programme services, short SId):
 *   FIG header: type=0, length=N
 *   byte 1: C/N=0, OE=0, P/D=0, Ext=13
 *   byte 2-3: SId (16 bits)
 *   byte 4: SCIdS(4) | No(4)  — No = number of user apps
 *   Per app: UAType(11 bits in 2 bytes) + UADataLen(5 bits)
 *     byte 5: UAType_hi(8)
 *     byte 6: UAType_lo(3) | UADataLen(5)
 *     [UADataLen bytes of app-specific data]
 */

typedef struct {
    uint16_t service_id;
    uint8_t  sc_ids;           /* 4 bits, service component id */
    uint16_t ua_type;          /* 11-bit user application type (0x007 = EPG) */
    uint8_t  ua_data[8];       /* optional app-specific data */
    uint8_t  ua_data_len;      /* 0..8 */
} fig0_13_app_t;

/* Writes FIG 0/13 with one user application. Size = 7 + ua_data_len bytes. */
int fig0_13_write(fib_t *fib, const fig0_13_app_t *f);

/* ---- FIG 1/0: Ensemble label + FIG 1/1: Service label ------------------- */
/*
 * Both labels carry a 16-byte ASCII string (space-padded) plus a 16-bit
 * character flag field (a mask saying which of the 16 characters should be
 * shown in a short 8-character display). We encode with charset = 0 (EBU
 * Latin / default 8-bit alphabet).
 */

int fig1_0_write(fib_t *fib, uint16_t ensemble_id,
                 const char *label, uint16_t char_flag);
int fig1_1_write(fib_t *fib, uint16_t service_id,
                 const char *label, uint16_t char_flag);

#ifdef __cplusplus
}
#endif
