#ifndef IPC_H
#define IPC_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include "daemon.h"

/* IPC functions for client-side communication */
int ipc_connect(void);
void ipc_disconnect(int fd);
bool ipc_send_request(int fd, IPCType type, const void *data, uint32_t size);
bool ipc_recv_response(int fd, void *data, uint32_t *size);

/* IPC functions for server-side communication */
int ipc_create_socket(const char *path);
int ipc_accept_client(int server_fd);
bool ipc_recv_request(int client_fd, IPCType *type, void *data, uint32_t *size);
bool ipc_send_response(int client_fd, int32_t result, const void *data, uint32_t size);

#endif /* IPC_H */
