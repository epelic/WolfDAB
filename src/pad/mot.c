#include "mot.h"
#include "common/crc16.h"
#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/*
 * MOT SlideShow encoder — builds MSC Data Groups from an image file
 * and emits X-PAD subfields for DAB+ ancillary data injection.
 *
 * References:
 *   ETSI EN 301 234 — MOT (header structure, content types)
 *   ETSI EN 300 401 §5.3.3.1 — MSC Data Group format
 *   ETSI TS 101 499 — MOT SlideShow application
 *   qt-dab pad-handler.cpp + mot-object.cpp — receiver reference
 */

#define MOT_BODY_SEG_SIZE  128   /* bytes per body segment */
#define MOT_MAX_IMAGE_SIZE 32768 /* 32 KB max */
#define MOT_MAX_DG_SIZE    1024  /* max MSC data group size */
#define MOT_MAX_DGS        300   /* max data groups (header + body segs) */

/* Normalise EXIF-only JPEG cover art to JFIF in memory for MOT receivers. */
static int ensure_jpeg_jfif(uint8_t **data, size_t *len) {
    static const uint8_t app0[] = {0xFF,0xE0,0x00,0x10,'J','F','I','F',0x00,
        0x01,0x01,0x00,0x00,0x01,0x00,0x01,0x00,0x00};
    if(!data||!*data||!len||*len<2||(*data)[0]!=0xFF||(*data)[1]!=0xD8)return -1;
    size_t scan=*len<4096?*len:4096;
    for(size_t i=2;i+9<=scan;++i)if((*data)[i]==0xFF&&(*data)[i+1]==0xE0&&memcmp(*data+i+4,"JFIF\0",5)==0)return 0;
    if(*len+sizeof(app0)>MOT_MAX_IMAGE_SIZE)return -1;
    uint8_t *next=(uint8_t*)malloc(*len+sizeof(app0));if(!next)return -1;
    memcpy(next,*data,2);memcpy(next+2,app0,sizeof(app0));memcpy(next+2+sizeof(app0),*data+2,*len-2);
    free(*data);*data=next;*len+=sizeof(app0);LOGI("mot: EXIF JPEG normalised to JFIF");return 1;
}

/* CI Length-Index → subfield size table (same as dls.c). */
static const int ci_sizes[] = { 4, 6, 8, 12, 16, 24, 32, 48 };

static int ci_len_index(int need) {
    for (int i = 0; i < 8; ++i)
        if (ci_sizes[i] >= need) return i;
    return 7;
}

/* Content type detection from file extension. */
static int detect_content_type(const char *path, int *ctype, int *csubtype) {
    const char *dot = strrchr(path, '.');
    if (!dot) return -1;
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) {
        *ctype = 0x02; *csubtype = 0x01; return 0; /* Image/JFIF */
    }
    if (strcasecmp(dot, ".png") == 0) {
        *ctype = 0x02; *csubtype = 0x03; return 0; /* Image/PNG */
    }
    return -1;
}

/*
 * Build MOT header per ETSI EN 301 234 §6.1.
 * Returns header byte count.
 */
static int build_mot_header(uint8_t *out, size_t cap,
                            int body_size,
                            int content_type, int content_subtype,
                            const char *content_name) {
    int name_len = content_name ? (int)strlen(content_name) : 0;
    /* Header core: 7 bytes, TriggerTime=NOW (PLI 2, four zero bytes),
     * then the variable-size ContentName parameter. */
    int param_data_len = 1 + name_len; /* charset byte + name */
    int param_total = 2 + param_data_len; /* header extension entry */
    int header_size = 7 + 5 + param_total;

    if ((size_t)header_size > cap) return -1;

    /* Bytes 0-3: BodySize (28 bits) + HeaderSize (13 bits, MSB portion) */
    out[0] = (uint8_t)((body_size >> 20) & 0xFF);
    out[1] = (uint8_t)((body_size >> 12) & 0xFF);
    out[2] = (uint8_t)((body_size >>  4) & 0xFF);
    out[3] = (uint8_t)(((body_size & 0x0F) << 4) | ((header_size >> 9) & 0x0F));

    /* Bytes 4-6: HeaderSize(9 LSBs) + ContentType(6) + ContentSubType(9) */
    out[4] = (uint8_t)((header_size >> 1) & 0xFF);
    out[5] = (uint8_t)(((header_size & 0x01) << 7) |
                        ((content_type & 0x3F) << 1) |
                        ((content_subtype >> 8) & 0x01));
    out[6] = (uint8_t)(content_subtype & 0xFF);

    /* Header extension: ContentName (ParamId=12, PLI=3 → DataFieldLength byte) */
    int p = 7;
    out[p++] = (uint8_t)((2 << 6) | 5); /* TriggerTime, PLI=2 (4 bytes) */
    out[p++] = 0x00; out[p++] = 0x00; out[p++] = 0x00; out[p++] = 0x00;
    out[p++] = (uint8_t)((3 << 6) | (12 & 0x3F));  /* PLI=3, ParamId=12 */
    out[p++] = (uint8_t)param_data_len;              /* DataFieldLength */
    out[p++] = 0x00; /* charset = EBU Latin (complete) */
    if (name_len > 0)
        memcpy(&out[p], content_name, (size_t)name_len);
    p += name_len;

    return p;
}

/*
 * Build one MSC Data Group per EN 300 401 §5.3.3.1.
 * Returns total DG byte count including CRC.
 */
static int build_msc_data_group(uint8_t *out, size_t cap,
                                int group_type,      /* 3=header, 4=body */
                                uint16_t transport_id,
                                int segment_number,
                                int last_flag,
                                const uint8_t *seg_data,
                                int seg_size,
                                int continuity_index) {
    int need = 9 + seg_size + 2; /* DG hdr(2)+seg(2)+ua(3)+segsize(2)+data+crc(2) */
    if ((size_t)need > cap) return -1;

    int p = 0;
    /* Byte 0: ExtFlag=0 | CRCFlag=1 | SegFlag=1 | UserAccessFlag=1 | GroupType(4). */
    out[p++] = (uint8_t)(0x40 | 0x20 | 0x10 | (group_type & 0x0F));
    /* Byte 1: ContinuityIndex(4) | RepetitionIndex(4) */
    out[p++] = (uint8_t)(((continuity_index & 0x0F) << 4) | 0x00);
    /* Segment field: LastFlag(1) | SegmentNumber(15) */
    out[p++] = (uint8_t)((last_flag ? 0x80 : 0x00) | ((segment_number >> 8) & 0x7F));
    out[p++] = (uint8_t)(segment_number & 0xFF);
    /* User access field: TransportIdFlag=1 | LengthIndicator=2
     * LI = length of End User Address = TransportId only = 2 bytes.
     * qt-dab skips (LI-2) bytes after TransportId — LI=3 would eat
     * the first byte of the segmentation header. */
    out[p++] = 0x10 | 0x02;
    /* TransportId (2 bytes) */
    out[p++] = (uint8_t)(transport_id >> 8);
    out[p++] = (uint8_t)(transport_id & 0xFF);
    /* Segment size (13 bits in 2 bytes) */
    out[p++] = (uint8_t)((seg_size >> 8) & 0x1F);
    out[p++] = (uint8_t)(seg_size & 0xFF);
    /* Segment data */
    memcpy(&out[p], seg_data, (size_t)seg_size);
    p += seg_size;
    /* CRC-16 over bytes 0..p-1, inverted, appended */
    uint16_t crc = crc16_ccitt(out, (size_t)p) ^ 0xFFFF;
    out[p++] = (uint8_t)(crc >> 8);
    out[p++] = (uint8_t)(crc & 0xFF);
    return p;
}

/* --- Encoder state ---------------------------------------------------- */

typedef enum {
    MOT_STATE_LENGTH_IND,   /* emit appType 1 (DG length indicator) */
    MOT_STATE_DG_START,     /* emit appType 12 (first chunk) */
    MOT_STATE_DG_CONTINUE,  /* emit appType 13 (continuation) */
    MOT_STATE_DONE,         /* all DGs sent */
} mot_state_t;

struct mot_enc {
    /* Pre-built MSC data groups. */
    uint8_t *dg_buf[MOT_MAX_DGS];
    int      dg_len[MOT_MAX_DGS];
    int      n_dgs;

    /* Transmission state. */
    mot_state_t state;
    int         cur_dg;       /* index into dg_buf[] */
    int         cur_offset;   /* byte offset within current DG */
    int         cont_index;   /* continuity counter (mod 16) */
    int         done;         /* nonzero if full cycle completed */
};

/* Internal: build a MOT encoder from raw data + content type. */
static mot_enc_t *mot_enc_build(const uint8_t *data, size_t data_len,
                                const char *content_name,
                                int ctype, int csubtype,
                                uint16_t transport_id) {
    mot_enc_t *e = (mot_enc_t *)calloc(1, sizeof(*e));
    if (!e) return NULL;

    /* Build MOT header. */
    uint8_t mot_hdr[256];
    int mot_hdr_len = build_mot_header(mot_hdr, sizeof(mot_hdr),
                                       (int)data_len, ctype, csubtype,
                                       content_name);
    if (mot_hdr_len < 0) {
        LOGE("mot: header build failed");
        free(e); return NULL;
    }

    /* DG 0: MOT header (groupType=3, segment 0, last=1). */
    uint8_t dg_tmp[MOT_MAX_DG_SIZE];
    int dg_len = build_msc_data_group(dg_tmp, sizeof(dg_tmp),
                                       3, transport_id, 0, 1,
                                       mot_hdr, mot_hdr_len, 0);
    if (dg_len < 0) { free(e); return NULL; }
    e->dg_buf[0] = (uint8_t *)malloc((size_t)dg_len);
    memcpy(e->dg_buf[0], dg_tmp, (size_t)dg_len);
    e->dg_len[0] = dg_len;
    e->n_dgs = 1;

    /* DGs 1..N: body segments (groupType=4). */
    int n_body_segs = ((int)data_len + MOT_BODY_SEG_SIZE - 1) / MOT_BODY_SEG_SIZE;
    int ci = 1;
    for (int s = 0; s < n_body_segs; ++s) {
        int off = s * MOT_BODY_SEG_SIZE;
        int sz  = (int)data_len - off;
        if (sz > MOT_BODY_SEG_SIZE) sz = MOT_BODY_SEG_SIZE;
        int last = (s == n_body_segs - 1) ? 1 : 0;

        dg_len = build_msc_data_group(dg_tmp, sizeof(dg_tmp),
                                       4, transport_id, s, last,
                                       data + off, sz, ci);
        ci = (ci + 1) & 0x0F;
        if (dg_len < 0 || e->n_dgs >= MOT_MAX_DGS) {
            LOGE("mot: too many segments or DG build failed");
            break;
        }
        e->dg_buf[e->n_dgs] = (uint8_t *)malloc((size_t)dg_len);
        memcpy(e->dg_buf[e->n_dgs], dg_tmp, (size_t)dg_len);
        e->dg_len[e->n_dgs] = dg_len;
        e->n_dgs++;
    }

    e->state = MOT_STATE_LENGTH_IND;
    e->cur_dg = 0;
    e->cur_offset = 0;
    e->cont_index = 0;
    e->done = 0;
    return e;
}

mot_enc_t *mot_enc_new(const char *image_path, const char *content_name,
                       uint16_t transport_id) {
    if (!image_path || transport_id == 0) return NULL;

    int ctype, csubtype;
    if (detect_content_type(image_path, &ctype, &csubtype) != 0) {
        LOGE("mot: unsupported file type: %s (need .jpg/.jpeg/.png)", image_path);
        return NULL;
    }

    FILE *f = fopen(image_path, "rb");
    if (!f) { LOGE("mot: cannot open %s", image_path); return NULL; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0 || fsize > MOT_MAX_IMAGE_SIZE) {
        LOGE("mot: file too large or empty (%ld bytes, max %d)", fsize, MOT_MAX_IMAGE_SIZE);
        fclose(f); return NULL;
    }
    size_t img_size=(size_t)fsize;
    uint8_t *img = (uint8_t *)malloc(img_size);
    if (!img) { fclose(f); return NULL; }
    if (fread(img, 1, img_size, f) != img_size) {
        LOGE("mot: read error"); free(img); fclose(f); return NULL;
    }
    fclose(f);

    if(ctype==0x02&&csubtype==0x01&&ensure_jpeg_jfif(&img,&img_size)<0){LOGE("mot: invalid JPEG/JFIF image");free(img);return NULL;}

    mot_enc_t *e = mot_enc_build(img, img_size, content_name,
                                 ctype, csubtype, transport_id);
    if (e)
        LOGI("mot: %s loaded, %zu bytes, %d DGs",image_path,img_size,e->n_dgs);
    free(img);
    return e;
}

mot_enc_t *mot_enc_new_raw(const uint8_t *data, size_t data_len,
                           const char *content_name,
                           int content_type, int content_subtype,
                           uint16_t transport_id) {
    if (!data || data_len == 0 || transport_id == 0) return NULL;
    if (data_len > MOT_MAX_IMAGE_SIZE) {
        LOGE("mot_raw: data too large (%zu, max %d)", data_len, MOT_MAX_IMAGE_SIZE);
        return NULL;
    }
    mot_enc_t *e = mot_enc_build(data, data_len, content_name,
                                 content_type, content_subtype, transport_id);
    if (e)
        LOGI("mot_raw: %zu bytes, ctype=%d/%d, %d DGs",
             data_len, content_type, content_subtype, e->n_dgs);
    return e;
}

void mot_enc_free(mot_enc_t *e) {
    if (!e) return;
    for (int i = 0; i < e->n_dgs; ++i) free(e->dg_buf[i]);
    free(e);
}

int mot_enc_complete(const mot_enc_t *e) {
    return e ? e->done : 1;
}

void mot_enc_restart(mot_enc_t *e) {
    if (!e) return;
    e->state = MOT_STATE_LENGTH_IND;
    e->cur_dg = 0;
    e->cur_offset = 0;
    e->cont_index = 0;
}

/*
 * Emit one X-PAD field.
 *
 * X-PAD layout (reversed byte order, CI at high address):
 *   [zero-pad] [data last] ... [data first] [end=0x00] [CI byte]
 *
 * F-PAD: fpad0=0x20 (Variable Size), fpad1=0x02 (CI present).
 */
int mot_enc_get_xpad(mot_enc_t *e,
                     uint8_t *xpad_out, size_t xpad_cap,
                     uint8_t *fpad0, uint8_t *fpad1) {
    if (!e || e->state == MOT_STATE_DONE || xpad_cap < MOT_XPAD_MAX)
        return 0;

    uint8_t app_type;
    const uint8_t *payload;
    int payload_len;

    /* Temporary buffer for length indicator subfield. */
    uint8_t li_buf[4];

    switch (e->state) {
    case MOT_STATE_LENGTH_IND: {
        /* AppType 1: two-byte data-group length followed by its CRC. */
        int dg_total = e->dg_len[e->cur_dg];
        li_buf[0] = (uint8_t)((dg_total >> 8) & 0x3F);
        li_buf[1] = (uint8_t)(dg_total & 0xFF);
        uint16_t li_crc = (uint16_t)(crc16_ccitt(li_buf, 2) ^ 0xFFFF);
        li_buf[2] = (uint8_t)(li_crc >> 8);
        li_buf[3] = (uint8_t)(li_crc & 0xFF);
        app_type = 1;
        payload = li_buf;
        payload_len = 4;
        e->state = MOT_STATE_DG_START;
        break;
    }
    case MOT_STATE_DG_START:
    case MOT_STATE_DG_CONTINUE: {
        /* AppType 12 (start) or 13 (continuation).
         * Send a chunk of the current DG. */
        app_type = (e->state == MOT_STATE_DG_START) ? 12 : 13;
        int remaining = e->dg_len[e->cur_dg] - e->cur_offset;
        payload = e->dg_buf[e->cur_dg] + e->cur_offset;

        /* Choose largest subfield that fits. */
        int li = ci_len_index(remaining);
        int subfield_sz = ci_sizes[li];
        payload_len = (remaining < subfield_sz) ? remaining : subfield_sz;

        e->cur_offset += payload_len;

        if (e->cur_offset >= e->dg_len[e->cur_dg]) {
            /* DG complete → advance to next DG. */
            e->cur_dg++;
            e->cur_offset = 0;
            if (e->cur_dg >= e->n_dgs) {
                e->state = MOT_STATE_DONE;
                e->done = 1;
            } else {
                e->state = MOT_STATE_LENGTH_IND;
            }
        } else {
            e->state = MOT_STATE_DG_CONTINUE;
        }
        break;
    }
    default:
        return 0;
    }

    /* Pack X-PAD: find CI length index for this payload. */
    int li = ci_len_index(payload_len);
    int subfield_sz = ci_sizes[li];
    int xpad_sz = subfield_sz + 2; /* subfield + CI end marker + CI byte */

    if ((size_t)xpad_sz > xpad_cap) return 0;

    memset(xpad_out, 0, (size_t)xpad_sz);
    xpad_out[xpad_sz - 1] = (uint8_t)((li << 5) | (app_type & 0x1F));
    xpad_out[xpad_sz - 2] = 0x00; /* CI end marker */

    /* Data bytes in reversed order (first logical byte nearest to end marker). */
    for (int j = 0; j < payload_len; ++j)
        xpad_out[xpad_sz - 3 - j] = payload[j];

    *fpad0 = 0x20; /* X-PAD indicator = 10 (Variable Size) */
    *fpad1 = 0x02; /* CI flag = 1 */

    return xpad_sz;
}
