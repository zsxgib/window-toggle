/*
 * IPC - Inter-Process Communication via Unix Domain Socket
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include "ipc.h"
#include "daemon.h"

/* Connect to daemon */
int ipc_connect(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DAEMON_SOCKET_FILE, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

/* Disconnect from daemon */
void ipc_disconnect(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

/* Send request to daemon */
bool ipc_send_request(int fd, IPCType type, const void *data, uint32_t size) {
    if (fd < 0) return false;

    /* Send type */
    if (write(fd, &type, sizeof(type)) != sizeof(type)) {
        return false;
    }

    /* Send size */
    if (write(fd, &size, sizeof(size)) != sizeof(size)) {
        return false;
    }

    /* Send data */
    if (size > 0 && data != NULL) {
        if (write(fd, data, size) != (ssize_t)size) {
            return false;
        }
    }

    return true;
}

/* Receive response from daemon */
bool ipc_recv_response(int fd, void *data, uint32_t *size) {
    if (fd < 0) return false;

    /* Receive result */
    int32_t result;
    if (read(fd, &result, sizeof(result)) != sizeof(result)) {
        return false;
    }

    /* Receive data size */
    uint32_t data_size;
    if (read(fd, &data_size, sizeof(data_size)) != sizeof(data_size)) {
        return false;
    }

    /* Receive data */
    if (data_size > 0 && data != NULL) {
        if (data_size > *size) {
            return false;
        }
        if (read(fd, data, data_size) != (ssize_t)data_size) {
            return false;
        }
        *size = data_size;
    }

    return true;
}

/* Create server socket */
int ipc_create_socket(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    /* Remove existing socket file */
    unlink(path);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 5) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

/* Accept client connection */
int ipc_accept_client(int server_fd) {
    return accept(server_fd, NULL, NULL);
}

/* Receive request from client */
bool ipc_recv_request(int client_fd, IPCType *type, void *data, uint32_t *size) {
    if (client_fd < 0) return false;

    /* Receive type */
    if (read(client_fd, type, sizeof(*type)) != sizeof(*type)) {
        return false;
    }

    /* Receive size */
    if (read(client_fd, size, sizeof(*size)) != sizeof(*size)) {
        return false;
    }

    /* Receive data */
    if (*size > 0 && data != NULL) {
        if (*size > 4096) {  /* Sanity check */
            return false;
        }
        if (read(client_fd, data, *size) != (ssize_t)*size) {
            return false;
        }
    }

    return true;
}

/* Send response to client */
bool ipc_send_response(int client_fd, int32_t result, const void *data, uint32_t size) {
    if (client_fd < 0) return false;

    /* Send result */
    if (write(client_fd, &result, sizeof(result)) != sizeof(result)) {
        return false;
    }

    /* Send size */
    if (write(client_fd, &size, sizeof(size)) != sizeof(size)) {
        return false;
    }

    /* Send data */
    if (size > 0 && data != NULL) {
        if (write(client_fd, data, size) != (ssize_t)size) {
            return false;
        }
    }

    return true;
}
