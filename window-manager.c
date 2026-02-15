#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <sys/wait.h>
#include <unistd.h>

#include "window-manager.h"

#define MAX_WINDOWS 100

int scan_all_windows(Window **windows) {
    Display *display = XOpenDisplay(NULL);
    if (!display) return 0;

    Window root = DefaultRootWindow(display);
    Atom net_client_list = XInternAtom(display, "_NET_CLIENT_LIST", False);

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    int status = XGetWindowProperty(display, root, net_client_list,
                                    0, ~0L, False, XA_WINDOW,
                                    &actual_type, &actual_format,
                                    &nitems, &bytes_after, &data);

    if (status != Success || data == NULL) {
        XCloseDisplay(display);
        return 0;
    }

    Window *client_list = (Window *)data;
    int count = (nitems < MAX_WINDOWS) ? nitems : MAX_WINDOWS;

    *windows = malloc(count * sizeof(Window));
    if (!*windows) {
        XFree(data);
        XCloseDisplay(display);
        return 0;
    }

    for (int i = 0; i < count; i++) {
        (*windows)[i] = client_list[i];
    }

    XFree(data);
    XCloseDisplay(display);

    return count;
}

void free_window_list(Window *windows) {
    free(windows);
}

/* Convert Nautilus window titles to full paths */
char* nautilus_path_lookup(Display *display, Window window, char *title) {
    /* Try to get window object path via D-Bus */
    Atom atom = XInternAtom(display, "_GTK_WINDOW_OBJECT_PATH", False);
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    int status = XGetWindowProperty(display, window, atom,
                                    0, ~0L, False, AnyPropertyType,
                                    &actual_type, &actual_format,
                                    &nitems, &bytes_after, &data);

    if (status == Success && data != NULL && nitems > 0) {
        char *window_path = (char*)data;

        /* Try to query D-Bus for window information */
        /* Note: This would require GIO integration which may be complex */
        /* For now, we'll use window_path if it contains useful info */

        /* Check if window_path contains a window number */
        if (strstr(window_path, "/window/")) {
            /* Extract window number */
            char *window_num = strrchr(window_path, '/') + 1;
            if (window_num) {
                /* We have the window number, we could use it to query D-Bus */
                /* But since we don't have GIO in the build, we'll use fallback */
            }
        }

        XFree(data);
    }

    /* Map common Nautilus folder names to full paths */
    if (strcmp(title, "主文件夹") == 0) {
        free(title);
        return strdup("/home/zsx");
    }
    else if (strcmp(title, "下载") == 0) {
        free(title);
        return strdup("/home/zsx/下载");
    }
    else if (strcmp(title, "Desktop") == 0 || strcmp(title, "桌面") == 0) {
        free(title);
        return strdup("/home/zsx/桌面");
    }
    else if (strcmp(title, "Documents") == 0 || strcmp(title, "文档") == 0) {
        free(title);
        return strdup("/home/zsx/文档");
    }
    else if (strcmp(title, "Pictures") == 0 || strcmp(title, "图片") == 0) {
        free(title);
        return strdup("/home/zsx/图片");
    }
    else if (strcmp(title, "Videos") == 0 || strcmp(title, "视频") == 0) {
        free(title);
        return strdup("/home/zsx/视频");
    }
    else if (strcmp(title, "Music") == 0 || strcmp(title, "音乐") == 0) {
        free(title);
        return strdup("/home/zsx/音乐");
    }
    else if (strcmp(title, "Templates") == 0 || strcmp(title, "模板") == 0) {
        free(title);
        return strdup("/home/zsx/模板");
    }
    else if (strcmp(title, "Public") == 0 || strcmp(title, "公共") == 0) {
        free(title);
        return strdup("/home/zsx/公共");
    }

    /* If no match found, check if title already looks like a path */
    if (title[0] == '/') {
        /* Already a path, return as-is */
        return title;
    }

    /* If no match found, return original title */
    return title;
}

char* get_window_title(Display *display, Window window) {
    XTextProperty prop;
    char **list = NULL;
    int count = 0;
    char *title = NULL;

    /* Try to get _NET_WM_NAME first (more detailed info) */
    Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
    if (XGetTextProperty(display, window, &prop, net_wm_name)) {
        Status status = Xutf8TextPropertyToTextList(display, &prop, &list, &count);
        if (status < Success || count <= 0 || !list[0]) {
            status = XmbTextPropertyToTextList(display, &prop, &list, &count);
        }

        if (count > 0 && list[0]) {
            title = strdup(list[0]);
            XFreeStringList(list);
            XFree(prop.value);

            /* Check if this is a Nautilus window and convert paths */
            XClassHint hint;
            if (XGetClassHint(display, window, &hint) && hint.res_class) {
                if (strcmp(hint.res_class, "org.gnome.Nautilus") == 0) {
                    title = nautilus_path_lookup(display, window, title);
                }
                XFree(hint.res_class);
                if (hint.res_name) XFree(hint.res_name);
            }

            return title;
        }
        XFree(prop.value);
    }

    /* Fallback to regular WM_NAME */
    if (XGetWMName(display, window, &prop)) {
        /* Try UTF-8 first, fallback to locale-specific */
        Status status = Xutf8TextPropertyToTextList(display, &prop, &list, &count);
        if (status < Success || count <= 0 || !list[0]) {
            /* Fallback to XmbTextPropertyToTextList */
            status = XmbTextPropertyToTextList(display, &prop, &list, &count);
        }

        if (count > 0 && list[0]) {
            title = strdup(list[0]);
            XFreeStringList(list);
            XFree(prop.value);

            /* Check if this is a Nautilus window and convert paths */
            XClassHint hint;
            if (XGetClassHint(display, window, &hint) && hint.res_class) {
                if (strcmp(hint.res_class, "org.gnome.Nautilus") == 0) {
                    title = nautilus_path_lookup(display, window, title);
                }
                XFree(hint.res_class);
                if (hint.res_name) XFree(hint.res_name);
            }

            return title;
        }
        XFree(prop.value);
    }

    return strdup("(Untitled)");
}

char* get_window_class(Display *display, Window window) {
    XClassHint hint;
    char *class_name = "(Unknown)";

    if (XGetClassHint(display, window, &hint)) {
        if (hint.res_class) {
            class_name = strdup(hint.res_class);
            XFree(hint.res_class);
        }
        if (hint.res_name) {
            XFree(hint.res_name);
        }
    }

    return class_name;
}

int get_window_state(Display *display, Window window) {
    /* First check if window exists */
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(display, window, &attrs)) {
        return STATE_NOT_RUNNING;  /* Window doesn't exist */
    }

    Atom net_wm_state = XInternAtom(display, "_NET_WM_STATE", False);
    Atom net_wm_state_hidden = XInternAtom(display, "_NET_WM_STATE_HIDDEN", False);

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    int status = XGetWindowProperty(display, window, net_wm_state,
                                    0, ~0L, False, XA_ATOM,
                                    &actual_type, &actual_format,
                                    &nitems, &bytes_after, &data);

    if (status != Success || data == NULL) {
        return STATE_VISIBLE;
    }

    Atom *states = (Atom *)data;
    int is_hidden = 0;

    for (unsigned long i = 0; i < nitems; i++) {
        if (states[i] == net_wm_state_hidden) {
            is_hidden = 1;
            break;
        }
    }

    XFree(data);

    return is_hidden ? STATE_HIDDEN : STATE_VISIBLE;
}

void launch_application(const char *app_name) {
    fprintf(stderr, "Launching: %s\n", app_name);
    fflush(stderr);

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execlp(app_name, app_name, NULL);
        perror("exec failed");
        exit(1);
    } else if (pid < 0) {
        perror("fork failed");
    }
}

void minimize_window(Display *display, Window window) {
    fprintf(stderr, "Minimizing window: 0x%lx\n", window);
    fflush(stderr);

    XIconifyWindow(display, window, DefaultScreen(display));
    XFlush(display);
}

void activate_window(Display *display, Window window) {
    fprintf(stderr, "Activating window: 0x%lx\n", window);
    fflush(stderr);

    Window root = DefaultRootWindow(display);
    Atom net_active_window = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);

    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.serial = 0;
    event.xclient.send_event = True;
    event.xclient.window = window;
    event.xclient.message_type = net_active_window;
    event.xclient.format = 32;
    event.xclient.data.l[0] = 2;
    event.xclient.data.l[1] = 0;
    event.xclient.data.l[2] = 0;

    XSendEvent(display, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask,
               &event);
    XSync(display, False);
}

WindowState read_state_file() {
    FILE *fp = fopen("/tmp/window-toggle-state", "r");
    if (!fp) return STATE_NOT_RUNNING;

    int state = STATE_NOT_RUNNING;
    if (fscanf(fp, "%d", &state) != 1) {
        fclose(fp);
        return STATE_NOT_RUNNING;
    }
    fclose(fp);

    return (WindowState)state;
}

void write_state_file(WindowState state) {
    FILE *fp = fopen("/tmp/window-toggle-state", "w");
    if (!fp) return;

    fprintf(fp, "%d", state);
    fclose(fp);
}

Window read_active_window() {
    FILE *fp = fopen("/tmp/window-toggle-active", "r");
    if (!fp) return 0;

    Window window_id = 0;
    if (fscanf(fp, "%lu", &window_id) != 1) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    return window_id;
}

void write_active_window(Window window) {
    FILE *fp = fopen("/tmp/window-toggle-active", "w");
    if (!fp) return;

    fprintf(fp, "%lu", window);
    fclose(fp);
}
