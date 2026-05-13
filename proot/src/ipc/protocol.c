#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>

#include "protocol.h"
#include "../extension/doki_hidden/doki_hidden.h"
#include "../extension/doki_portswitch/doki_portswitch.h"

static int parse_type(const char *json) {
    /* Simple parser: find "type":"..." */
    const char *key = strstr(json, "\"type\"");
    if (!key) return -1;

    const char *val = strchr(key, ':');
    if (!val) return -1;
    val++;

    while (*val == ' ' || *val == '"') val++;

    if (strncmp(val, "exec", 4) == 0) return 0;
    if (strncmp(val, "config", 6) == 0) return 1;
    if (strncmp(val, "signal", 6) == 0) return 2;
    if (strncmp(val, "health", 6) == 0) return 3;
    if (strncmp(val, "shutdown", 8) == 0) return 4;

    return -1;
}

void ipc_send_response(int fd, const char *type, const char *id,
                       const char *data, int code) {
    char resp[4096];
    if (id && data) {
        snprintf(resp, sizeof(resp),
                 "{\"type\":\"%s\",\"id\":\"%s\",\"data\":\"%s\",\"code\":%d}\n",
                 type, id, data, code);
    } else if (id) {
        snprintf(resp, sizeof(resp),
                 "{\"type\":\"%s\",\"id\":\"%s\",\"code\":%d}\n",
                 type, id, code);
    } else if (data) {
        snprintf(resp, sizeof(resp),
                 "{\"type\":\"%s\",\"data\":\"%s\"}\n",
                 type, data);
    } else {
        snprintf(resp, sizeof(resp),
                 "{\"type\":\"%s\",\"code\":%d}\n",
                 type, code);
    }
    send(fd, resp, strlen(resp), 0);
}

void ipc_process_message(int client_fd, const char *json_msg) {
    int msg_type = parse_type(json_msg);

    switch (msg_type) {
    case 0: { /* exec */
        /* Not implemented: requires full proot tracee/execve machinery */
        ipc_send_response(client_fd, "exec_error", NULL,
                          "exec not yet implemented in daemon mode", -1);
        break;
    }
    case 1: { /* config */
        doki_hidden_load_from_json(json_msg);
        doki_portswitch_load_from_json(json_msg);
        ipc_send_response(client_fd, "config_ack", NULL, NULL, 0);
        break;
    }
    case 2: { /* signal */
        char response[256];
        snprintf(response, sizeof(response),
                 "{\"type\":\"signal_ack\",\"status\":\"ok\"}\n");
        send(client_fd, response, strlen(response), 0);
        break;
    }
    case 3: { /* health */
        char response[256];
        snprintf(response, sizeof(response),
                 "{\"type\":\"health\",\"status\":\"ok\",\"pid\":%d}\n", getpid());
        send(client_fd, response, strlen(response), 0);
        break;
    }
    case 4: { /* shutdown */
        char response[256];
        snprintf(response, sizeof(response),
                 "{\"type\":\"shutdown_ack\",\"status\":\"ok\"}\n");
        send(client_fd, response, strlen(response), 0);
        break;
    }
    default:
        ipc_send_response(client_fd, "error", NULL, "unknown message type", -1);
        break;
    }
}
