#ifndef IPC_PROTOCOL_H
#define IPC_PROTOCOL_H

#include <stddef.h>

void ipc_process_message(int client_fd, const char *json_msg);
void ipc_send_response(int fd, const char *type, const char *id,
                       const char *data, int code);

#endif /* IPC_PROTOCOL_H */
