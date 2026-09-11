#include "channels.h"
#include <string.h>

/*
 * Channel catalogue for the dabtx CLI. Two groups:
 *
 * 1) Official DAB+ Band III channels per ETSI EN 300 401 Annex E. These
 *    are lizenzpflichtig in DE — Tests nur mit Dummy-Load / Attenuator
 *    und abgeschirmt.
 *
 * 2) Amateur-radio experimental channels for licensed operators. DAB
 *    occupies ~1.536 MHz, which does not cleanly fit anywhere in the
 *    70 cm (430–440 MHz) band — the only ~1 MHz experimental window is
 *    434.0–435.0, and 435–438 is satellite-exclusive per IARU R1. For
 *    serious DAB+ ham experiments prefer 23 cm or 13 cm DATV segments
 *    where 2 MHz+ slots are available. Labels carry a "ham-" prefix so
 *    they cannot be confused with broadcast channels.
 *
 *    Use only with valid Amateurfunkzeugnis (Klasse A in DE), minimum
 *    power, announcement per bandplan, Dummy-Load or Attenuator — 70cm
 *    DAB+ is NOT an approved broadcast mode and falls under §16 AFuG
 *    "experimental digital modes".
 */
static const dab_channel_t CHANNELS[] = {
    /* Band III (broadcast, lizenzpflichtig) */
    { "5A",  174928000 }, { "5B",  176640000 }, { "5C",  178352000 }, { "5D",  180064000 },
    { "6A",  181936000 }, { "6B",  183648000 }, { "6C",  185360000 }, { "6D",  187072000 },
    { "7A",  188928000 }, { "7B",  190640000 }, { "7C",  192352000 }, { "7D",  194064000 },
    { "8A",  195936000 }, { "8B",  197648000 }, { "8C",  199360000 }, { "8D",  201072000 },
    { "9A",  202928000 }, { "9B",  204640000 }, { "9C",  206352000 }, { "9D",  208064000 },
    { "10A", 209936000 }, { "10N", 210096000 }, { "10B", 211648000 }, { "10C", 213360000 }, { "10D", 215072000 },
    { "11A", 216928000 }, { "11N", 217088000 }, { "11B", 218640000 }, { "11C", 220352000 }, { "11D", 222064000 },
    { "12A", 223936000 }, { "12N", 224096000 }, { "12B", 225648000 }, { "12C", 227360000 }, { "12D", 229072000 },
    { "13A", 230784000 }, { "13B", 232496000 }, { "13C", 234208000 }, { "13D", 235776000 },
    { "13E", 237488000 }, { "13F", 239200000 },

    /* L-band DAB blocks LA..LW (legacy/experimental allocations). */
    { "LA", 1452960000u }, { "LB", 1454672000u }, { "LC", 1456384000u },
    { "LD", 1458096000u }, { "LE", 1459808000u }, { "LF", 1461520000u },
    { "LG", 1463232000u }, { "LH", 1464944000u }, { "LI", 1466656000u },
    { "LJ", 1468368000u }, { "LK", 1470080000u }, { "LL", 1471792000u },
    { "LM", 1473504000u }, { "LN", 1475216000u }, { "LO", 1476928000u },
    { "LP", 1478640000u }, { "LQ", 1480352000u }, { "LR", 1482064000u },
    { "LS", 1483776000u }, { "LT", 1485488000u }, { "LU", 1487200000u },
    { "LV", 1488912000u }, { "LW", 1490624000u },

    /* 70 cm Amateurband — experimental segment 434.0–435.0 MHz (1 MHz only,
     * DAB+ at 1.536 MHz will spill; only for very short, coordinated tests
     * with minimum power and Dummy-Load). 435–438 MHz is IARU R1 satellite
     * exclusive — do NOT transmit there. */
    { "ham-70a", 434500000 },   /* 434.500 — center of the 1 MHz experimental slot */

    /* 23 cm Amateurband — DATV segment around 1298 MHz, ~2 MHz wide slots.
     * Much better fit for 1.5 MHz DAB+. Specific frequencies per IARU R1
     * bandplan / DARC Distrikt coordination. */
    { "ham-23a", 1298000000u }, /* 1298.000 — DATV-Mitte */
    { "ham-23b", 1299500000u }, /* 1299.500 — zweiter DATV-Slot */

    /* 13 cm Amateurband — DATV segment around 2345 MHz (2300–2450 MHz
     * overall). HackRF reaches this frequency, 2 MHz slots available. */
    { "ham-13a", 2345000000u }, /* 2345.000 — DATV */
    { "ham-13b", 2395000000u }, /* 2395.000 — DATV, upper end */

    /* 6 cm Amateurband — 5650–5850 MHz, HackRF max ~6 GHz. Wide slots
     * available for DATV/digital experiments, very little QRM. Requires
     * good coax/connectors (SMA loss significant at 5.7 GHz). */
    { "ham-6a",  5760000000ull }, /* 5760.000 — DATV center (IARU R1) */
};

const dab_channel_t *dab_channels(size_t *count) {
    if (count) *count = sizeof(CHANNELS) / sizeof(CHANNELS[0]);
    return CHANNELS;
}

const dab_channel_t *dab_channel_find(const char *label) {
    if (!label) return NULL;
    size_t n = sizeof(CHANNELS) / sizeof(CHANNELS[0]);
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(CHANNELS[i].label, label) == 0) return &CHANNELS[i];
    }
    return NULL;
}
