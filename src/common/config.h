#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal key=value config file parser for dabtx.
 *
 * File format (dabtx.cfg):
 *   # comment
 *   key = value
 *   key=value        (spaces around '=' are optional)
 *   # blank lines and lines starting with '#' are ignored
 *   # keys and values are trimmed of leading/trailing whitespace
 *
 * Usage:
 *   dabtx_cfg_t *cfg = dabtx_cfg_load("dabtx.cfg");
 *   const char *v = dabtx_cfg_get(cfg, "channel");
 *   int n = dabtx_cfg_get_int(cfg, "txvga", 0);
 *   dabtx_cfg_free(cfg);
 */

typedef struct dabtx_cfg dabtx_cfg_t;

/* Load config from file. Returns NULL on error (file not found is not
 * an error — returns an empty config). */
dabtx_cfg_t *dabtx_cfg_load(const char *path);
void         dabtx_cfg_free(dabtx_cfg_t *cfg);

/* Get a value by key.  Returns NULL if not found. */
const char *dabtx_cfg_get(const dabtx_cfg_t *cfg, const char *key);

/* Get as integer with default. */
int dabtx_cfg_get_int(const dabtx_cfg_t *cfg, const char *key, int def);

/* Set a value (e.g. from CLI override).  Overwrites existing key. */
void dabtx_cfg_set(dabtx_cfg_t *cfg, const char *key, const char *value);

#ifdef __cplusplus
}
#endif
