#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "doki_hidden.h"

typedef struct {
    char path[256];
} hidden_entry_t;

static hidden_entry_t hidden_entries[DOKI_HIDDEN_MAX_FILES];
static int hidden_count = 0;
static int config_loaded = 0;

/* Parse JSON like {"hidden_files":["/proc/self/cmdline","/proc/self/maps"]} */
void doki_hidden_load_from_json(const char *json_msg) {
    const char *key = strstr(json_msg, "\"hidden_files\"");
    if (!key) return;

    const char *arr = strchr(key, '[');
    if (!arr) return;
    arr++;

    hidden_count = 0;

    while (*arr && hidden_count < DOKI_HIDDEN_MAX_FILES) {
        while (*arr == ' ' || *arr == ',' || *arr == '"') arr++;
        if (*arr == ']' || *arr == '\0') break;

        const char *start = arr;
        while (*arr && *arr != '"') arr++;

        size_t len = (size_t)(arr - start);
        if (len > 0 && len < sizeof(hidden_entries[0].path)) {
            memcpy(hidden_entries[hidden_count].path, start, len);
            hidden_entries[hidden_count].path[len] = '\0';
            hidden_count++;
        }

        if (*arr == '"') arr++;
    }

    config_loaded = 1;
    fprintf(stderr, "{\"type\":\"log\",\"msg\":\"doki_hidden: %d paths configured\"}\n",
            hidden_count);
}

int doki_hidden_has_config(void) {
    return config_loaded && hidden_count > 0;
}

const char *doki_hidden_check_path(const char *path) {
    if (hidden_count == 0 || !path) return NULL;

    for (int i = 0; i < hidden_count; i++) {
        /* Exact match */
        if (strcmp(path, hidden_entries[i].path) == 0) {
            return DOKI_HIDDEN_REPLACEMENT;
        }
        /* Prefix match for directories */
        size_t len = strlen(hidden_entries[i].path);
        if (strncmp(path, hidden_entries[i].path, len) == 0 &&
            (path[len] == '/' || path[len] == '\0')) {
            return DOKI_HIDDEN_REPLACEMENT;
        }
    }

    return NULL;
}

void doki_hidden_clear(void) {
    hidden_count = 0;
    config_loaded = 0;
}

void doki_hidden_add_path(const char *path) {
    if (hidden_count >= DOKI_HIDDEN_MAX_FILES || !path) return;
    size_t len = strlen(path);
    if (len > 0 && len < sizeof(hidden_entries[0].path)) {
        memcpy(hidden_entries[hidden_count].path, path, len);
        hidden_entries[hidden_count].path[len] = '\0';
        hidden_count++;
    }
    config_loaded = 1;
}

int doki_hidden_count(void) {
    return hidden_count;
}
