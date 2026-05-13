#ifndef DOKI_HIDDEN_H
#define DOKI_HIDDEN_H

#define DOKI_HIDDEN_MAX_FILES 32
#define DOKI_HIDDEN_REPLACEMENT "/doki_internal/hidden"

void doki_hidden_load_from_json(const char *json_msg);
const char *doki_hidden_check_path(const char *path);
int doki_hidden_has_config(void);

#endif /* DOKI_HIDDEN_H */
