#pragma once
#include <stddef.h>
#include <stdint.h>
#include "pa_source.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WOLFDAB_SOURCE_DEVICE = 0,
    WOLFDAB_SOURCE_TONE,
    WOLFDAB_SOURCE_MEDIA
} wolfdab_source_kind_t;

typedef struct wolfdab_source wolfdab_source_t;

wolfdab_source_t *wolfdab_source_open_device(int device_index, size_t ring_samples);
wolfdab_source_t *wolfdab_source_open_tone(double frequency_hz, double level_dbfs);
/* media may be a local pathname or an http/https URL. ffmpeg_exe is an
 * explicit path, normally the portable copy next to WolfDAB.exe. */
wolfdab_source_t *wolfdab_source_open_media(const wchar_t *ffmpeg_exe,
                                            const wchar_t *media);
wolfdab_source_t *wolfdab_source_open_device_ex(int device_index, size_t ring_samples, int sample_rate);
wolfdab_source_t *wolfdab_source_open_tone_ex(double frequency_hz, double level_dbfs, int sample_rate);
wolfdab_source_t *wolfdab_source_open_media_ex(const wchar_t *ffmpeg_exe, const wchar_t *media, int sample_rate);
size_t wolfdab_source_read(wolfdab_source_t *src, int16_t *dst, size_t samples);
const char *wolfdab_source_metadata(wolfdab_source_t *src);
void wolfdab_source_get_stats(const wolfdab_source_t *src, pa_source_stats_t *out);
void wolfdab_source_close(wolfdab_source_t *src);

#ifdef __cplusplus
}
#endif
