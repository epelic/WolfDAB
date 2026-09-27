#pragma once

/* ETSI EN 300 401 long-form Equal Error Protection. A Mode-I ensemble has
 * exactly 864 Capacity Units (CU) in each MSC CIF. */
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DAB_EEP_1A, DAB_EEP_2A, DAB_EEP_3A, DAB_EEP_4A,
    DAB_EEP_1B, DAB_EEP_2B, DAB_EEP_3B, DAB_EEP_4B,
} dab_eep_profile_t;

/* Returns occupied CU or zero if the bitrate/profile pair is invalid. */
unsigned dab_eep_cu_for_bitrate(unsigned bitrate_kbps, dab_eep_profile_t profile);
/* Values to put into FIG 0/1: option 0=A / 1=B, zero-based level. */
int dab_eep_fig01_fields(dab_eep_profile_t profile, unsigned *option, unsigned *level);
const char *dab_eep_profile_name(dab_eep_profile_t profile);

#ifdef __cplusplus
}
#endif
