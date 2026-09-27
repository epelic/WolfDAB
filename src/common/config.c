#include "config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ENTRIES 64
#define MAX_LINE   512

typedef struct {
    char *key;
    char *value;
} cfg_entry_t;

struct dabtx_cfg {
    cfg_entry_t entries[MAX_ENTRIES];
    int         count;
};

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) ++s;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return s;
}

static char *my_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

dabtx_cfg_t *dabtx_cfg_load(const char *path) {
    dabtx_cfg_t *cfg = (dabtx_cfg_t *)calloc(1, sizeof(*cfg));
    if (!cfg) return NULL;

    if (!path || !path[0]) return cfg;

    FILE *f = fopen(path, "r");
    if (!f) return cfg; /* file not found → empty config, not an error */

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (*p == '\0' || *p == '#') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(p);
        char *val = trim(eq + 1);

        if (*key == '\0') continue;
        dabtx_cfg_set(cfg, key, val);
    }
    fclose(f);
    return cfg;
}

void dabtx_cfg_free(dabtx_cfg_t *cfg) {
    if (!cfg) return;
    for (int i = 0; i < cfg->count; ++i) {
        free(cfg->entries[i].key);
        free(cfg->entries[i].value);
    }
    free(cfg);
}

const char *dabtx_cfg_get(const dabtx_cfg_t *cfg, const char *key) {
    if (!cfg || !key) return NULL;
    for (int i = 0; i < cfg->count; ++i) {
        if (strcmp(cfg->entries[i].key, key) == 0)
            return cfg->entries[i].value;
    }
    return NULL;
}

int dabtx_cfg_get_int(const dabtx_cfg_t *cfg, const char *key, int def) {
    const char *v = dabtx_cfg_get(cfg, key);
    if (!v) return def;
    return atoi(v);
}

void dabtx_cfg_set(dabtx_cfg_t *cfg, const char *key, const char *value) {
    if (!cfg || !key) return;
    /* Overwrite existing? */
    for (int i = 0; i < cfg->count; ++i) {
        if (strcmp(cfg->entries[i].key, key) == 0) {
            free(cfg->entries[i].value);
            cfg->entries[i].value = my_strdup(value ? value : "");
            return;
        }
    }
    /* New entry. */
    if (cfg->count >= MAX_ENTRIES) return;
    cfg->entries[cfg->count].key   = my_strdup(key);
    cfg->entries[cfg->count].value = my_strdup(value ? value : "");
    cfg->count++;
}
