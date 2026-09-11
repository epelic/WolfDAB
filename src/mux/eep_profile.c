#include "eep_profile.h"

unsigned dab_eep_cu_for_bitrate(unsigned bitrate_kbps, dab_eep_profile_t profile) {
    static const unsigned a[] = {12, 8, 6, 4};
    static const unsigned b[] = {27, 21, 18, 15};
    unsigned n, d;
    if (profile <= DAB_EEP_4A) {
        n = bitrate_kbps * a[(unsigned)profile]; d = 8;
    } else if (profile <= DAB_EEP_4B) {
        n = bitrate_kbps * b[(unsigned)profile - DAB_EEP_1B]; d = 32;
    } else return 0;
    return n % d == 0 ? n / d : 0;
}

int dab_eep_fig01_fields(dab_eep_profile_t profile, unsigned *option, unsigned *level) {
    if (!option || !level || profile > DAB_EEP_4B) return -1;
    if (profile <= DAB_EEP_4A) { *option = 0; *level = (unsigned)profile; }
    else { *option = 1; *level = (unsigned)profile - DAB_EEP_1B; }
    return 0;
}

const char *dab_eep_profile_name(dab_eep_profile_t profile) {
    static const char *n[] = {"EEP-1A", "EEP-2A", "EEP-3A", "EEP-4A",
                              "EEP-1B", "EEP-2B", "EEP-3B", "EEP-4B"};
    return profile <= DAB_EEP_4B ? n[(unsigned)profile] : "invalid";
}
