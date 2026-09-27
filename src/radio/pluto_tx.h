#pragma once
#include <complex.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pluto_tx pluto_tx_t;

typedef struct {
    uint64_t samples_pushed;
    uint64_t samples_consumed;
    uint64_t underruns;
    size_t ring_fill;
    size_t ring_capacity;
} pluto_tx_stats_t;

int pluto_tx_probe(void);
int pluto_tx_check_dab(void);
pluto_tx_t *pluto_tx_open(uint64_t freq_hz, unsigned attenuation_db);
pluto_tx_t *pluto_tx_open_ex(uint64_t freq_hz, unsigned attenuation_db,
                             int frequency_correction_hz);
int pluto_tx_start(pluto_tx_t *tx);
void pluto_tx_close(pluto_tx_t *tx);
size_t pluto_tx_writable(const pluto_tx_t *tx);
size_t pluto_tx_push(pluto_tx_t *tx, const float _Complex *samples, size_t n);
void pluto_tx_get_stats(const pluto_tx_t *tx, pluto_tx_stats_t *stats);
int pluto_tx_has_failed(const pluto_tx_t *tx);

#ifdef __cplusplus
}
#endif
