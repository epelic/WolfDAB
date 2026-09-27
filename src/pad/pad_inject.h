#pragma once
/*
 * PAD injection into DAB+ superframe body (post-processing).
 *
 * After fdk-aac-dabplus produces the superframe body with TT_DABPLUS,
 * this module injects F-PAD + X-PAD into each AU by overwriting the
 * fill padding at the end of each AU and recalculating the AU CRC.
 *
 * Per ETSI TS 102 563 §5.4, the AU layout within the superframe is:
 *   [AAC bitstream] [X-PAD] [F-PAD (2 bytes)] [CRC (2 bytes)]
 * F-PAD is always the last 2 bytes of AU data (before CRC).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Inject PAD data into a raw superframe body (110*R bytes).
 *
 * For each of num_aus AUs, the last (pad_len + 2) bytes of AU data
 * (before the 2-byte CRC) are overwritten with [X-PAD pad_data] [F-PAD].
 * The AU CRC is then recalculated.
 *
 *   sf_body    : raw superframe body from fdk-aac-dabplus (110*R bytes)
 *   sf_body_len: must be 110 * (bitrate_kbps/8)
 *   bitrate_kbps: subchannel bitrate
 *   num_aus    : number of access units (3 for SBR, 6 for AAC-LC)
 *   pad_data   : X-PAD bytes to inject (NULL = no X-PAD, just F-PAD)
 *   pad_len    : number of X-PAD bytes (0 if pad_data is NULL)
 *   fpad0      : F-PAD byte 0 (X-PAD indicator + flags)
 *   fpad1      : F-PAD byte 1 (CI flag + reserved)
 *
 * Returns 0 on success, -1 on error.
 */
int pad_inject(uint8_t *sf_body, size_t sf_body_len,
               int bitrate_kbps, int num_aus,
               const uint8_t *pad_data, int pad_len,
               uint8_t fpad0, uint8_t fpad1);

#ifdef __cplusplus
}
#endif
