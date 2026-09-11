#include "mux/eep_profile.h"
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
int main(void) {
    CHECK(dab_eep_cu_for_bitrate(72, DAB_EEP_1A) == 108);
    CHECK(dab_eep_cu_for_bitrate(72, DAB_EEP_2A) == 72);
    CHECK(dab_eep_cu_for_bitrate(72, DAB_EEP_3A) == 54);
    CHECK(dab_eep_cu_for_bitrate(72, DAB_EEP_4A) == 36);
    CHECK(dab_eep_cu_for_bitrate(96, DAB_EEP_1B) == 81);
    CHECK(dab_eep_cu_for_bitrate(32, DAB_EEP_2B) == 21);
    CHECK(dab_eep_cu_for_bitrate(72, DAB_EEP_1B) == 0);
    unsigned o, l;
    CHECK(dab_eep_fig01_fields(DAB_EEP_3A, &o, &l) == 0 && o == 0 && l == 2);
    CHECK(dab_eep_fig01_fields(DAB_EEP_4B, &o, &l) == 0 && o == 1 && l == 3);
    return 0;
}
