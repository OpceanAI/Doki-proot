#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/stat.h>

#include "daemon.h"
#include "../ipc/protocol.h"
#include "../extension/doki_hidden/doki_hidden.h"
#include "../extension/doki_portswitch/doki_portswitch.h"

typedef struct {
    int fd;
    char buffer[BUFFER_SIZE];
    size_t buffer_len;
} client_t;

static client_t clients[MAX_CLIENTS];
static int server_fd = -1;
static const char *g_rootfs = NULL;
static int g_running = 1;

static void add_client(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == 0) {
            clients[i].fd = fd;
            clients[i].buffer_len = 0;
            fprintf(stderr, "{\"type\":\"log\",\"msg\":\"client %d connected\"}\n", fd);
            return;
        }
    }
    close(fd);
}

static void remove_client(int idx) {
    fprintf(stderr, "{\"type\":\"log\",\"msg\":\"client %d disconnected\"}\n", clients[idx].fd);
    close(clients[idx].fd);
    clients[idx].fd = 0;
    clients[idx].buffer_len = 0;
}

static void handle_message(client_t *client) {
    char temp[4096];
    ssize_t n = recv(client->fd, temp, sizeof(temp) - 1, 0);

    if (n <= 0) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (&clients[i] == client) {
                remove_client(i);
                return;
            }
        }
        return;
    }

    temp[n] = '\0';

    if (client->buffer_len + (size_t)n < BUFFER_SIZE) {
        memcpy(client->buffer + client->buffer_len, temp, (size_t)n);
        client->buffer_len += (size_t)n;
        client->buffer[client->buffer_len] = '\0';
    } else {
        fprintf(stderr, "{\"type\":\"error\",\"msg\":\"client buffer overflow\"}\n");
        close(client->fd);
        client->fd = 0;
        client->buffer_len = 0;
        return;
    }

    char *newline;
    while ((newline = strchr(client->buffer, '\n')) != NULL) {
        *newline = '\0';
        ipc_process_message(client->fd, client->buffer);

        size_t msg_len = (size_t)(newline - client->buffer) + 1;
        if (client->buffer_len > msg_len) {
            memmove(client->buffer, client->buffer + msg_len,
                    client->buffer_len - msg_len);
        }
        client->buffer_len -= msg_len;
        if (client->buffer_len > 0) {
            client->buffer[client->buffer_len] = '\0';
        }
    }
}

static void handle_shutdown(int fd) {
    const char *resp = "{\"type\":\"shutdown_ack\",\"status\":\"ok\"}\n";
    send(fd, resp, strlen(resp), 0);
    g_running = 0;
}

static void handle_health(int fd) {
    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"type\":\"health\",\"status\":\"ok\",\"pid\":%d}\n", getpid());
    send(fd, resp, strlen(resp), 0);
}

static void handle_config(int fd, const char *json_msg) {
    doki_hidden_load_from_json(json_msg);
    doki_portswitch_load_from_json(json_msg);

    const char *resp = "{\"type\":\"config_ack\",\"status\":\"ok\"}\n";
    send(fd, resp, strlen(resp), 0);
}

int daemon_main(const char *socket_path, const char *config_path,
                const char *rootfs_path, int argc, char *argv[]) {
    (void)config_path;
    (void)argc;
    (void)argv;

    g_rootfs = rootfs_path ? rootfs_path : "/";

    /* Daemon init: umask, working dir, close stdin, write PID file */
    umask(022);
    chdir("/");
    freopen("/dev/null", "r", stdin);
    /* Write PID file */
    FILE *pidf = fopen("/tmp/doki-proot.pid", "w");
    if (pidf) {
        fprintf(pidf, "%d\n", getpid());
        fclose(pidf);
    }

    if (!socket_path) {
        socket_path = DEFAULT_SOCKET_PATH;
    }

    memset(clients, 0, sizeof(clients));

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        fprintf(stderr, "{\"type\":\"error\",\"msg\":\"socket: %s\"}\n", strerror(errno));
        return DAE_ERR_SOCKET;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    unlink(socket_path);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "{\"type\":\"error\",\"msg\":\"bind %s: %s\"}\n",
                socket_path, strerror(errno));
        close(server_fd);
        return DAE_ERR_BIND;
    }

    if (listen(server_fd, 5) < 0) {
        fprintf(stderr, "{\"type\":\"error\",\"msg\":\"listen: %s\"}\n", strerror(errno));
        close(server_fd);
        return DAE_ERR_LISTEN;
    }

    fprintf(stderr, "{\"type\":\"log\",\"msg\":\"doki-proot daemon listening on %s\"}\n",
            socket_path);

    while (g_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);

        int max_fd = server_fd;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd > 0) {
                FD_SET(clients[i].fd, &readfds);
                if (clients[i].fd > max_fd) {
                    max_fd = clients[i].fd;
                }
            }
        }

        struct timeval tv;
        tv.tv_sec = 30;  /* request timeout */
        tv.tv_usec = 0;

        int ret = select(max_fd + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            fprintf(stderr, "{\"type\":\"error\",\"msg\":\"select: %s\"}\n",
                    strerror(errno));
            continue;
        }

        if (FD_ISSET(server_fd, &readfds)) {
            int client_fd = accept(server_fd, NULL, NULL);
            if (client_fd >= 0) {
                add_client(client_fd);
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd > 0 && FD_ISSET(clients[i].fd, &readfds)) {
                handle_message(&clients[i]);
            }
        }
    }

    close(server_fd);
    unlink(socket_path);

    return DAE_OK;
}
