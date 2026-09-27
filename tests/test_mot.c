/*
 * test_mot.c — verify MOT Data Group structure matches ETSI EN 300 401
 *              and qt-dab's pad-handler.cpp expectations.
 *
 * Key checks:
 *   1. User Access LengthIndicator = 2 (TransportId only)
 *   2. DG byte layout: header, segment, UA, segheader, data, CRC
 *   3. X-PAD CI encoding for appType 1/12/13
 *   4. CRC-16 validation
 */
#include "pad/mot.h"
#include "pad/pad_sched.h"
#include "common/crc16.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

/* Create a tiny JPEG file on disk for mot_enc_new. */
static const char *create_test_image(void) {
    static const char *path = "test_mot_image.jpg";
    /* Minimal JFIF: SOI + APP0 + enough data to have a few body segments. */
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    /* SOI marker */
    fputc(0xFF, f); fputc(0xD8, f);
    /* APP0 JFIF header (minimal) */
    fputc(0xFF, f); fputc(0xE0, f);
    fputc(0x00, f); fputc(0x10, f); /* length=16 */
    fputs("JFIF", f); fputc(0x00, f);
    fputc(0x01, f); fputc(0x01, f); /* version 1.1 */
    fputc(0x00, f); /* density units */
    fputc(0x00, f); fputc(0x01, f); /* x density */
    fputc(0x00, f); fputc(0x01, f); /* y density */
    fputc(0x00, f); fputc(0x00, f); /* thumbnail */
    /* Pad to ~500 bytes to get a few body segments. */
    for (int i = 0; i < 480; ++i) fputc((uint8_t)(i & 0xFF), f);
    /* EOI marker */
    fputc(0xFF, f); fputc(0xD9, f);
    fclose(f);
    return path;
}

/*
 * Parse an MSC Data Group the way qt-dab's build_MSC_segment does.
 * Returns 0 on success, fills out parameters.
 */
static int parse_dg_like_qtdab(const uint8_t *dg, int dg_len,
                               int *group_type, int *segment_number,
                               int *last_flag, uint16_t *transport_id,
                               int *seg_size_out, int *data_offset) {
    if (dg_len < 2) return -1;

    *group_type = dg[0] & 0x0F;
    int ext_flag = (dg[0] & 0x80) != 0;
    int seg_flag = (dg[0] & 0x20) != 0;
    int ua_flag  = (dg[0] & 0x10) != 0;
    int crc_flag = (dg[0] & 0x40) != 0;

    int idx = ext_flag ? 4 : 2;

    if (seg_flag) {
        *last_flag = (dg[idx] & 0x80) != 0;
        *segment_number = ((dg[idx] & 0x7F) << 8) | dg[idx + 1];
        idx += 2;
    }

    if (ua_flag) {
        int li = dg[idx] & 0x0F;
        int tid_flag = (dg[idx] & 0x10) != 0;
        if (tid_flag) {
            *transport_id = (uint16_t)((dg[idx + 1] << 8) | dg[idx + 2]);
            idx += 3;
        }
        idx += (li - 2);
    }

    /* Segmentation header: RepetitionCount(3) + SegmentSize(13) */
    *seg_size_out = ((dg[idx] & 0x1F) << 8) | dg[idx + 1];
    *data_offset = idx + 2;

    /* CRC check */
    if (crc_flag) {
        uint16_t crc = crc16_ccitt(dg, (size_t)(dg_len - 2)) ^ 0xFFFF;
        uint16_t stored = (uint16_t)((dg[dg_len - 2] << 8) | dg[dg_len - 1]);
        if (crc != stored) return -2;
    }

    return 0;
}

int main(void) {
    const char *img_path = create_test_image();
    CHECK(img_path != NULL, "create_test_image failed\n");

    mot_enc_t *enc = mot_enc_new(img_path, "test.jpg", 42);
    CHECK(enc != NULL, "mot_enc_new failed\n");

    /* Collect all X-PAD fields until MOT is complete. */
    uint8_t xpad[MOT_XPAD_MAX];
    uint8_t fpad0, fpad1;
    int field_count = 0;
    int li_count = 0;      /* appType 1 (Length Indicator) */
    int start_count = 0;   /* appType 12 (DG start) */
    int cont_count = 0;    /* appType 13 (continuation) */

    /* We also reconstruct the first DG to verify its structure. */
    int first_dg_len = -1;
    uint8_t first_dg[1024];
    int first_dg_offset = 0;
    int collecting_first = 0;

    while (!mot_enc_complete(enc)) {
        int r = mot_enc_get_xpad(enc, xpad, sizeof(xpad), &fpad0, &fpad1);
        CHECK(r > 0, "mot_enc_get_xpad returned 0 before complete\n");
        CHECK(r <= MOT_XPAD_MAX, "xpad too large: %d\n", r);
        CHECK(fpad0 == 0x20, "fpad0 should be 0x20, got 0x%02x\n", fpad0);
        CHECK(fpad1 == 0x02, "fpad1 should be 0x02, got 0x%02x\n", fpad1);

        /* CI byte is at the highest address. */
        uint8_t ci = xpad[r - 1];
        int app_type = ci & 0x1F;
        int li_idx   = (ci >> 5) & 0x07;

        static const int ci_sizes[] = { 4, 6, 8, 12, 16, 24, 32, 48 };
        int subfield_sz = ci_sizes[li_idx];
        CHECK(r == subfield_sz + 2, "xpad_sz=%d != subfield(%d)+2\n", r, subfield_sz);

        /* End marker should be at r-2. */
        CHECK(xpad[r - 2] == 0x00, "end marker should be 0, got 0x%02x\n", xpad[r - 2]);

        switch (app_type) {
        case 1: {
            li_count++;
            /* Length indicator: 4 bytes, li_idx should be 0 (ci_size=4). */
            CHECK(li_idx == 0, "LI appType=1 should have li=0, got %d\n", li_idx);
            /* Extract DG length (first 14 bits of the 4-byte subfield).
             * Data is reversed: first logical byte nearest to end marker. */
            int dg_length = ((xpad[r - 3] & 0x3F) << 8) | xpad[r - 4];
            CHECK(dg_length > 0, "DG length should be >0, got %d\n", dg_length);
            uint8_t li_data[2] = { xpad[r - 3], xpad[r - 4] };
            uint16_t li_crc = (uint16_t)(crc16_ccitt(li_data, 2) ^ 0xFFFF);
            uint16_t li_stored = (uint16_t)((xpad[r - 5] << 8) | xpad[r - 6]);
            CHECK(li_crc == li_stored, "DG length indicator CRC mismatch\n");

            /* Start collecting the first DG. */
            if (li_count == 1) {
                first_dg_len = dg_length;
                first_dg_offset = 0;
                collecting_first = 1;
            } else {
                collecting_first = 0;
            }
            break;
        }
        case 12:
            start_count++;
            if (collecting_first && first_dg_offset == 0) {
                /* Extract data from X-PAD (reversed order). */
                for (int j = 0; j < subfield_sz && first_dg_offset < first_dg_len; ++j) {
                    first_dg[first_dg_offset++] = xpad[r - 3 - j];
                }
            }
            break;
        case 13:
            cont_count++;
            if (collecting_first && first_dg_offset > 0) {
                for (int j = 0; j < subfield_sz && first_dg_offset < first_dg_len; ++j) {
                    first_dg[first_dg_offset++] = xpad[r - 3 - j];
                }
            }
            break;
        default:
            CHECK(0, "unexpected appType %d\n", app_type);
        }

        field_count++;
        CHECK(field_count < 5000, "too many fields, possible infinite loop\n");
    }

    printf("MOT fields: %d total (%d LI, %d start, %d cont)\n",
           field_count, li_count, start_count, cont_count);

    /* Basic sanity checks. */
    CHECK(li_count > 0, "no appType=1 LIs emitted\n");
    CHECK(start_count == li_count, "appType12 count (%d) != LI count (%d)\n",
          start_count, li_count);
    CHECK(cont_count >= 0, "negative cont_count?!\n");

    /* Parse the first DG (header DG, groupType=3) like qt-dab does. */
    CHECK(first_dg_offset >= first_dg_len,
          "first DG incomplete: got %d of %d bytes\n",
          first_dg_offset, first_dg_len);

    int group_type, seg_num, last, seg_size, data_off;
    uint16_t tid;
    int rc = parse_dg_like_qtdab(first_dg, first_dg_len,
                                 &group_type, &seg_num, &last, &tid,
                                 &seg_size, &data_off);
    CHECK(rc == 0, "DG parse failed (rc=%d), likely LI bug in UA field\n", rc);
    CHECK(group_type == 3, "first DG should be groupType=3 (header), got %d\n", group_type);
    CHECK((first_dg[0] & 0x80) == 0, "MOT MSC data group must not set extension flag\n");
    CHECK(tid == 42, "transportId should be 42, got %d\n", tid);
    CHECK(seg_num == 0, "header DG segmentNumber should be 0, got %d\n", seg_num);
    CHECK(last == 1, "header DG lastFlag should be 1\n");
    CHECK(seg_size > 0 && seg_size < 300,
          "segmentSize should be reasonable, got %d\n", seg_size);

    /* Verify data_off + seg_size + 2 (CRC) == first_dg_len. */
    CHECK(data_off + seg_size + 2 == first_dg_len,
          "data_off(%d) + seg_size(%d) + 2 != dg_len(%d)\n",
           data_off, seg_size, first_dg_len);
    CHECK(data_off == 9, "MOT segment payload must start at byte 9, got %d\n", data_off);

    printf("First DG: groupType=%d tid=%d segNum=%d last=%d segSize=%d dataOff=%d\n",
           group_type, tid, seg_num, last, seg_size, data_off);

    mot_enc_free(enc);

    pad_sched_t *sched=pad_sched_new("MOT test",0,NULL);
    CHECK(sched!=NULL,"pad scheduler creation failed\n");
    CHECK(pad_sched_set_slide(sched,img_path,"updated.jpg",77)==0,
          "dynamic MOT image update failed\n");
    uint8_t live_xpad[PAD_SCHED_XPAD_MAX];uint8_t live_f0=0,live_f1=0;
    CHECK(pad_sched_get_xpad(sched,live_xpad,sizeof(live_xpad),&live_f0,&live_f1)>0,
          "dynamic MOT image produced no X-PAD\n");
    int guard=0;
    while(!pad_sched_slide_complete(sched) && guard++<5000)
        CHECK(pad_sched_get_xpad(sched,live_xpad,sizeof(live_xpad),&live_f0,&live_f1)>0,
              "dynamic MOT cycle stopped early\n");
    CHECK(guard<5000,"dynamic MOT cycle did not complete\n");

    /* A folder carousel must be able to observe completion.  Otherwise the
     * scheduler restarts the same image forever and the next file is never
     * selected. */
    CHECK(pad_sched_set_slide(sched,img_path,"carousel.jpg",78)==0,
          "carousel MOT image update failed\n");
    pad_sched_set_slide_auto_retx(sched, 0);
    guard=0;
    while (!pad_sched_slide_complete(sched) && guard++ < 5000)
        CHECK(pad_sched_get_xpad(sched,live_xpad,sizeof(live_xpad),&live_f0,&live_f1)>0,
              "carousel MOT cycle stopped early\n");
    CHECK(pad_sched_slide_complete(sched),
          "carousel slide completion is not observable\n");
    pad_sched_free(sched);
    remove(img_path);

    printf("test_mot: PASS\n");
    return 0;
}
