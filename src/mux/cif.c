#include "cif.h"
#include <string.h>

void cif_reset(cif_t *cif) {
    memset(cif->data, 0, sizeof(cif->data));
}

int cif_write_subch(cif_t *cif,
                    unsigned start_addr_cus,
                    unsigned size_cus,
                    const uint8_t *bytes) {
    size_t start = (size_t)start_addr_cus * CU_BYTES;
    size_t len   = (size_t)size_cus       * CU_BYTES;
    if (start + len > CIF_BYTES) return -1;
    memcpy(cif->data + start, bytes, len);
    return 0;
}
