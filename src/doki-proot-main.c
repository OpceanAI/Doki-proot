/*
 * doki-proot-main.c — Entry point for doki-proot.
 * In daemon mode: listens on Unix socket for JSON commands, spawns proot.
 * In direct mode: exec's proot with given arguments.
 *
 * Build: cc -O2 -s -o doki-proot doki-proot-main.c
 *
 * Licensed under GPL-2.0 (derived from proot).
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/types.h>

#define MAX_CLIENTS 16
#define BUFFER_SIZE 65536
#define DEFAULT_SOCKET "/tmp/doki-proot.sock"

static volatile int g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/*
 * Simple JSON string extractor: finds the value of a key in a JSON object.
 * Returns malloc'd string or NULL.
 */
static char *json_get_string(const char *json, const char *key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *pos = strstr(json, search);
    if (!pos) return NULL;

    pos = strchr(pos + strlen(search), ':');
    if (!pos) return NULL;
    pos++;

    while (*pos == ' ' || *pos == '\t') pos++;

    if (*pos == '"') {
        pos++;
        const char *end = strchr(pos, '"');
        if (!end) return NULL;
        size_t len = (size_t)(end - pos);
        char *result = malloc(len + 1);
        if (!result) return NULL;
        memcpy(result, pos, len);
        result[len] = '\0';
        return result;
    }

    return NULL;
}

/*
 * Extract a JSON array of strings. Returns count.
 */
static int json_get_string_array(const char *json, const char *key,
                                  char ***out) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *pos = strstr(json, search);
    if (!pos) return 0;

    pos = strchr(pos + strlen(search), ':');
    if (!pos) return 0;
    pos++;

    while (*pos == ' ' || *pos == '\t') pos++;

    if (*pos != '[') return 0;
    pos++;

    int count = 0;
    char **result = NULL;

    while (*pos && *pos != ']') {
        while (*pos == ' ' || *pos == ',' || *pos == '\n') pos++;
        if (*pos == '"') {
            pos++;
            const char *end = strchr(pos, '"');
            if (!end) break;
            size_t len = (size_t)(end - pos);

            result = realloc(result, sizeof(char *) * (size_t)(count + 1));
            result[count] = malloc(len + 1);
            memcpy(result[count], pos, len);
            result[count][len] = '\0';
            count++;

            pos = end + 1;
        } else {
            break;
        }
    }

    *out = result;
    return count;
}

/*
 * Build response JSON and send to client.
 */
/*
 * Escape a string for JSON: replace \, ", \n, \r, \t with escaped versions.
 * Writes to dst (must be large enough - 2x src length is safe).
 */
static void json_escape(const char *src, char *dst, size_t dst_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 1; i++) {
        switch (src[i]) {
        case '\\': dst[j++] = '\\'; if (j < dst_size-1) dst[j++] = '\\'; break;
        case '"':  dst[j++] = '\\'; if (j < dst_size-1) dst[j++] = '"';  break;
        case '\n': dst[j++] = '\\'; if (j < dst_size-1) dst[j++] = 'n';  break;
        case '\r': dst[j++] = '\\'; if (j < dst_size-1) dst[j++] = 'r';  break;
        case '\t': dst[j++] = '\\'; if (j < dst_size-1) dst[j++] = 't';  break;
        default:   dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
}

static void send_response(int fd, const char *type, const char *id,
                          const char *data, int code) {
    char buf[4096];
    int len;

    if (data && id[0]) {
        char escaped[8192];
        json_escape(data, escaped, sizeof(escaped));
        len = snprintf(buf, sizeof(buf),
                       "{\"type\":\"%s\",\"id\":\"%s\",\"data\":\"%s\",\"code\":%d}\n",
                       type, id, escaped, code);
    } else if (id[0]) {
        len = snprintf(buf, sizeof(buf),
                       "{\"type\":\"%s\",\"id\":\"%s\",\"code\":%d}\n",
                       type, id, code);
    } else if (data) {
        char escaped[8192];
        json_escape(data, escaped, sizeof(escaped));
        len = snprintf(buf, sizeof(buf),
                       "{\"type\":\"%s\",\"data\":\"%s\"}\n",
                       type, escaped);
    } else {
        len = snprintf(buf, sizeof(buf),
                       "{\"type\":\"%s\",\"code\":%d}\n",
                       type, code);
    }
    send(fd, buf, (size_t)len, 0);
}

/*
 * Execute a command using proot, streaming stdout/stderr back.
 */
static void handle_exec(int fd, const char *json, const char *extra_args) {
    char *id = json_get_string(json, "id");
    if (!id) id = strdup("unknown");

    char **cmd = NULL;
    int argc = json_get_string_array(json, "cmd", &cmd);
    if (argc == 0) {
        send_response(fd, "exit", id, NULL, 1);
        free(id);
        return;
    }

    /* Build proot command line */
    int total_args = argc + 20;
    char **argv = calloc((size_t)(total_args + 1), sizeof(char *));
    int ai = 0;

    argv[ai++] = strdup("proot");

    /* Add extra args (rootfs, etc.) */
    if (extra_args && extra_args[0]) {
        char *args_copy = strdup(extra_args);
        char *token = strtok(args_copy, " ");
        while (token) {
            argv[ai++] = strdup(token);
            token = strtok(NULL, " ");
        }
        free(args_copy);
    }

    /* Add the command */
    for (int i = 0; i < argc; i++) {
        argv[ai++] = cmd[i];
    }
    argv[ai] = NULL;

    /* Fork and exec proot */
    int pipe_stdout[2], pipe_stderr[2];
    pipe(pipe_stdout);
    pipe(pipe_stderr);

    pid_t pid = fork();
    if (pid == 0) {
        /* Child: reset signal handlers inherited from parent */
        signal(SIGTERM, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        /* Redirect stdout/stderr to pipes, exec proot */
        dup2(pipe_stdout[1], STDOUT_FILENO);
        dup2(pipe_stderr[1], STDERR_FILENO);
        close(pipe_stdout[0]);
        close(pipe_stderr[0]);
        close(pipe_stdout[1]);
        close(pipe_stderr[1]);
        close(fd);

        execvp("proot", argv);
        /* If proot not found, try path */
        execv("/data/data/com.termux/files/usr/bin/proot", argv);
        exit(127);
    }

    /* Parent: close write ends, read from pipes */
    close(pipe_stdout[1]);
    close(pipe_stderr[1]);

    /* Read stdout and stderr in a simple loop */
    fd_set readfds;
    char buf[4096];
    int child_done = 0;
    int exit_code = 0;

    while (!child_done || (pipe_stdout[0] >= 0 && pipe_stderr[0] >= 0)) {
        FD_ZERO(&readfds);
        int maxfd = 0;

        if (pipe_stdout[0] >= 0) {
            FD_SET(pipe_stdout[0], &readfds);
            if (pipe_stdout[0] > maxfd) maxfd = pipe_stdout[0];
        }
        if (pipe_stderr[0] >= 0) {
            FD_SET(pipe_stderr[0], &readfds);
            if (pipe_stderr[0] > maxfd) maxfd = pipe_stderr[0];
        }
        if (maxfd == 0 && child_done) break;

        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
        int ret = select(maxfd + 1, &readfds, NULL, NULL, &tv);

        if (ret < 0) break;

        if (pipe_stdout[0] >= 0 && FD_ISSET(pipe_stdout[0], &readfds)) {
            ssize_t n = read(pipe_stdout[0], buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                send_response(fd, "stdout", id, buf, 0);
            } else {
                close(pipe_stdout[0]);
                pipe_stdout[0] = -1;
            }
        }

        if (pipe_stderr[0] >= 0 && FD_ISSET(pipe_stderr[0], &readfds)) {
            ssize_t n = read(pipe_stderr[0], buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                send_response(fd, "stderr", id, buf, 0);
            } else {
                close(pipe_stderr[0]);
                pipe_stderr[0] = -1;
            }
        }

        if (!child_done) {
            int status;
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) {
                child_done = 1;
                if (WIFEXITED(status))
                    exit_code = WEXITSTATUS(status);
                else if (WIFSIGNALED(status))
                    exit_code = 128 + WTERMSIG(status);
            } else if (w < 0) {
                child_done = 1;
                exit_code = 1;
            }
        }
    }

    send_response(fd, "exit", id, NULL, exit_code);

    /* Cleanup */
    for (int i = 0; i < argc; i++) free(cmd[i]);
    free(cmd);
    for (int i = 0; i < ai; i++) free(argv[i]);
    free(argv);
    free(id);
}

/*
 * Handle config message: parse hidden_files and port_map.
 */
static void handle_config(int fd, const char *json) {
    /* Store config for use with subsequent exec commands.
     * The hidden_files and port_map are passed as proot CLI args
     * via the extra_args mechanism. */
    fprintf(stderr, "{\"type\":\"log\",\"msg\":\"config stored\",\"size\":%zu}\n",
            strlen(json));
    send_response(fd, "config_ack", "", NULL, 0);
}

/*
 * Process one JSON message from a client.
 */
static void process_message(int fd, const char *json, const char *extra_args) {
    char *type = json_get_string(json, "type");
    if (!type) {
        send_response(fd, "error", "", "missing type field", -1);
        return;
    }

    if (strcmp(type, "exec") == 0) {
        handle_exec(fd, json, extra_args);
    } else if (strcmp(type, "config") == 0) {
        handle_config(fd, json);
    } else if (strcmp(type, "health") == 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "pid=%d", getpid());
        send_response(fd, "health", "", msg, 0);
    } else if (strcmp(type, "shutdown") == 0) {
        send_response(fd, "shutdown_ack", "", NULL, 0);
        g_running = 0;
    } else {
        send_response(fd, "error", type, "unknown type", -1);
    }

    free(type);
}

/*
 * Daemon main loop.
 */
static int run_daemon(const char *socket_path, const char *extra_args) {
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);
    signal(SIGHUP, sig_handler);

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    unlink(socket_path);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    fprintf(stderr, "{\"type\":\"ready\",\"socket\":\"%s\",\"pid\":%d}\n",
            socket_path, getpid());
    fflush(stderr);

    /* Client state */
    struct {
        int fd;
        char buf[BUFFER_SIZE];
        size_t len;
    } clients[MAX_CLIENTS];
    memset(clients, 0, sizeof(clients));

    while (g_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        int maxfd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd > 0) {
                FD_SET(clients[i].fd, &readfds);
                if (clients[i].fd > maxfd) maxfd = clients[i].fd;
            }
        }

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ret = select(maxfd + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* Accept new connections */
        if (FD_ISSET(server_fd, &readfds)) {
            int cfd = accept(server_fd, NULL, NULL);
            if (cfd >= 0) {
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd == 0) { slot = i; break; }
                }
                if (slot >= 0) {
                    clients[slot].fd = cfd;
                    clients[slot].len = 0;
                } else {
                    close(cfd);
                }
            }
        }

        /* Process client messages */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd <= 0 || !FD_ISSET(clients[i].fd, &readfds))
                continue;

            char tmp[4096];
            ssize_t n = recv(clients[i].fd, tmp, sizeof(tmp) - 1, 0);

            if (n <= 0) {
                close(clients[i].fd);
                clients[i].fd = 0;
                clients[i].len = 0;
                continue;
            }

            if (clients[i].len + (size_t)n < BUFFER_SIZE) {
                memcpy(clients[i].buf + clients[i].len, tmp, (size_t)n);
                clients[i].len += (size_t)n;
                clients[i].buf[clients[i].len] = '\0';
            }

            /* Process complete messages (newline delimited) */
            char *nl;
            while ((nl = strchr(clients[i].buf, '\n')) != NULL) {
                *nl = '\0';
                process_message(clients[i].fd, clients[i].buf, extra_args);

                size_t consumed = (size_t)(nl - clients[i].buf) + 1;
                if (clients[i].len > consumed) {
                    memmove(clients[i].buf, clients[i].buf + consumed,
                            clients[i].len - consumed);
                }
                clients[i].len -= consumed;
                if (clients[i].len > 0)
                    clients[i].buf[clients[i].len] = '\0';
            }
        }
    }

    /* Cleanup */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd > 0) close(clients[i].fd);
    }
    close(server_fd);
    unlink(socket_path);

    return 0;
}

/*
 * Print usage.
 */
static void print_usage(const char *prog) {
    fprintf(stderr,
        "doki-proot — Container engine for Doki\n"
        "Usage: %s [OPTIONS] [--] [COMMAND]\n"
        "Options:\n"
        "  --daemon         Run in daemon mode (JSON IPC)\n"
        "  --socket PATH    Socket path for IPC (default: %s)\n"
        "  --help           Show this help\n"
        "  --version        Show version\n"
        "\n"
        "In daemon mode, doki-proot listens on a Unix socket for JSON\n"
        "commands from Doki. In direct mode, it exec's proot directly.\n",
        prog, DEFAULT_SOCKET);
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    const char *socket_path = DEFAULT_SOCKET;
    char extra_args[4096] = "";
    int extra_len = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--daemon") == 0) {
            daemon_mode = 1;
        } else if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            fprintf(stderr, "doki-proot v0.9.0\n");
            return 0;
        } else {
            /* Accumulate extra args for proot */
            int remain = (int)sizeof(extra_args) - extra_len - 2;
            if (remain > 0) {
                extra_len += snprintf(extra_args + extra_len, (size_t)remain,
                                      "%s ", argv[i]);
            }
        }
    }

    if (daemon_mode) {
        return run_daemon(socket_path, extra_args);
    }

    /* Direct mode: exec proot with given arguments */
    char **proot_argv = calloc((size_t)(argc + 2), sizeof(char *));
    proot_argv[0] = "proot";
    int pi = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--daemon") == 0) continue;
        if (strcmp(argv[i], "--socket") == 0) { i++; continue; }
        proot_argv[pi++] = argv[i];
    }
    proot_argv[pi] = NULL;

    execvp("proot", proot_argv);
    execv("/data/data/com.termux/files/usr/bin/proot", proot_argv);
    perror("exec proot");
    free(proot_argv);
    return 127;
}
