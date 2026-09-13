#include "fib.h"
#include "common/crc16.h"

#include <string.h>

void fib_reset(fib_t *fib) {
    memset(fib->bytes, 0, sizeof(fib->bytes));
    fib->used = 0;
}

uint8_t *fib_alloc(fib_t *fib, size_t nbytes) {
    if (fib->used + nbytes > FIB_PAYLOAD_CAP) return NULL;
    uint8_t *p = &fib->bytes[fib->used];
    fib->used += nbytes;
    return p;
}

void fib_finalize(fib_t *fib) {
    /* Pad remaining payload with the 0xFF end marker. */
    for (size_t i = fib->used; i < FIB_PAYLOAD_CAP; ++i) fib->bytes[i] = 0xFF;
    uint16_t crc = crc16_fib(fib->bytes, FIB_PAYLOAD_CAP);
    fib->bytes[30] = (uint8_t)(crc >> 8);
    fib->bytes[31] = (uint8_t)(crc & 0xFF);
    fib->used = FIB_PAYLOAD_CAP;  /* sealed */
}

/*
 * FIG type-0 header byte (byte 0):   Type(3) << 5 | Length(5)
 * FIG type-0 flags byte  (byte 1):   C/N(1)<<7 | OE(1)<<6 | P/D(1)<<5 | Ext(5)
 * The Length field counts all FIG bytes AFTER byte 0, including byte 1.
 */
static inline uint8_t fig0_header_byte0(unsigned length) {
    return (uint8_t)((0u << 5) | (length & 0x1F));
}
static inline uint8_t fig0_header_byte1(unsigned ext /* 5 bits */) {
    return (uint8_t)(ext & 0x1F);   /* C/N=0, OE=0, P/D=0 */
}

int fig0_0_write(fib_t *fib, const fig0_0_t *f) {
    /*
     * FIG type 0 header byte 0: Type(3) | Length(5)
     * FIG 0 body  byte 1:       C/N(1) | OE(1) | P/D(1) | Ext(5)
     * FIG 0/0 body (extension 0) length = 5 bytes total → Length field = 5:
     *   byte 1: C/N=0, OE=0, P/D=0, Ext=0           → 0x00
     *   byte 2..3: EId (16 bit)
     *   byte 4: Change(2) | Al(1) | CIFcount_high(5)
     *   byte 5: CIFcount_low (8)
     * So the FIG header byte says length=5, followed by 5 body bytes → 6 total.
     */
    uint8_t *p = fib_alloc(fib, 6);
    if (!p) return -1;
    p[0] = (uint8_t)((0 << 5) | (5 & 0x1F));   /* type=0, length=5 */
    p[1] = 0x00;                                /* C/N=0 OE=0 P/D=0 Ext=0 */
    p[2] = (uint8_t)(f->ensemble_id >> 8);
    p[3] = (uint8_t)(f->ensemble_id & 0xFF);
    p[4] = (uint8_t)(((f->change_flag & 0x03) << 6) |
                     ((f->al_flag     & 0x01) << 5) |
                     ((f->cif_count_high & 0x1F)));
    p[5] = f->cif_count_low;
    return 0;
}

int fig0_9_write(fib_t *fib, uint8_t ecc) {
    /* FIG 0/9: no international table, UTC local-time offset. */
    uint8_t *p = fib_alloc(fib, 4);
    if (!p) return -1;
    p[0] = fig0_header_byte0(3);
    p[1] = fig0_header_byte1(9);
    p[2] = 0x00;
    p[3] = ecc;
    return 0;
}

int fig0_10_write(fib_t *fib, const fig0_10_t *f) {
    if (!f || f->mjd > 99999u || f->hour > 23u || f->minute > 59u) return -1;
    uint8_t *p=fib_alloc(fib,6);if(!p)return -1;
    /* EN 300 401 FIG 0/10 short UTC form: Rfu(1), MJD(17), LSI(1),
     * ConfInd(1), UTC flag=0(1), hours(5), minutes(6). */
    uint32_t v=((f->mjd&0x1FFFFu)<<14)|((uint32_t)f->hour<<6)|f->minute;
    p[0]=fig0_header_byte0(5);p[1]=fig0_header_byte1(10);
    p[2]=(uint8_t)(v>>24);p[3]=(uint8_t)(v>>16);p[4]=(uint8_t)(v>>8);p[5]=(uint8_t)v;
    return 0;
}

int fig0_1_eep_write(fib_t *fib, const fig0_1_eep_t *f) {
    /* 6 bytes total: 2 FIG0 header + 4 subchannel long-form body.
     *   byte 0 FIG header:  type=0, length=5 (1 flags byte + 4 body)
     *   byte 1 FIG0 flags:  ext=1
     *   byte 2: SubChId(6) | StartAddr_hi(2)
     *   byte 3: StartAddr_lo(8)
     *   byte 4: S/L=1 | Option(3) | ProtLevel(2) | SubChSize_hi(2)
     *   byte 5: SubChSize_lo(8)
     */
    uint8_t *p = fib_alloc(fib, 6);
    if (!p) return -1;
    if (f->start_addr > 0x3FF || f->sub_ch_size_cus > 0x3FF) return -1;
    p[0] = fig0_header_byte0(5);
    p[1] = fig0_header_byte1(1);
    p[2] = (uint8_t)(((f->sub_ch_id & 0x3F) << 2) |
                     ((f->start_addr >> 8) & 0x03));
    p[3] = (uint8_t)(f->start_addr & 0xFF);
    p[4] = (uint8_t)((1u << 7) |                         /* long form = 1 */
                     ((f->eep_option & 0x07) << 4) |
                     ((f->prot_level & 0x03) << 2) |
                     ((f->sub_ch_size_cus >> 8) & 0x03));
    p[5] = (uint8_t)(f->sub_ch_size_cus & 0xFF);
    return 0;
}

int fig0_2_audio_write(fib_t *fib, const fig0_2_audio_t *f) {
    /* 7 bytes: 2 FIG0 header + 3 service descriptor + 2 component descriptor.
     *
     * The audio service-component descriptor is a 16-bit word, MSB-first per
     * ETSI EN 300 401 §6.3.1:
     *   bits 15..14  TMId (2)
     *   bits 13..8   ASCTy (6)
     *   bits  7..2   SubChId (6)
     *   bit   1      P/S flag
     *   bit   0      CA flag
     * → byte A = TMId<<6 | ASCTy
     * → byte B = SubChId<<2 | PS<<1 | CA
     */
    uint8_t *p = fib_alloc(fib, 7);
    if (!p) return -1;
    p[0] = fig0_header_byte0(6);       /* length = 1 flags + 3 service + 2 comp = 6 */
    p[1] = fig0_header_byte1(2);       /* ext = 2 */

    p[2] = (uint8_t)(f->service_id >> 8);
    p[3] = (uint8_t)(f->service_id & 0xFF);
    p[4] = (uint8_t)(((f->local_flag & 0x01) << 7) |
                     ((f->ca_id      & 0x07) << 4) |
                     (1u /* NbServiceComp = 1 */ & 0x0F));

    /* Component: TMid=0 (MSC stream audio), PS=1 (primary), CA=0. */
    p[5] = (uint8_t)(((0u /* TMid */ & 0x03) << 6) | (f->asc_type & 0x3F));
    p[6] = (uint8_t)(((f->sub_ch_id & 0x3F) << 2) |
                     ((1u /* PS primary */ & 0x01) << 1) |
                     (0u /* CA_flag      */ & 0x01));
    return 0;
}

int fig0_8_audio_write(fib_t *fib, const fig0_8_audio_t *f) {
    /* 6 bytes: 2 FIG0 header + 2 SId + 2 short-form body.
     *   short-form body byte 0: ext(1)=0 | rfa(3)=0 | SCIdS(4)
     *   short-form body byte 1: LS(1)=0 | MscFic(1)=0 | Id(6) = SubChId
     */
    uint8_t *p = fib_alloc(fib, 6);
    if (!p) return -1;
    p[0] = fig0_header_byte0(5);       /* length = flags 1 + SId 2 + body 2 = 5 */
    p[1] = fig0_header_byte1(8);       /* ext = 8 */
    p[2] = (uint8_t)(f->service_id >> 8);
    p[3] = (uint8_t)(f->service_id & 0xFF);
    p[4] = (uint8_t)(f->sc_ids & 0x0F);                  /* ext=0 rfa=0 SCIdS */
    p[5] = (uint8_t)(f->sub_ch_id & 0x3F);               /* LS=0 MscFic=0 Id  */
    return 0;
}

int fig0_17_write(fib_t *fib, const fig0_17_t *f) {
    /* 6 bytes: 2 FIG0 header + 4 body (SId 2 + flags/PTY 2).
     * Per EN 300 401 §8.1.5 and ODR-DabMux FIG0_17.cpp:
     *   body[0..1]: SId (big-endian)
     *   body[2]: SD(1) | rfa(1) | rfu(2) | rfa2_high(4)  — all zero for static
     *   body[3]: rfa2_low(2) | rfu(1) | IntCode(5)
     */
    uint8_t *p = fib_alloc(fib, 6);
    if (!p) return -1;
    p[0] = fig0_header_byte0(5);       /* length = 5 (SId 2 + flags/PTY 2 + ext 1) */
    p[1] = fig0_header_byte1(17);      /* ext = 17 */
    p[2] = (uint8_t)(f->service_id >> 8);
    p[3] = (uint8_t)(f->service_id & 0xFF);
    p[4] = 0x00;                       /* SD=0 rfa=0 rfu=00 rfa2_high=0000 */
    p[5] = (uint8_t)(f->pty & 0x1F);   /* rfa2_low=00 rfu=0 IntCode(5) */
    return 0;
}

int fig0_13_write(fib_t *fib, const fig0_13_app_t *f) {
    /* FIG 0/13: User Application Information.
     * Total: 2 (FIG0 header) + 2 (SId) + 1 (SCIdS/No) + 2 (UAType/Len)
     *        + ua_data_len = 7 + ua_data_len bytes.
     * Length field = total - 1 (excluding byte 0).
     */
    int total = 7 + f->ua_data_len;
    uint8_t *p = fib_alloc(fib, (size_t)total);
    if (!p) return -1;

    p[0] = fig0_header_byte0((unsigned)(total - 1));  /* length = N-1 */
    p[1] = fig0_header_byte1(13);                     /* ext = 13 */
    p[2] = (uint8_t)(f->service_id >> 8);
    p[3] = (uint8_t)(f->service_id & 0xFF);
    p[4] = (uint8_t)(((f->sc_ids & 0x0F) << 4) | 1u);  /* SCIdS | No=1 */
    /* UAType is 11 bits: high 8 in byte 5, low 3 in byte 6 bits 7..5. */
    p[5] = (uint8_t)((f->ua_type >> 3) & 0xFF);
    p[6] = (uint8_t)(((f->ua_type & 0x07) << 5) | (f->ua_data_len & 0x1F));
    if (f->ua_data_len > 0)
        memcpy(&p[7], f->ua_data, f->ua_data_len);
    return 0;
}

/*
 * FIG type-1 header byte 0: Type(3)=1<<5 | Length(5)
 * FIG type-1 header byte 1: Charset(4)<<4 | OE(1)<<3 | Extension(3)
 * Label body: 2 bytes ID + 16 bytes ASCII label + 2 bytes character flag.
 * Total FIG size = 1 (header) + 21 (body) = 22 bytes.
 */
static int fig1_label_write(fib_t *fib, unsigned extension,
                            uint16_t id16, const char *label, uint16_t char_flag) {
    uint8_t *p = fib_alloc(fib, 22);
    if (!p) return -1;
    p[0] = (uint8_t)((1u << 5) | (21 & 0x1F));           /* type=1, length=21 */
    p[1] = (uint8_t)((0u << 4) | (0u << 3) | (extension & 0x07)); /* charset=0 OE=0 */
    p[2] = (uint8_t)(id16 >> 8);
    p[3] = (uint8_t)(id16 & 0xFF);
    for (int i = 0; i < 16; ++i) {
        char c = label[i] ? label[i] : ' ';
        p[4 + i] = (uint8_t)c;
        if (label[i] == 0) {
            for (int j = i + 1; j < 16; ++j) p[4 + j] = ' ';
            break;
        }
    }
    p[20] = (uint8_t)(char_flag >> 8);
    p[21] = (uint8_t)(char_flag & 0xFF);
    return 0;
}

int fig1_0_write(fib_t *fib, uint16_t ensemble_id,
                 const char *label, uint16_t char_flag) {
    return fig1_label_write(fib, 0, ensemble_id, label, char_flag);
}
int fig1_1_write(fib_t *fib, uint16_t service_id,
                 const char *label, uint16_t char_flag) {
    return fig1_label_write(fib, 1, service_id, label, char_flag);
}
