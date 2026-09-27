#include "pad_inject.h"
#include "common/crc16.h"
#include "common/log.h"

#include <stdlib.h>
#include <string.h>

/*
 * Parse AU start offsets from the superframe header.
 */
static int parse_au_starts(const uint8_t *sf, size_t sf_len,
                           int num_aus, int au_start[/*num_aus+1*/]) {
    int au_bits = (num_aus - 1) * 12;
    int au_field_bytes = (au_bits + 7) / 8;
    int hdr_sz = 2 + 1 + au_field_bytes;

    au_start[0] = hdr_sz;

    int bit_offset = 0;
    for (int i = 1; i < num_aus; ++i) {
        int byte_pos = 3 + (bit_offset / 8);
        int bit_pos  = bit_offset % 8;
        if (byte_pos + 1 >= (int)sf_len) return -1;

        uint16_t val;
        if (bit_pos == 0)
            val = ((uint16_t)sf[byte_pos] << 4) | (sf[byte_pos + 1] >> 4);
        else
            val = ((uint16_t)(sf[byte_pos] & 0x0F) << 8) | sf[byte_pos + 1];

        au_start[i] = (int)val;
        bit_offset += 12;
    }
    au_start[num_aus] = (int)sf_len;
    return num_aus;
}

/*
 * Find a DSE (Data Stream Element) in AU data by scanning for
 * element_id=4 (top 3 bits = 100). The DSE placed by fdk-aac has:
 *   byte 0: 0x81 (id=4, instance_tag=0, data_byte_align_flag=1)
 *   byte 1: count (number of payload bytes)
 * Total DSE size = 2 + count.
 *
 * Returns the byte offset within the AU, or -1 if not found.
 */
static int find_dse(const uint8_t *au_data, int au_data_len, int *dse_total) {
    for (int pos = 0; pos + 2 < au_data_len; ++pos) {
        if (au_data[pos] == 0x81) {
            int count = au_data[pos + 1];
            int total = 2 + count;
            if (pos + total <= au_data_len) {
                *dse_total = total;
                return pos;
            }
        }
    }
    return -1;
}

int pad_inject(uint8_t *sf_body, size_t sf_body_len,
               int bitrate_kbps, int num_aus,
               const uint8_t *pad_data, int pad_len,
               uint8_t fpad0, uint8_t fpad1) {
    (void)pad_data; (void)pad_len; (void)fpad0; (void)fpad1;

    if (!sf_body || sf_body_len == 0) return -1;

    int au_start[8];
    if (parse_au_starts(sf_body, sf_body_len, num_aus, au_start) < 0)
        return -1;

    /*
     * Strategy: the encoder (via IN_ANCILLRY_DATA) already placed a DSE
     * in the AAC bitstream of each AU, AFTER the audio elements. Qt-dab
     * only checks if the FIRST byte of the AU is a DSE (element_id=4).
     *
     * We find the DSE and rotate the AU data so the DSE comes first:
     *   Before: [Audio...][DSE][FIL][END]
     *   After:  [DSE][Audio...][FIL][END]
     *
     * This is a byte rotation, no data is lost, total size unchanged.
     */
    static int log_once = 0;

    for (int i = 0; i < num_aus; ++i) {
        int data_start = au_start[i];
        int au_end     = au_start[i + 1];
        int au_data_len = au_end - data_start - 2; /* exclude CRC */

        uint8_t *au = &sf_body[data_start];
        int dse_total = 0;
        int dse_pos = find_dse(au, au_data_len, &dse_total);

        if (dse_pos <= 0) continue; /* not found or already at front */

        if (!log_once) {
            LOGI("pad_swap: AU%d dse_pos=%d dse_total=%d au_data_len=%d",
                 i, dse_pos, dse_total, au_data_len);
        }

        /* Rotate: save DSE, shift audio data right, place DSE at front. */
        uint8_t *dse_tmp = (uint8_t *)malloc((size_t)dse_total);
        if (!dse_tmp) continue;

        memcpy(dse_tmp, &au[dse_pos], (size_t)dse_total);
        memmove(&au[dse_total], &au[0], (size_t)dse_pos);
        memcpy(&au[0], dse_tmp, (size_t)dse_total);
        free(dse_tmp);

        /* Recalculate AU CRC. */
        int crc_pos = au_end - data_start - 2;
        uint16_t crc = (uint16_t)(
            crc16_ccitt(au, (size_t)crc_pos) ^ 0xFFFF);
        au[crc_pos + 0] = (uint8_t)(crc >> 8);
        au[crc_pos + 1] = (uint8_t)(crc & 0xFF);
    }

    /* Recalculate Firecode (bytes 0-1 over bytes 2-10). */
    {
        uint16_t fc = crc16_firecode(&sf_body[2], 9);
        sf_body[0] = (uint8_t)(fc >> 8);
        sf_body[1] = (uint8_t)(fc & 0xFF);
    }

    if (!log_once) log_once = 1;
    return 0;
}
