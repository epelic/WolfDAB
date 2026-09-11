#include "common/config.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); return 1; } \
} while (0)

int main(void) {
    /* Write a test config file. */
    {
        FILE *f = fopen("test_config.cfg", "w");
        CHECK(f != NULL, "cannot create test_config.cfg\n");
        fprintf(f, "# comment\n");
        fprintf(f, "ensemble = Test Ensemble\n");
        fprintf(f, "service=My Service\n");
        fprintf(f, "  dls = Hello World  \n");
        fprintf(f, "number = 42\n");
        fprintf(f, "\n");
        fprintf(f, "# another comment\n");
        fprintf(f, "empty =\n");
        fclose(f);
    }

    dabtx_cfg_t *cfg = dabtx_cfg_load("test_config.cfg");
    CHECK(cfg != NULL, "dabtx_cfg_load returned NULL\n");

    /* Basic lookups. */
    const char *v;
    v = dabtx_cfg_get(cfg, "ensemble");
    CHECK(v != NULL && strcmp(v, "Test Ensemble") == 0,
          "ensemble: got '%s'\n", v ? v : "(null)");

    v = dabtx_cfg_get(cfg, "service");
    CHECK(v != NULL && strcmp(v, "My Service") == 0,
          "service: got '%s'\n", v ? v : "(null)");

    v = dabtx_cfg_get(cfg, "dls");
    CHECK(v != NULL && strcmp(v, "Hello World") == 0,
          "dls: got '%s' (should be trimmed)\n", v ? v : "(null)");

    /* Integer. */
    CHECK(dabtx_cfg_get_int(cfg, "number", 0) == 42, "number != 42\n");
    CHECK(dabtx_cfg_get_int(cfg, "missing", 99) == 99, "missing default != 99\n");

    /* Empty value. */
    v = dabtx_cfg_get(cfg, "empty");
    CHECK(v != NULL && strcmp(v, "") == 0,
          "empty: got '%s'\n", v ? v : "(null)");

    /* Missing key. */
    CHECK(dabtx_cfg_get(cfg, "nonexistent") == NULL, "nonexistent should be NULL\n");

    /* Override. */
    dabtx_cfg_set(cfg, "ensemble", "Override");
    v = dabtx_cfg_get(cfg, "ensemble");
    CHECK(v != NULL && strcmp(v, "Override") == 0,
          "override: got '%s'\n", v ? v : "(null)");

    dabtx_cfg_free(cfg);

    /* Non-existent file → empty config, not NULL. */
    cfg = dabtx_cfg_load("nonexistent_file_xyz.cfg");
    CHECK(cfg != NULL, "nonexistent file should return empty config\n");
    CHECK(dabtx_cfg_get(cfg, "anything") == NULL, "empty config has no keys\n");
    dabtx_cfg_free(cfg);

    remove("test_config.cfg");
    printf("test_config: PASS\n");
    return 0;
}
