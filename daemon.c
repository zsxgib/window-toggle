/*
 * Daemon - Persistent X connection daemon
 *
 * Maintains a single persistent X connection and handles
 * all window operations via IPC from clients.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <X11/Xlib.h>
#include "daemon.h"
#include "ipc.h"
#include "config.h"
#include "window-manager.h"

/* Global daemon state */
static Display *g_display = NULL;
static int g_socket_fd = -1;
static volatile sig_atomic_t g_running = 1;

/* Signal handler */
static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running = 0;
    }
}

/* Write PID file */
static int write_pid_file(void) {
    FILE *fp = fopen(DAEMON_PID_FILE, "w");
    if (!fp) return -1;
    fprintf(fp, "%d\n", getpid());
    fclose(fp);
    return 0;
}

/* Remove PID file */
static void remove_pid_file(void) {
    unlink(DAEMON_PID_FILE);
}

/* Check if daemon is already running */
bool daemon_is_running(void) {
    FILE *fp = fopen(DAEMON_PID_FILE, "r");
    if (!fp) return false;

    pid_t pid;
    if (fscanf(fp, "%d", &pid) != 1) {
        fclose(fp);
        return false;
    }
    fclose(fp);

    /* Check if process exists */
    if (kill(pid, 0) < 0) {
        return false;
    }

    return true;
}

/* Initialize X connection */
static Display* init_x_connection(void) {
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Failed to open X display\n");
        return NULL;
    }
    return display;
}

/* Handle IPC request */
static void handle_request(int client_fd) {
    IPCType type;
    uint32_t size;
    uint8_t buffer[4096];

    /* Receive request */
    size = sizeof(buffer);
    if (!ipc_recv_request(client_fd, &type, buffer, &size)) {
        return;
    }

    int32_t result = 0;
    uint32_t resp_size = 0;
    uint8_t resp_data[4096];
    memset(resp_data, 0, sizeof(resp_data));

    switch (type) {
        case IPC_TOGGLE_WINDOW: {
            if (size >= sizeof(Window)) {
                Window window = *(Window *)buffer;
                int state = get_window_state(g_display, window);
                if (state == STATE_HIDDEN) {
                    activate_window(g_display, window);
                } else if (state == STATE_VISIBLE) {
                    minimize_window(g_display, window);
                }
                result = 0;
            } else {
                result = -1;
            }
            break;
        }

        case IPC_GET_WINDOW_STATE: {
            if (size >= sizeof(Window)) {
                Window window = *(Window *)buffer;
                int state = get_window_state(g_display, window);
                resp_size = sizeof(int);
                *(int *)resp_data = state;
                result = 0;
            } else {
                result = -1;
            }
            break;
        }

        case IPC_SCAN_WINDOWS: {
            Window *windows = NULL;
            int count = scan_all_windows(&windows);
            if (count > 0 && windows) {
                /* Return window list */
                resp_size = sizeof(Window) * count;
                if (resp_size > sizeof(resp_data)) {
                    resp_size = sizeof(resp_data);
                }
                memcpy(resp_data, windows, resp_size);
                result = count;
                free_window_list(windows);
            } else {
                result = 0;
            }
            break;
        }

        case IPC_RELOAD_CONFIG:
            /* Reload configuration - no-op for now */
            result = 0;
            break;

        default:
            result = -1;
            break;
    }

    /* Send response */
    ipc_send_response(client_fd, result, resp_data, resp_size);
}

/* Main daemon loop */
static int daemon_loop(void) {
    /* Set up signal handlers */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);

    /* Initialize X connection */
    g_display = init_x_connection();
    if (!g_display) {
        return 1;
    }

    /* Create IPC socket */
    g_socket_fd = ipc_create_socket(DAEMON_SOCKET_FILE);
    if (g_socket_fd < 0) {
        fprintf(stderr, "Failed to create socket\n");
        XCloseDisplay(g_display);
        return 1;
    }

    /* Set socket to non-blocking */
    fcntl(g_socket_fd, F_SETFL, O_NONBLOCK);

    /* Main loop */
    while (g_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(g_socket_fd, &rfds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(g_socket_fd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (ret == 0) continue;

        if (FD_ISSET(g_socket_fd, &rfds)) {
            int client_fd = ipc_accept_client(g_socket_fd);
            if (client_fd >= 0) {
                handle_request(client_fd);
                close(client_fd);
            }
        }
    }

    /* Cleanup */
    if (g_socket_fd >= 0) {
        close(g_socket_fd);
    }
    if (g_display) {
        XCloseDisplay(g_display);
    }
    remove_pid_file();
    unlink(DAEMON_SOCKET_FILE);

    return 0;
}

/* Start daemon */
int daemon_start(void) {
    /* Check if already running */
    if (daemon_is_running()) {
        fprintf(stderr, "Daemon is already running\n");
        return 0;
    }

    /* Fork */
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Failed to fork\n");
        return -1;
    }

    if (pid > 0) {
        /* Parent process */
        /* Wait briefly for child to start */
        usleep(100000);
        if (daemon_is_running()) {
            printf("Daemon started (PID: %d)\n", pid);
            return 0;
        } else {
            fprintf(stderr, "Failed to start daemon\n");
            return -1;
        }
    }

    /* Child process - become daemon */
    setsid();

    /* Write PID file */
    if (write_pid_file() < 0) {
        exit(1);
    }

    /* Change to root directory to avoid mount issues */
    chdir("/");

    /* Close standard file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    /* Run main loop */
    int ret = daemon_loop();

    exit(ret);
}

/* Stop daemon */
int daemon_stop(void) {
    FILE *fp = fopen(DAEMON_PID_FILE, "r");
    if (!fp) {
        fprintf(stderr, "Daemon is not running\n");
        return -1;
    }

    pid_t pid;
    if (fscanf(fp, "%d", &pid) != 1) {
        fclose(fp);
        fprintf(stderr, "Invalid PID file\n");
        return -1;
    }
    fclose(fp);

    if (kill(pid, SIGTERM) < 0) {
        fprintf(stderr, "Failed to stop daemon: %s\n", strerror(errno));
        return -1;
    }

    /* Wait for daemon to stop */
    int count = 0;
    while (daemon_is_running() && count < 50) {
        usleep(100000);
        count++;
    }

    if (daemon_is_running()) {
        fprintf(stderr, "Daemon did not stop in time\n");
        return -1;
    }

    printf("Daemon stopped\n");
    return 0;
}

/* Send request to daemon (client-side) */
bool daemon_send_request(IPCType type, const void *request_data, uint32_t request_size, void *response_data, uint32_t *response_size) {
    int fd = ipc_connect();
    if (fd < 0) {
        return false;
    }

    if (!ipc_send_request(fd, type, request_data, request_size)) {
        ipc_disconnect(fd);
        return false;
    }

    if (response_data && response_size) {
        if (!ipc_recv_response(fd, response_data, response_size)) {
            ipc_disconnect(fd);
            return false;
        }
    }

    ipc_disconnect(fd);
    return true;
}

/* Show daemon status */
int daemon_status(void) {
    if (daemon_is_running()) {
        FILE *fp = fopen(DAEMON_PID_FILE, "r");
        if (fp) {
            pid_t pid;
            if (fscanf(fp, "%d", &pid) == 1) {
                printf("Daemon is running (PID: %d)\n", pid);
                fclose(fp);
                return 0;
            }
            fclose(fp);
        }
        printf("Daemon is running\n");
        return 0;
    } else {
        printf("Daemon is not running\n");
        return 1;
    }
}
