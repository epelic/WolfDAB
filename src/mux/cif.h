#pragma once
/*
 * Common Interleaved Frame (CIF) buffer for the MSC (Main Service Channel).
 *
 * In DAB Transmission Mode I one CIF is 55 296 bits = 6 912 bytes. It is
 * divided into 864 Capacity Units (CUs) of 64 bits each. Each subchannel
 * occupies a contiguous range of CUs described by start_addr and size, both
 * counted in CUs. See ETSI EN 300 401 §5.3.3.
 *
 * A 64 kbit/s DAB+ audio subchannel with EEP-3A protection has a CU size of
 * 24 CUs per 24 ms logical frame (192 bytes). Over 120 ms it spans 120 CUs
 * i.e. exactly one DAB+ superframe (960 bytes) split across 5 CIFs.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CIF_BYTES      6912u
#define CIF_CUS        864u
#define CU_BYTES       8u     /* one CU = 64 bits */

typedef struct {
    uint8_t data[CIF_BYTES];
} cif_t;

void cif_reset(cif_t *cif);

/*
 * Copy `size_cus * CU_BYTES` bytes of subchannel data into the CIF at
 * [start_addr*CU_BYTES .. (start_addr+size_cus)*CU_BYTES). Returns 0 on
 * success or -1 if the range overflows the CIF.
 */
int cif_write_subch(cif_t *cif,
                    unsigned start_addr_cus,
                    unsigned size_cus,
                    const uint8_t *bytes);

#ifdef __cplusplus
}
#endif
