#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <X11/Xlib.h>

typedef enum {
    STATE_NOT_RUNNING,
    STATE_HIDDEN,
    STATE_VISIBLE
} WindowState;

/* Window scanning */
int scan_all_windows(Window **windows);
void free_window_list(Window *windows);
char* get_window_title(Display *display, Window window);
char* get_window_class(Display *display, Window window);
int get_window_state(Display *display, Window window);

/* Window operations */
void launch_application(const char *app_name);
void minimize_window(Display *display, Window window);
void activate_window(Display *display, Window window);
void raise_window(Display *display, Window window);
int is_window_on_top(Display *display, Window window);

/* State management */
WindowState read_state_file();
void write_state_file(WindowState state);
Window read_active_window();
void write_active_window(Window window);

#endif
