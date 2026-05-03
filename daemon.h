#ifndef DAEMON_H
#define DAEMON_H

#include <stdint.h>
#include <stdbool.h>

/* IPC request types */
typedef enum {
    IPC_TOGGLE_WINDOW = 1,
    IPC_GET_WINDOW_STATE = 2,
    IPC_SCAN_WINDOWS = 3,
    IPC_GET_CONFIG = 4,
    IPC_RELOAD_CONFIG = 5
} IPCType;

/* IPC request structure */
typedef struct {
    IPCType type;
    uint32_t size;
    uint8_t data[];
} IPCRequest;

/* IPC response structure */
typedef struct {
    int32_t result;
    uint32_t size;
    uint8_t data[];
} IPCResponse;

/* Daemon state */
typedef struct {
    int socket_fd;
    pid_t pid;
    bool running;
} DaemonState;

/* Daemon PID file path */
#define DAEMON_PID_FILE "/tmp/window-toggle-daemon.pid"

/* Daemon socket path */
#define DAEMON_SOCKET_FILE "/tmp/window-toggle-daemon.sock"

/* Daemon functions */
int daemon_start(void);
int daemon_stop(void);
bool daemon_is_running(void);
bool daemon_send_request(IPCType type, const void *request_data, uint32_t request_size, void *response_data, uint32_t *response_size);
int daemon_status(void);

#endif /* DAEMON_H */
