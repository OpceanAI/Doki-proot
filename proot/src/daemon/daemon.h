#ifndef DAE_MON_H
#define DAE_MON_H

#include <stdint.h>

#define MAX_CLIENTS 16
#define BUFFER_SIZE 65536
#define DEFAULT_SOCKET_PATH "/tmp/doki-proot.sock"

typedef enum {
    DAE_OK = 0,
    DAE_ERR_SOCKET = -1,
    DAE_ERR_BIND = -2,
    DAE_ERR_LISTEN = -3,
    DAE_ERR_MAX_CLIENTS = -4,
} daemon_status_t;

typedef enum {
    REQ_EXEC = 1,
    REQ_CONFIG = 2,
    REQ_SIGNAL = 3,
    REQ_HEALTH = 4,
    REQ_SHUTDOWN = 5,
} request_type_t;

int daemon_main(const char *socket_path, const char *config_path,
                const char *rootfs_path, int argc, char *argv[]);

#endif /* DAE_MON_H */
