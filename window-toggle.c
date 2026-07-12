/*
 * Window Toggle - Main program
 * Supports configure mode and run mode
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#include <X11/keysymdef.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include "config.h"
#include "window-manager.h"
#include "daemon.h"
#include "ipc.h"
#include "app-binding.h"
/* Forward declaration */
void free_config(Config *config);
static volatile sig_atomic_t grab_timeout = 0;
void configure_mode_with_path(const char *config_path);
void run_mode_with_path(const char *config_path, const char *key_param);
void clean_mode_with_path(const char *config_path);
void start_mode_with_path(const char *config_path);
void show_config(const char *config_path);

static int _show_fx_num(const char *k) {
    if (!k || k[0] != 'F') return 99;
    int n = 0;
    for (const char *p = k + 1; *p; p++) {
        if (*p < '0' || *p > '9') return 99;
        n = n * 10 + (*p - '0');
    }
    return n;
}

static int _show_binding_cmp(const void *a, const void *b) {
    const AppBinding *A = a, *B = b;
    /* 按 F 键数字升序主排序 (F8 在 F12 前面)。同 Fx 按 cmd 字典序稳定 fallback. */
    int ka = _show_fx_num(A->key), kb = _show_fx_num(B->key);
    if (ka != kb) return ka - kb;
    int r = strcmp(A->cmd ? A->cmd : "", B->cmd ? B->cmd : "");
    if (r != 0) return r;
    return 0;
}

void bind_app_mode_with_path(const char *config_path, const char *key, const char *cmd, const char *wm_class);
void unbind_app_mode_with_path(const char *config_path, const char *key);
void show_app_mode_with_path(const char *config_path);
void run_app_mode_with_path(const char *config_path, const char *key);
int find_next_slot_id(const char *config_path);

/* Get the executable path dynamically */
static int silent_xerror_handler(Display *dpy, XErrorEvent *e) { (void)dpy; (void)e; return 0; }

static void get_exec_path(char *buf, size_t bufsize) {
    ssize_t len = readlink("/proc/self/exe", buf, bufsize - 1);
    if (len != -1) {
        buf[len] = '\0';
    } else {
        /* Fallback: use argv[0] */
        snprintf(buf, bufsize, "window-toggle");
    }
}

/* List of window classes to hide from configuration */
static const char *hidden_classes[] = {
    "Gjs",  /* Desktop Icons */
    NULL
};
/* Color codes for output */
#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_GRAY    "\033[90m"
#define COLOR_LIGHT_RED    "\033[91m"
#define COLOR_LIGHT_GREEN  "\033[92m"
#define COLOR_LIGHT_YELLOW "\033[93m"
#define COLOR_LIGHT_BLUE   "\033[94m"
#define COLOR_LIGHT_MAGENTA "\033[95m"
#define COLOR_LIGHT_CYAN   "\033[96m"
void grab_alarm_handler(int sig) {
    grab_timeout = 1;
}
void configure_mode() {
    configure_mode_with_path(CONFIG_PATH);
}
void configure_mode_with_path(const char *config_path) {
    fprintf(stderr, COLOR_BOLD COLOR_CYAN "=== Window Toggle Configuration ===" COLOR_RESET "\n");
    /* Check if config file exists */
    int is_first_config = !config_exists(config_path);
    if (is_first_config) {
        fprintf(stderr, COLOR_GREEN "Creating new configuration..." COLOR_RESET "\n");
    } else {
        fprintf(stderr, COLOR_GREEN "Adding new shortcut to existing configuration..." COLOR_RESET "\n");
    }
    /* Step 1: Configure hotkey */
    fprintf(stderr, "\n" COLOR_BOLD COLOR_YELLOW "Step 1: Press your desired hotkey combination" COLOR_RESET "\n");
    fprintf(stderr, "Recommended: Press " COLOR_BOLD "Ctrl+Alt+F12" COLOR_RESET " or " COLOR_BOLD "Super+F2" COLOR_RESET "\n");
    fprintf(stderr, "Also supported: " COLOR_BOLD "F1-F12" COLOR_RESET " alone (no modifier required)\n");
    fprintf(stderr, "Or press any key with Ctrl+Alt or Super modifier...\n");
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, COLOR_RED "Failed to open X display" COLOR_RESET "\n");
        return;
    }
    /* Initialize XKB */
    int xkb_event_base, xkb_error_base;
    if (!XkbQueryExtension(display, NULL, &xkb_event_base, &xkb_error_base, NULL, NULL)) {
        fprintf(stderr, COLOR_RED "XKB extension not available" COLOR_RESET "\n");
        XCloseDisplay(display);
        return;
    }
    fprintf(stderr, "Waiting for hotkey... (Press Ctrl+Alt+F12 or your combination)\n");
    fprintf(stderr, "Press ESC to cancel\n");
    fprintf(stderr, "Auto-timeout in 10 seconds if no input\n");
    fflush(stderr);
    /* Set up signal handler for grab timeout */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = grab_alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);
    /* Try to grab keyboard with timeout protection */
    Window root = DefaultRootWindow(display);
    int grab_status;
    int grab_succeeded = 0;
    fprintf(stderr, "Attempting to grab keyboard (will timeout in 2 seconds if frozen)...\n");
    fflush(stderr);
    /* Set alarm for 2 seconds */
    alarm(2);
    grab_status = XGrabKeyboard(display, root, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    alarm(0);  /* Cancel alarm */
    if (grab_status == GrabSuccess) {
        fprintf(stderr, "Keyboard grabbed successfully! Press your hotkey now...\n");
        fflush(stderr);
        grab_succeeded = 1;
    } else {
        fprintf(stderr, "Could not grab keyboard (status=%d). Using fallback method.\n", grab_status);
        fprintf(stderr, "Falling back to root window event listening.\n");
        fflush(stderr);
        XSelectInput(display, root, KeyPressMask | KeyReleaseMask);
    }
    char selected_key[64] = "F12";
    int have_hotkey = 0;
    int detected_modifiers = 0;  /* 0 = none, 1 = Ctrl+Alt, 2 = Super */
    int timeout_counter = 0;
    const int MAX_TIMEOUT = 1000;  /* 10 seconds at 100ms intervals */
    while (!have_hotkey && timeout_counter < MAX_TIMEOUT) {
        XEvent event;
        /* Use a timeout to avoid infinite blocking */
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  /* 100ms */
        int fd = ConnectionNumber(display);
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        int ret = select(fd + 1, &fds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(fd, &fds)) {
            XNextEvent(display, &event);
        } else {
            /* Timeout occurred */
            timeout_counter++;
            continue;
        }
        if (event.type != KeyPress) {
            continue;
        }
        if (event.type == KeyPress) {
            /* Get the keycode */
            KeyCode keycode = event.xkey.keycode;
            /* Try both XKB and standard X11 keysym conversion */
            KeySym keysym = XKeycodeToKeysym(display, keycode, 0);
            if (keysym == NoSymbol) {
                keysym = XkbKeycodeToKeysym(display, keycode, 0, 0);
            }
            /* Check for ESC key (using hex value) */
            if (keysym == 0xff1b) {  /* XK_Escape = 0xff1b */
                fprintf(stderr, "Configuration cancelled\n");
                if (grab_succeeded) XUngrabKeyboard(display, CurrentTime);
                XCloseDisplay(display);
                return;
            }
            /* Check for Fx keys first (works with or without modifiers) */
            int is_f_key = (keysym >= 0xffbe && keysym <= 0xffc9);
            if (is_f_key) {
                int f_num = (int)(keysym - 0xffbe + 1);
                snprintf(selected_key, sizeof(selected_key), "F%d", f_num);

                /* Determine which modifiers were used.
                 * 0=none, 1=Ctrl+Alt, 2=Super, 3=Ctrl, 4=Ctrl+Shift */
                int has_ctrl = (event.xkey.state & ControlMask);
                int has_alt  = (event.xkey.state & Mod1Mask);
                int has_super= (event.xkey.state & Mod4Mask);
                int has_shift= (event.xkey.state & ShiftMask);
                int is_ctrl_alt   = has_ctrl && has_alt;
                int is_ctrl_shift = has_ctrl && has_shift && !has_alt && !has_super;
                int is_ctrl_only  = has_ctrl && !has_alt && !has_super && !has_shift;
                int is_super      = has_super;
                char modifier_str[64] = "";

                if (is_ctrl_alt) {
                    strcpy(modifier_str, "Ctrl+Alt");
                    detected_modifiers = 1;
                } else if (is_ctrl_shift) {
                    strcpy(modifier_str, "Ctrl+Shift");
                    detected_modifiers = 4;
                } else if (is_ctrl_only) {
                    strcpy(modifier_str, "Ctrl");
                    detected_modifiers = 3;
                } else if (is_super) {
                    strcpy(modifier_str, "Super");
                    detected_modifiers = 2;
                } else {
                    /* No modifiers - Fx key alone */
                    strcpy(modifier_str, "(none)");
                    detected_modifiers = 0;
                }
                fprintf(stderr, "\nHotkey detected: %s+%s\n", modifier_str, selected_key);
                have_hotkey = 1;
                /* CRITICAL: Release keyboard grab before window selection */
                if (grab_succeeded) {
                    fprintf(stderr, "Releasing keyboard grab for window selection...\n");
                    fflush(stderr);
                    XUngrabKeyboard(display, CurrentTime);
                    grab_succeeded = 0;
                }
            }
            /* Check for other keys only with modifiers (Tab, Return, Space, 0-9, A-Z) */
            else if (event.xkey.state & (ControlMask | Mod1Mask | Mod4Mask)) {
                /* Determine which modifiers were used */
                char modifier_str[64] = "";
                int has_ctrl = (event.xkey.state & ControlMask);
                int has_alt  = (event.xkey.state & Mod1Mask);
                int has_super= (event.xkey.state & Mod4Mask);
                int has_shift= (event.xkey.state & ShiftMask);
                int is_ctrl_alt   = has_ctrl && has_alt;
                int is_ctrl_shift = has_ctrl && has_shift && !has_alt && !has_super;
                int is_ctrl_only  = has_ctrl && !has_alt && !has_super && !has_shift;
                if (is_ctrl_alt) {
                    strcpy(modifier_str, "Ctrl+Alt");
                    detected_modifiers = 1;
                } else if (is_ctrl_shift) {
                    strcpy(modifier_str, "Ctrl+Shift");
                    detected_modifiers = 4;
                } else if (is_ctrl_only) {
                    strcpy(modifier_str, "Ctrl");
                    detected_modifiers = 3;
                } else if (has_super) {
                    strcpy(modifier_str, "Super");
                    detected_modifiers = 2;
                }
                /* Determine the key based on keysym value */
                strcpy(selected_key, "Unknown");
                if (keysym == 0xff09) {  /* Tab */
                    strcpy(selected_key, "Tab");
                }
                else if (keysym == 0xff0d) {  /* Return */
                    strcpy(selected_key, "Return");
                }
                else if (keysym == 0xff20) {  /* space */
                    strcpy(selected_key, "Space");
                }
                else if (keysym >= 0x30 && keysym <= 0x39) {  /* 0-9 */
                    snprintf(selected_key, sizeof(selected_key), "%c", (char)keysym);
                }
                else if (keysym >= 0x41 && keysym <= 0x5a) {  /* A-Z */
                    snprintf(selected_key, sizeof(selected_key), "%c", (char)(keysym + 32));
                }
                else {
                    /* Unknown key with modifier, skip */
                    continue;
                }
                fprintf(stderr, "\nHotkey detected: %s+%s\n", modifier_str, selected_key);
                have_hotkey = 1;
                /* CRITICAL: Release keyboard grab before window selection */
                if (grab_succeeded) {
                    fprintf(stderr, "Releasing keyboard grab for window selection...\n");
                    fflush(stderr);
                    XUngrabKeyboard(display, CurrentTime);
                    grab_succeeded = 0;
                }
            }
        } else if (event.type == ClientMessage && event.xclient.message_type == XInternAtom(display, "WM_PROTOCOLS", True)) {
            /* Window close event - cancel configuration */
            fprintf(stderr, "Configuration cancelled (window closed)\n");
            if (grab_succeeded) XUngrabKeyboard(display, CurrentTime);
            XCloseDisplay(display);
            return;
        }
    }
    /* Check if we timed out */
    if (timeout_counter >= MAX_TIMEOUT) {
        fprintf(stderr, "Configuration timed out - using default hotkey (Ctrl+Alt+F12)\n");
        strcpy(selected_key, "F12");
        detected_modifiers = 1;  /* Ctrl+Alt */
        if (grab_succeeded) XUngrabKeyboard(display, CurrentTime);
    }
    /* Step 2: Scan and display windows */
    fprintf(stderr, "\n" COLOR_BOLD COLOR_YELLOW "Step 2: Scanning all windows..." COLOR_RESET "\n");
    Window *windows = NULL;
    int count = scan_all_windows(&windows);
    if (count == 0) {
        fprintf(stderr, "No windows found\n");
        if (grab_succeeded) XUngrabKeyboard(display, CurrentTime);
        XCloseDisplay(display);
        return;
    }
    fprintf(stderr, "Found %d windows:\n", count);
    /* Collect all window info first */
    typedef struct {
        int index;
        char *title;
        char *class;
        Window window;
    } WindowInfo;
    WindowInfo *window_info = malloc(count * sizeof(WindowInfo));
    for (int i = 0; i < count; i++) {
        window_info[i].index = i + 1;
        window_info[i].title = get_window_title(display, windows[i]);
        window_info[i].class = get_window_class(display, windows[i]);
        window_info[i].window = windows[i];
    }
    /* Group windows by application (window_class) */
    typedef struct {
        char *class_name;
        int window_count;
        WindowInfo **windows;
    } AppGroup;
    /* First, count unique classes */
    int max_apps = count;
    AppGroup *app_groups = malloc(max_apps * sizeof(AppGroup));
    int app_count = 0;
    for (int i = 0; i < count; i++) {
        /* Skip hidden window classes */
        int should_skip = 0;
        for (int h = 0; hidden_classes[h] != NULL; h++) {
            if (strstr(window_info[i].class, hidden_classes[h])) {
                should_skip = 1;
                break;
            }
        }
        if (should_skip) continue;
        /* Find if class already exists (exact match only) */
        int found = -1;
        for (int j = 0; j < app_count; j++) {
            if (strcmp(app_groups[j].class_name, window_info[i].class) == 0) {
                found = j;
                break;
            }
        }
        if (found == -1) {
            /* New app class */
            app_groups[app_count].class_name = strdup(window_info[i].class);
            app_groups[app_count].window_count = 0;
            app_groups[app_count].windows = malloc(count * sizeof(WindowInfo*));
            app_groups[app_count].windows[0] = &window_info[i];
            app_groups[app_count].window_count = 1;
            app_count++;
        } else {
            /* Existing app class, add window to it */
            app_groups[found].windows[app_groups[found].window_count] = &window_info[i];
            app_groups[found].window_count++;
        }
    }
    /* Display grouped windows */
    int window_counter = 1;
    for (int app = 0; app < app_count; app++) {
        /* Choose color based on app class */
        const char *class_name = app_groups[app].class_name;
        const char *color;
        if (strstr(class_name, "Nautilus")) {
            color = COLOR_BLUE;
        } else if (strstr(class_name, "chrome") || strstr(class_name, "firefox") || strstr(class_name, "edge")) {
            color = COLOR_CYAN;
        } else if (strstr(class_name, "Code") || strstr(class_name, "editor")) {
            color = COLOR_MAGENTA;
        } else if (strstr(class_name, "terminal") || strstr(class_name, "Terminator")) {
            color = COLOR_GREEN;
        } else if (strstr(class_name, "wechat") || strstr(class_name, "qq") || strstr(class_name, "weixin")) {
            color = COLOR_LIGHT_GREEN;
        } else {
            color = COLOR_YELLOW;
        }
        fprintf(stderr, COLOR_BOLD "%s" COLOR_RESET " (%d window%s):\n",
                class_name,
                app_groups[app].window_count,
                app_groups[app].window_count == 1 ? "" : "s");
        for (int i = 0; i < app_groups[app].window_count; i++) {
            fprintf(stderr, "  " COLOR_BOLD "[" COLOR_LIGHT_BLUE "%d" COLOR_BOLD "]" COLOR_RESET " %s%s%s\n",
                    window_counter,
                    color,
                    app_groups[app].windows[i]->title,
                    COLOR_RESET);
            app_groups[app].windows[i]->index = window_counter;
            window_counter++;
        }
    }
    /* Free temporary grouping structures */
    for (int app = 0; app < app_count; app++) {
        free(app_groups[app].class_name);
        free(app_groups[app].windows);
    }
    free(app_groups);
    /* Step 3: User selection */
    fprintf(stderr, "\n" COLOR_BOLD COLOR_YELLOW "Step 3: Select target window" COLOR_RESET "\n");
    fprintf(stderr, "Enter window number " COLOR_BOLD "(1-%d)" COLOR_RESET ": ", count);
    fflush(stdout);
    int choice;
    if (scanf("%d", &choice) != 1 || choice < 1 || choice > count) {
        fprintf(stderr, "Invalid selection\n");
        free_window_list(windows);
        if (grab_succeeded) XUngrabKeyboard(display, CurrentTime);
        XCloseDisplay(display);
        return;
    }
    /* Find window by displayed index */
    Window selected = 0;
    char *title = NULL;
    char *class = NULL;
    for (int i = 0; i < count; i++) {
        /* Skip hidden window classes during selection too */
        int should_skip = 0;
        for (int h = 0; hidden_classes[h] != NULL; h++) {
            if (strstr(window_info[i].class, hidden_classes[h])) {
                should_skip = 1;
                break;
            }
        }
        if (should_skip) continue;
        if (window_info[i].index == choice) {
            selected = window_info[i].window;
            title = window_info[i].title;
            class = window_info[i].class;
            break;
        }
    }
    if (selected == 0) {
        fprintf(stderr, COLOR_RED "Error: Could not find selected window" COLOR_RESET "\n");
        free_window_list(windows);
        if (grab_succeeded) XUngrabKeyboard(display, CurrentTime);
        XCloseDisplay(display);
        return;
    }
    fprintf(stderr, "\n" COLOR_BOLD COLOR_GREEN "Selected window:" COLOR_RESET "\n");
    fprintf(stderr, "  " COLOR_YELLOW "Title:" COLOR_RESET " %s\n", title);
    fprintf(stderr, "  " COLOR_YELLOW "Class:" COLOR_RESET " %s\n", class);
    fprintf(stderr, "  " COLOR_YELLOW "ID:" COLOR_RESET " " COLOR_CYAN "0x%lx" COLOR_RESET "\n", selected);
    fprintf(stderr, "\n");
    /* Step 4: Save configuration */
    Config config;
    memset(&config, 0, sizeof(Config));
    /* Use detected modifiers */
    if (detected_modifiers == 1) {  /* Ctrl+Alt */
        config.modifiers[0] = strdup("Ctrl");
        config.modifiers[1] = strdup("Alt");
        config.modifiers[2] = NULL;
    } else if (detected_modifiers == 2) {  /* Super */
        config.modifiers[0] = strdup("Super");
        config.modifiers[1] = NULL;
    } else if (detected_modifiers == 3) {  /* Ctrl only */
        config.modifiers[0] = strdup("Ctrl");
        config.modifiers[1] = NULL;
    } else if (detected_modifiers == 4) {  /* Ctrl+Shift */
        config.modifiers[0] = strdup("Ctrl");
        config.modifiers[1] = strdup("Shift");
        config.modifiers[2] = NULL;
    }
    config.key = strdup(selected_key);
    config.target_window = selected;
    config.window_title = title;
    config.window_class = class;
    /* Build the shortcut string (e.g., "Super+F2", "Ctrl+F1") */
    char shortcut_str[128];
    if (detected_modifiers == 1) {  /* Ctrl+Alt */
        snprintf(shortcut_str, sizeof(shortcut_str), "Ctrl+Alt+%s", selected_key);
    } else if (detected_modifiers == 2) {  /* Super */
        snprintf(shortcut_str, sizeof(shortcut_str), "Super+%s", selected_key);
    } else if (detected_modifiers == 3) {  /* Ctrl only */
        snprintf(shortcut_str, sizeof(shortcut_str), "Ctrl+%s", selected_key);
    } else if (detected_modifiers == 4) {  /* Ctrl+Shift */
        snprintf(shortcut_str, sizeof(shortcut_str), "Ctrl+Shift+%s", selected_key);
    } else {
        snprintf(shortcut_str, sizeof(shortcut_str), "%s", selected_key);
    }
    /* Save the shortcut mapping (append mode) */
    save_shortcut_mapping(config_path, shortcut_str, selected, title, class, -1);  /* -1 means no slot_id set yet */
    fprintf(stderr, "Configuration saved to: %s\n", config_path);
    /* Step 5: Show system shortcut configuration */
    fprintf(stderr, "\n" COLOR_BOLD COLOR_CYAN "=== Configuration Complete ===" COLOR_RESET "\n");
    fprintf(stderr, COLOR_GREEN "Shortcut added:" COLOR_RESET " " COLOR_BOLD COLOR_MAGENTA "%s" COLOR_RESET " -> %s\n", shortcut_str, title);
    fprintf(stderr, COLOR_GREEN "Total shortcuts:" COLOR_RESET " Check " COLOR_CYAN "%s" COLOR_RESET "\n", config_path);
    fprintf(stderr, "\n" COLOR_YELLOW "Setting up system shortcut automatically..." COLOR_RESET "\n");
    /* Generate automatic configuration */
    char shortcut_key[64];
    if (detected_modifiers == 1) {
        snprintf(shortcut_key, sizeof(shortcut_key), "<Control><Alt>%s", selected_key);
    } else if (detected_modifiers == 2) {
        snprintf(shortcut_key, sizeof(shortcut_key), "<Super>%s", selected_key);
    } else if (detected_modifiers == 3) {
        snprintf(shortcut_key, sizeof(shortcut_key), "<Control>%s", selected_key);
    } else if (detected_modifiers == 4) {
        snprintf(shortcut_key, sizeof(shortcut_key), "<Control><Shift>%s", selected_key);
    } else {
        /* Fx key alone - use just the key name */
        snprintf(shortcut_key, sizeof(shortcut_key), "%s", selected_key);
    }
    /* Find the next slot ID - always allocate new slot */
    int next_id = find_next_slot_id(config_path);
    fprintf(stderr, COLOR_YELLOW "Using shortcut slot:" COLOR_RESET " custom%d\n", next_id);
    /* Now set up the new shortcut */
    char command[4096];

    /* STEP 1: Add to custom-keybindings list FIRST (required before setting other properties) */
    fprintf(stderr, COLOR_YELLOW "=== Step 1: Adding slot to custom-keybindings list ===" COLOR_RESET "\n");
    /* Get the current list of custom keybindings */
    FILE *fp = popen("gsettings get org.gnome.settings-daemon.plugins.media-keys custom-keybindings 2>/dev/null", "r");
    if (!fp) {
        fprintf(stderr, COLOR_RED "  Failed to query existing shortcuts\n" COLOR_RESET);
    } else {
        char buffer[8192];
        if (fgets(buffer, sizeof(buffer), fp) == NULL) {
            fprintf(stderr, COLOR_RED "  Could not read shortcuts\n" COLOR_RESET);
            pclose(fp);
        } else {
            pclose(fp);
            /* Build the new custom keybinding path */
            char custom_path[256];
            snprintf(custom_path, sizeof(custom_path), "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/", next_id);

            /* Handle @as [] (empty array) case */
            char new_list[8192];
            if (strncmp(buffer, "@as []", 6) == 0) {
                snprintf(new_list, sizeof(new_list), "['%s']", custom_path);
            } else {
                char *bracket = strrchr(buffer, ']');
                if (bracket) {
                    *bracket = '\0';
                    snprintf(new_list, sizeof(new_list), "%s, '%s']", buffer, custom_path);
                } else {
                    snprintf(new_list, sizeof(new_list), "%s, '%s']", buffer, custom_path);
                }
            }

            /* Use dconf to update the list */
            snprintf(command, sizeof(command),
                     "dconf write /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings \"%s\"",
                     new_list);
            fprintf(stderr, COLOR_YELLOW "Executing:" COLOR_RESET " %s\n", command);
            int list_result = system(command);
            if (list_result == 0) {
                fprintf(stderr, COLOR_GREEN "✓ Added to keybindings list successfully" COLOR_RESET "\n");
            } else {
                fprintf(stderr, COLOR_RED "✗ Failed to add to keybindings list (exit code: %d)" COLOR_RESET "\n", list_result);
            }
        }
    }

    /* Small delay to ensure the schema is created */
    usleep(100000);

    /* STEP 2: Set the binding - use dconf write with GVariant format */
    fprintf(stderr, COLOR_YELLOW "=== Step 2: Setting up binding ===" COLOR_RESET "\n");
    snprintf(command, sizeof(command),
             "dconf write /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/binding \"%s%s%s\"",
             next_id, "'", shortcut_key, "'");
    fprintf(stderr, COLOR_YELLOW "Executing:" COLOR_RESET " %s\n", command);
    if (system(command) == 0) {
        fprintf(stderr, COLOR_GREEN "✓ Binding set successfully" COLOR_RESET "\n");
    } else {
        fprintf(stderr, COLOR_RED "✗ Failed to set binding\n" COLOR_RESET);
    }

    /* STEP 3: Set the command - use dconf write with GVariant format */
    /* Get executable path dynamically */
    char exec_path[4096];
    get_exec_path(exec_path, sizeof(exec_path));
    fprintf(stderr, COLOR_YELLOW "=== Step 3: Setting up command ===" COLOR_RESET "\n");
    fprintf(stderr, "Using executable: %s\n", exec_path);
    snprintf(command, sizeof(command),
             "dconf write /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/command \"%s%s --run --key %s%s\"",
             next_id, "'", exec_path, shortcut_str, "'");
    fprintf(stderr, COLOR_YELLOW "Executing:" COLOR_RESET " %s\n", command);
    if (system(command) == 0) {
        fprintf(stderr, COLOR_GREEN "✓ Command set successfully" COLOR_RESET "\n");
    } else {
        fprintf(stderr, COLOR_RED "✗ Failed to set command\n" COLOR_RESET);
    }

    /* STEP 4: Set the name - use dconf write with GVariant format */
    fprintf(stderr, COLOR_YELLOW "=== Step 4: Setting up name ===" COLOR_RESET "\n");
    snprintf(command, sizeof(command),
             "dconf write /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/name \"%s%s%s\"",
             next_id, "'", "window-toggle", "'");
    fprintf(stderr, COLOR_YELLOW "Executing:" COLOR_RESET " %s\n", command);
    if (system(command) == 0) {
        fprintf(stderr, COLOR_GREEN "✓ Name set successfully" COLOR_RESET "\n");
    } else {
        fprintf(stderr, COLOR_RED "✗ Failed to set name\n" COLOR_RESET);
    }
    fprintf(stderr, COLOR_BOLD COLOR_GREEN "✓ Shortcut configured automatically!" COLOR_RESET " Press " COLOR_BOLD COLOR_YELLOW "%s" COLOR_RESET " to test.\n",
            shortcut_str);
    /* Update configuration file with slot_id (append mode) */
    save_shortcut_mapping(config_path, shortcut_str, selected, title, class, next_id);
    fprintf(stderr, COLOR_CYAN "Configuration updated with slot ID:" COLOR_RESET " %d\n", next_id);
configuration_complete:
    /* Cleanup */
    if (config.modifiers[0]) free(config.modifiers[0]);
    if (config.modifiers[1]) free(config.modifiers[1]);
    if (config.key) free(config.key);
    /* Free per-window title/class strdup's before freeing the array itself.
     * Each get_window_title / get_window_class returns a fresh strdup, so
     * we can safely free them here. The selected window's pointers (title
     * and class) are referenced by config.window_title / config.window_class
     * but config doesn't own them, so we must free them via the array. */
    if (window_info) {
        for (int i = 0; i < count; i++) {
            free(window_info[i].title);
            free(window_info[i].class);
        }
    }
    free(window_info);
    free_window_list(windows);
    if (grab_succeeded) XUngrabKeyboard(display, CurrentTime);
    XCloseDisplay(display);
}
void run_mode() {
    run_mode_with_path(CONFIG_PATH, NULL);
}
void run_mode_with_path(const char *config_path, const char *key_param) {
    fprintf(stderr, "Window Toggle: Running...\n");
    Config *config = NULL;
    Window target_window = 0;
    /* If key_param is provided, find the corresponding window from mappings */
    if (key_param) {
        fprintf(stderr, "Key pressed: %s\n", key_param);
        target_window = find_window_by_key(config_path, key_param);
        if (!target_window) {
            fprintf(stderr, "No window found for key %s\n", key_param);
            fprintf(stderr, "Config path: %s\n", config_path);
            return;
        }
        fprintf(stderr, "Target window: 0x%lx\n", target_window);
    } else {
        /* Load configuration */
        config = load_config(config_path);
        if (!config) {
            fprintf(stderr, "No configuration found at %s. Run with --configure first.\n", config_path);
            return;
        }
        target_window = config->target_window;
        fprintf(stderr, "Target window: 0x%lx\n", target_window);
    }

    /* Try daemon mode first */
    if (daemon_is_running()) {
        fprintf(stderr, "Using daemon for window operation...\n");
        int32_t resp = -1;
        uint32_t resp_size = sizeof(resp);

        if (daemon_send_request(IPC_TOGGLE_WINDOW, &target_window, sizeof(target_window), &resp, &resp_size)) {
            if (resp == 0) {
                fprintf(stderr, COLOR_GREEN "Window toggled via daemon" COLOR_RESET "\n");
            } else {
                fprintf(stderr, COLOR_RED "Daemon operation failed" COLOR_RESET "\n");
            }
        } else {
            fprintf(stderr, COLOR_YELLOW "Daemon request failed, falling back to direct X" COLOR_RESET "\n");
            /* Fall through to direct X mode */
        }

        if (config) free_config(config);
        return;
    }

    /* Fallback: direct X operations (for backward compatibility) */
    fprintf(stderr, "Daemon not running, using direct X connection...\n");
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, COLOR_RED "Failed to open X display" COLOR_RESET "\n");
        if (config) free_config(config);
        return;
    }
    WindowState current_state = read_state_file();
    fprintf(stderr, "Current state: %d\n", current_state);
    /* Perform toggle action */
    int window_state = get_window_state(display, target_window);
    fprintf(stderr, "Window state: %d (0=not running, 1=hidden, 2=visible)\n", window_state);
    if (window_state == STATE_NOT_RUNNING) {
        fprintf(stderr, COLOR_RED "Error: Window no longer exists. Please reconfigure with a currently open window." COLOR_RESET "\n");
        XCloseDisplay(display);
        if (config) free_config(config);
        return;
    }
    /* 区分 "被压在底下" vs "在最前":mutter 只标 _NET_WM_STATE_HIDDEN,
     * 不告诉用户 z-order。被压的窗口 minimize 后用户看不到,所以只 raise。 */
    int was_on_top = is_window_on_top(display, target_window);
    if (window_state == STATE_HIDDEN) {
        activate_window(display, target_window);
        write_active_window(target_window);
        write_state_file(STATE_VISIBLE);
        fprintf(stderr, COLOR_GREEN "Window activated" COLOR_RESET "\n");
    } else if (was_on_top) {
        /* 可见且在最前 → minimize,toggle 的"藏起来"那半 */
        minimize_window(display, target_window);
        write_active_window(target_window);
        write_state_file(STATE_HIDDEN);
        fprintf(stderr, COLOR_GREEN "Window minimized" COLOR_RESET "\n");
    } else {
        /* 可见但被压在底下 → 只 raise,不 minimize,让用户看到窗口跳出来 */
        raise_window(display, target_window);
        fprintf(stderr, COLOR_GREEN "Window raised" COLOR_RESET "\n");
    }
    XCloseDisplay(display);
    if (config) free_config(config);
    fprintf(stderr, COLOR_GREEN "Done." COLOR_RESET "\n");
}
/* Find the next available slot ID - skip invalid slots */
int find_next_slot_id(const char *config_path) {
    int start = 0;

    /* If config exists, count shortcuts to determine starting point */
    if (config_exists(config_path)) {
        FILE *fp = fopen(config_path, "r");
        if (fp) {
            int count = 0;
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                if (line[0] != '\0') {  /* Non-empty line = shortcut */
                    count++;
                }
            }
            fclose(fp);
            /* Start from count if config exists */
            start = count;
        }
    }

    /* Find first slot not in dconf list, starting from 'start' */
    for (int i = start; i < 100; i++) {
        /* First check if slot is in custom-keybindings list (use regex to avoid matching custom51 when searching for custom5) */
        char list_check[512];
        snprintf(list_check, sizeof(list_check),
                 "gsettings get org.gnome.settings-daemon.plugins.media-keys custom-keybindings | grep -oE 'custom%d[^0-9]' | wc -l",
                 i);
        FILE *list_fp = popen(list_check, "r");
        if (list_fp) {
            char list_count[32];
            if (fgets(list_count, sizeof(list_count), list_fp)) {
                /* Remove newline from list_count */
                list_count[strcspn(list_count, "\r\n")] = 0;
                int in_list = atoi(list_count);
                pclose(list_fp);
                if (in_list > 0) {
                    continue;  /* Skip slots already in list */
                }
            } else {
                pclose(list_fp);
            }
        }
        /* Slot not in list, it's available */
        return i;
    }
    return start;
}
void start_mode_with_path(const char *config_path) {
    fprintf(stderr, COLOR_BOLD COLOR_CYAN "=== Window Toggle Daemon Mode ===" COLOR_RESET "\n");
    (void)config_path;  /* unused */

    if (daemon_is_running()) {
        fprintf(stderr, COLOR_YELLOW "Daemon is already running." COLOR_RESET "\n");
        daemon_status();
        return;
    }

    fprintf(stderr, "Starting daemon...\n");
    int ret = daemon_start();
    if (ret == 0) {
        fprintf(stderr, COLOR_GREEN "✓ Daemon started successfully" COLOR_RESET "\n");
    } else if (ret < 0) {
        fprintf(stderr, COLOR_RED "✗ Failed to start daemon" COLOR_RESET "\n");
    }
}
void show_config(const char *config_path) {
    /* App bindings (XDG) shown first. Use the merged (Ctrl(S+A)+Fx) format
     * so a single app+Fx trio prints one row instead of three. */
    {
        char xdg_path[1024];
        app_binding_xdg_path(xdg_path, sizeof(xdg_path));
        AppBinding *list = NULL; int count = 0;
        app_binding_load(xdg_path, &list, &count);
        if (count > 0) {
            fprintf(stderr, COLOR_BOLD COLOR_CYAN "=== App Bindings (%d, from %s) ===" COLOR_RESET "\n", count, xdg_path);
            Display *disp = XOpenDisplay(NULL);
            if (disp) { XSync(disp, False); XSetErrorHandler(silent_xerror_handler); }
            qsort(list, count, sizeof(AppBinding), _show_binding_cmp);
            int i = 0;
            while (i < count) {
                const char *gcmd = list[i].cmd ? list[i].cmd : "?";
                const char *gwc  = list[i].wm_class ? list[i].wm_class : "?";
                const char *gkey = list[i].key ? list[i].key : "?";
                int j = i;
                while (j < count &&
                       strcmp(list[j].cmd ? list[j].cmd : "", gcmd) == 0 &&
                       strcmp(list[j].wm_class ? list[j].wm_class : "", gwc) == 0 &&
                       strcmp(list[j].key ? list[j].key : "", gkey) == 0) j++;

                unsigned long anchor = 0;
                int has_ctrl = 0, has_super = 0, has_alt = 0, has_other = 0;
                for (int k = i; k < j; k++) {
                    const AppBinding *b = &list[k];
                    if (b->target_window != 0 && anchor == 0) anchor = b->target_window;
                    const char *m = b->modifiers ? b->modifiers : "";
                    if (strcmp(m, "Ctrl") == 0)        has_ctrl = 1;
                    else if (strcmp(m, "Super") == 0)  has_super = 1;
                    else if (strcmp(m, "Alt") == 0)    has_alt = 1;
                    else                                has_other = 1;
                }

                const char *status = "not-started";
                if (anchor != 0 && disp) {
                    XWindowAttributes attrs;
                    if (XGetWindowAttributes(disp, (Window)anchor, &attrs)) status = "alive";
                    else status = "dead";
                } else if (anchor != 0) status = "anchored";

                char shortcut[128];
                if (has_ctrl && has_super && has_alt && !has_other)
                    snprintf(shortcut, sizeof(shortcut), "Ctrl(S+A)+%s", gkey);
                else if (has_ctrl && has_super && !has_alt && !has_other)
                    snprintf(shortcut, sizeof(shortcut), "Ctrl+S+%s", gkey);
                else if (has_ctrl && has_alt && !has_super && !has_other)
                    snprintf(shortcut, sizeof(shortcut), "Ctrl+A+%s", gkey);
                else if (has_super && has_alt && !has_ctrl && !has_other)
                    snprintf(shortcut, sizeof(shortcut), "Super+Alt+%s", gkey);
                else if (has_ctrl && !has_super && !has_alt && !has_other)
                    snprintf(shortcut, sizeof(shortcut), "Ctrl+%s", gkey);
                else {
                    int pos = 0;
                    for (int k = i; k < j; k++) {
                        const char *m = list[k].modifiers ? list[k].modifiers : "";
                        if (m[0]) pos += snprintf(shortcut + pos, sizeof(shortcut) - pos, "%s%s", pos ? " / " : "", m);
                        pos += snprintf(shortcut + pos, sizeof(shortcut) - pos, "%s%s", m[0] ? "+" : "", gkey);
                    }
                }

                fprintf(stderr, "  " COLOR_BOLD "%s" COLOR_RESET "  →  %s (%s)  [anchor: 0x%lx, %s]\n",
                        shortcut, gcmd, gwc, anchor, status);
                i = j;
            }
            if (disp) XCloseDisplay(disp);
            app_binding_free(list, count);
            fprintf(stderr, "\n");
        }
    }
    fprintf(stderr, COLOR_BOLD COLOR_CYAN "=== Window Toggle Configuration ===" COLOR_RESET "\n");
    if (!config_exists(config_path)) {
        fprintf(stderr, COLOR_YELLOW "No configuration found at %s" COLOR_RESET "\n", config_path);
        fprintf(stderr, "Run with " COLOR_BOLD "--configure" COLOR_RESET " to create a new configuration.\n");
        return;
    }
    /* Read config file */
    FILE *fp = fopen(config_path, "r");
    if (!fp) {
        fprintf(stderr, COLOR_RED "Failed to open config file: %s" COLOR_RESET "\n", config_path);
        return;
    }
    fprintf(stderr, COLOR_YELLOW "Configuration file:" COLOR_RESET " %s\n", config_path);
    char line[512];
    int shortcut_count = 0;
    /* Collect all shortcuts */
    typedef struct {
        char *shortcut;
        char *window_title;
        char *window_class;
        Window window_id;
    } ShortcutInfo;
    ShortcutInfo *shortcuts = NULL;
    int max_count = 10;
    /* Allocate initial memory */
    shortcuts = malloc(max_count * sizeof(ShortcutInfo));
    /* Parse JSON format */
    char current_modifiers[64] = "";
    char current_key[32] = "";
    unsigned long current_window_id = 0;
    char current_window_title[256] = "";
    char current_window_class[128] = "";
    int in_config_block = 0;

    while (fgets(line, sizeof(line), fp)) {
        /* Remove newline */
        line[strcspn(line, "\r\n")] = 0;

        /* Skip empty lines */
        if (line[0] == '\0') {
            /* 空行结束一个 slot block。空 modifiers（裸 Fx）合法，
             * 不该被这条多余的条件过滤掉。 */
            if (current_key[0] != '\0' && current_window_id != 0) {
                /* Build shortcut string */
                char shortcut[128];
                if (strlen(current_modifiers) > 0 && strcmp(current_modifiers, "Super") != 0 && strcmp(current_modifiers, "Ctrl+Alt") != 0) {
                    snprintf(shortcut, sizeof(shortcut), "%s+%s", current_modifiers, current_key);
                } else {
                    snprintf(shortcut, sizeof(shortcut), "%s", current_key);
                }

                /* Add to shortcuts array */
                if (shortcut_count >= max_count) {
                    max_count *= 2;
                    shortcuts = realloc(shortcuts, max_count * sizeof(ShortcutInfo));
                }
                shortcuts[shortcut_count].shortcut = strdup(shortcut);
                shortcuts[shortcut_count].window_title = strdup(current_window_title);
                shortcuts[shortcut_count].window_class = strdup(current_window_class);
                shortcuts[shortcut_count].window_id = (Window)current_window_id;
                shortcut_count++;

                /* Reset for next block */
                current_modifiers[0] = '\0';
                current_key[0] = '\0';
                current_window_id = 0;
                current_window_title[0] = '\0';
                current_window_class[0] = '\0';
            }
            continue;
        }

        /* Parse JSON lines */
        if (strncmp(line, "modifiers:", 10) == 0) {
            char *p = line + 10;
            while (*p == ' ' || *p == '\t') p++;
            strncpy(current_modifiers, p, sizeof(current_modifiers) - 1);
            current_modifiers[sizeof(current_modifiers) - 1] = '\0';
        } else if (strncmp(line, "key:", 4) == 0) {
            char *p = line + 4;
            while (*p == ' ' || *p == '\t') p++;
            strncpy(current_key, p, sizeof(current_key) - 1);
            current_key[sizeof(current_key) - 1] = '\0';
        } else if (strncmp(line, "window_id:", 10) == 0) {
            char *p = line + 10;
            while (*p == ' ' || *p == '\t') p++;
            sscanf(p, "0x%lx", &current_window_id);
        } else if (strncmp(line, "window_title:", 13) == 0) {
            char *p = line + 13;
            while (*p == ' ' || *p == '\t') p++;
            strncpy(current_window_title, p, sizeof(current_window_title) - 1);
            current_window_title[sizeof(current_window_title) - 1] = '\0';
        } else if (strncmp(line, "window_class:", 13) == 0) {
            char *p = line + 13;
            while (*p == ' ' || *p == '\t') p++;
            strncpy(current_window_class, p, sizeof(current_window_class) - 1);
            current_window_class[sizeof(current_window_class) - 1] = '\0';
        }
    }
    fclose(fp);
    if (shortcut_count == 0) {
        fprintf(stderr, COLOR_YELLOW "No shortcuts found in configuration." COLOR_RESET "\n");
        return;
    }
    fprintf(stderr, COLOR_GREEN "Found %d shortcut(s):" COLOR_RESET "\n", shortcut_count);
    /* Group by application (window_class) */
    typedef struct {
        char *class_name;
        int count;
        ShortcutInfo *items;
    } AppGroup;
    /* First, count unique classes */
    int max_classes = shortcut_count;
    AppGroup *groups = malloc(max_classes * sizeof(AppGroup));
    int group_count = 0;
    for (int i = 0; i < shortcut_count; i++) {
        /* Find if class already exists (with smart matching) */
        int found = -1;
        for (int j = 0; j < group_count; j++) {
            /* Exact match */
            if (strcmp(groups[j].class_name, shortcuts[i].window_class) == 0) {
                found = j;
                break;
            }
            /* Smart match: check if one contains the other (for cases like X-terminal-emulator vs ~:X-terminal-emulator) */
            if (strstr(groups[j].class_name, shortcuts[i].window_class) ||
                strstr(shortcuts[i].window_class, groups[j].class_name)) {
                found = j;
                break;
            }
        }
        if (found == -1) {
            /* New class */
            groups[group_count].class_name = strdup(shortcuts[i].window_class);
            groups[group_count].count = 0;
            groups[group_count].items = NULL;
            groups[group_count].items = malloc(sizeof(ShortcutInfo));
            groups[group_count].items[0].shortcut = shortcuts[i].shortcut;
            groups[group_count].items[0].window_title = shortcuts[i].window_title;
            groups[group_count].items[0].window_class = shortcuts[i].window_class;
            groups[group_count].items[0].window_id = shortcuts[i].window_id;
            groups[group_count].count = 1;
            group_count++;
        } else {
            /* Existing class, add to it */
            groups[found].count++;
            groups[found].items = realloc(groups[found].items, groups[found].count * sizeof(ShortcutInfo));
            int idx = groups[found].count - 1;
            groups[found].items[idx].shortcut = shortcuts[i].shortcut;
            groups[found].items[idx].window_title = shortcuts[i].window_title;
            groups[found].items[idx].window_class = shortcuts[i].window_class;
            groups[found].items[idx].window_id = shortcuts[i].window_id;
            /* If this is a smart match, update class_name to the shorter/more general one */
            if (strcmp(groups[found].class_name, shortcuts[i].window_class) != 0) {
                /* Choose the shorter name (more general) */
                if (strlen(shortcuts[i].window_class) < strlen(groups[found].class_name)) {
                    free(groups[found].class_name);
                    groups[found].class_name = strdup(shortcuts[i].window_class);
                }
            }
        }
    }
    /* 抽出每个 shortcut 字面里的 F 数字 (F1, F12 这种), 没有 F 键的排最后。
     * 排序在 group 内做, 让 F1 排在 F12 前面而不是按文件里读到的顺序。 */
    int sort_key(const char *s) {
        if (!s) return 9999;
        const char *p = strstr(s, "F");
        if (!p || p[1] < '0' || p[1] > '9') return 9999;
        int n = 0;
        for (const char *q = p + 1; *q >= '0' && *q <= '9'; q++) {
            n = n * 10 + (*q - '0');
        }
        return n;
    }
    int cmp_shortcut(const void *a, const void *b) {
        const ShortcutInfo *A = a, *B = b;
        return sort_key(A->shortcut) - sort_key(B->shortcut);
    }
    /* Display groups */
    for (int g = 0; g < group_count; g++) {
        /* 对这个 group 内部的 item 排序 (in-place 不影响其它 group)。 */
        if (groups[g].count > 1) {
            qsort(groups[g].items, groups[g].count, sizeof(ShortcutInfo), cmp_shortcut);
        }
        fprintf(stderr, "\n" COLOR_BOLD COLOR_YELLOW "%s" COLOR_RESET " (%d window%s):\n",
                groups[g].class_name,
                groups[g].count,
                groups[g].count == 1 ? "" : "s");
        fprintf(stderr, COLOR_GRAY "%s" COLOR_RESET "\n", "────────────────────");
        for (int i = 0; i < groups[g].count; i++) {
            fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "%s" COLOR_RESET " → " COLOR_CYAN "%s" COLOR_RESET " " COLOR_GRAY "[%s]" COLOR_RESET " " COLOR_GRAY "(0x%lx)" COLOR_RESET "\n",
                    groups[g].items[i].shortcut,
                    groups[g].items[i].window_title,
                    groups[g].items[i].window_class,
                    groups[g].items[i].window_id);
        }
    }
    /* Free memory */
    for (int i = 0; i < shortcut_count; i++) {
        free(shortcuts[i].shortcut);
        free(shortcuts[i].window_title);
        free(shortcuts[i].window_class);
    }
    free(shortcuts);
    for (int g = 0; g < group_count; g++) {
        free(groups[g].class_name);
        free(groups[g].items);
    }
    free(groups);
}
void usage(const char *prog_name) {
    fprintf(stderr, COLOR_BOLD COLOR_CYAN "Usage: %s [options]" COLOR_RESET "\n", prog_name);
    fprintf(stderr, "\n" COLOR_BOLD COLOR_YELLOW "Options:" COLOR_RESET "\n");
    fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "--configure" COLOR_RESET "       Enter configuration mode\n");
    fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "--run" COLOR_RESET "             Run in toggle mode\n");
    fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "--show" COLOR_RESET "            Show slot bindings and app bindings\n");
    fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "--clean" COLOR_RESET "           Clean up all shortcuts and system keybindings\n");
    fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "--start" COLOR_RESET "           Start the daemon (persistent X connection)\n");
    fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "--stop" COLOR_RESET "            Stop the daemon\n");
    fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "--status" COLOR_RESET "           Show daemon status\n");
    fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "--key KEY" COLOR_RESET "         Specify which key was pressed (e.g., F2, F3, or plain F2 for Fx-only)\n");
    fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "--config PATH" COLOR_RESET "     Specify custom config file path\n");
    fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "--help" COLOR_RESET "            Show this help message\n");
    fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "--version" COLOR_RESET "         Show version information\n");    fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "--bind-app K C W" COLOR_RESET "   Bind an app-launch shortcut (e.g. Ctrl+Alt+F12)\n");
    fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "--unbind-app K" COLOR_RESET "      Remove an app-launch shortcut\n");
    fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "--show-app" COLOR_RESET "          List all app-launch bindings\n");
    fprintf(stderr, "  " COLOR_BOLD COLOR_MAGENTA "--run-app" COLOR_RESET "          Internal: triggered by the dconf shortcut\n");
    fprintf(stderr, "\n" COLOR_BOLD COLOR_YELLOW "Examples:" COLOR_RESET "\n");
    fprintf(stderr, "  " COLOR_CYAN "%s --configure" COLOR_RESET "\n", prog_name);
    fprintf(stderr, "  " COLOR_CYAN "%s --run --key F2" COLOR_RESET "\n", prog_name);
    fprintf(stderr, "  " COLOR_CYAN "%s --show" COLOR_RESET "\n", prog_name);
    fprintf(stderr, "  " COLOR_CYAN "%s --clean" COLOR_RESET "\n", prog_name);
    fprintf(stderr, "  " COLOR_CYAN "%s --start" COLOR_RESET "\n", prog_name);
    fprintf(stderr, "\n" COLOR_GRAY "If no option is provided, will auto-configure if needed." COLOR_RESET "\n");
}
int main(int argc, char *argv[]) {
    /* Default config path */
    const char *config_path = CONFIG_PATH;
    const char *key_param = NULL;
    const char *mode = NULL;
    /* Parse all arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                config_path = argv[i + 1];
                i++; /* Skip next argument */
            } else {
                fprintf(stderr, "Error: --config requires a file path\n");
                usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--key") == 0) {
            if (i + 1 < argc) {
                key_param = argv[i + 1];
                i++; /* Skip next argument */
            } else {
                fprintf(stderr, "Error: --key requires a key name\n");
                usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--configure") == 0) {
            mode = "configure";
        } else if (strcmp(argv[i], "--run") == 0) {
            mode = "run";
        } else if (strcmp(argv[i], "--show") == 0) {
            mode = "show";
        } else if (strcmp(argv[i], "--clean") == 0) {
            mode = "clean";
        } else if (strcmp(argv[i], "--start") == 0) {
            mode = "start";
        } else if (strcmp(argv[i], "--stop") == 0) {
            mode = "stop";
        } else if (strcmp(argv[i], "--status") == 0) {
            mode = "status";
        } else if (strcmp(argv[i], "--bind-app") == 0) {
            if (i + 3 < argc) {
                bind_app_mode_with_path(config_path, argv[i+1], argv[i+2], argv[i+3]);
                return 0;
            } else {
                fprintf(stderr, "Error: --bind-app requires <key> <cmd> <wm_class>\n");
                usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--unbind-app") == 0) {
            if (i + 1 < argc) {
                unbind_app_mode_with_path(config_path, argv[i+1]);
                return 0;
            } else {
                fprintf(stderr, "Error: --unbind-app requires <key>\n");
                usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--show-app") == 0) {
            mode = "show_app";
        } else if (strcmp(argv[i], "--run-app") == 0) {
            mode = "run_app";
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("window-toggle v1.9.6\n");
            printf("\n");
            printf("GNOME 下的窗口切换工具：为任意窗口绑定一个快捷键，按一下显示，\n");
            printf("再按一下最小化。类似 macOS 的「隐藏应用」，但针对单个窗口。\n");
            printf("\n");
            printf("v1.9.6 主要更新：\n");
            printf("  - --clean 也清掉 viewer 自动注册 (Pause / Scroll_Lock / Print)：\n");
            printf("    之前 --clean 只清 slot binding, viewer 自动注册的那几条\n");
            printf("    留着,看上去清不干净。现在 viewer 自启那几条一并清掉,\n");
            printf("    slot binding 也照旧清, dconf 数组里残留的空 slot 路径\n");
            printf("    顺手剪掉。\n");
            printf("  - --clean 不再动 --bind-app 的 app binding\n");
            printf("    --bind-app 注册的快捷键 (name = window-toggle-app) 由\n");
            printf("    --unbind-app 单独删, 不归 --clean 管。~/.config/\n");
            printf("    window-toggle/bindings.json 文件不动。\n");
            printf("\n");
            printf("v1.9.5 主要更新：\n");
            printf("  - --bind-app 一次写三条快捷键 (Ctrl/Super/Alt)：\n");
            printf("    之前 --bind-app F11 code Code 只在 dconf 里加 Ctrl+F11 一条，\n");
            printf("    想加 Super+F11 还得再跑一次，麻烦。裸 Fx 现在直接扩成\n");
            printf("    Ctrl、Super、Alt 三条，按哪个都 toggle 同一个窗口。\n");
            printf("    带修饰符（比如 Ctrl+Shift+F7）还是只注册一条，\n");
            printf("    保持和老 binding 兼容。\n");
            printf("  - --show-app 和 viewer 弹窗把同一个 app+Fx 的三条合并显示：\n");
            printf("    三件套齐全时合并写成 Ctrl(S+A)+Fx，两条时写 Ctrl+S+Fx，\n");
            printf("    一条时照旧 Ctrl+Fx。viewer 状态圆点按\n");
            printf("    已启动 > 已隐藏 > 未启动 取最有用那条，避免看上去全错配。\n");
            printf("  - 锚点共享：同 cmd+wm_class 的三条 binding 现在共享同一个 XID：\n");
            printf("    启动新窗口时另外两条的 anchor 一起覆盖到新窗口 ID，\n");
            printf("    避免老 anchor 死了之后三条互相指向不同 ID。\n");
            printf("\n");
            printf("v1.9.4 主要更新：\n");
            printf("  - 按快捷键时区分三种状态做不同动作:\n");
            printf("    之前只看「藏起来没」和「是不是 visible」两种,visible 就直接\n");
            printf("    最小化。如果窗口 visible 但被压在别的窗口底下,按下去用户\n");
            printf("    什么都看不到 —— 窗口只是从底下被藏到了最小化,视觉上没变化。\n");
            printf("    现在多查一眼窗口是不是在普通窗口最顶,如果在最顶就 minimize\n");
            printf("    (老行为),被压就只把它抢到焦点放到最前 (raise),用户看到\n");
            printf("    窗口跳出来。F1/F3/F6 和 Ctrl+F10/F11/F12 都按这个改。\n");
            printf("  - 把窗口抢到最前改用另一种方式发消息:\n");
            printf("    mutter 收到普通 app 的抢焦点请求时只给焦点不动 stacking,\n");
            printf("    收到「桌面切换器」(pager) 那种请求才同时抢焦点+提到最前。\n");
            printf("    之前用的是 app 那种,现在改用 pager 那种,所以按下去确实\n");
            printf("    能看到窗口跳出来。\n");
            printf("\n");
            printf("v1.9.3 主要更新：\n");
            printf("  - viewer 弹窗状态从两态改三态：\n");
            printf("    之前 status_for 只把窗口分成 开 / 没 两种，\n");
            printf("    按 Ctrl+Fx 把它最小化之后 viewer 还显示 在\n");
            printf("    那里，让人以为按键没生效。现在分三态：\n");
            printf("    开着（绿圆点 + 不透明）、已隐藏（灰圆点 + 40%% 透明）、\n");
            printf("    已失效（橙圆点 + 25%% 透明）。弹窗期间每秒轮询一次，\n");
            printf("    按一下 Ctrl+Fx 之后立刻看见新状态。\n");
            printf("  - --clean 顺手把 viewer 弹窗进程一起杀掉：\n");
            printf("    之前 --clean 只清配置和 dconf，viewer 还活着，\n");
            printf("    下次按 Ctrl+PB 弹出来是空的。\n");
            printf("\n");
            printf("v1.9.2 主要更新：\n");
            printf("  - 修复 --bind-app 启动后 anchor 永远写不上的 bug：\n");
            printf("    find_window_by_class 改用 strcasecmp 匹配 WM_CLASS，\n");
            printf("    避免真实窗口的 res_class（如 code）大小写与用户配置不一致\n");
            printf("    时 3 秒轮询空转后超时退出、anchor 保持为 0、\n");
            printf("    下次按键又 fork+execlp 启动新进程。\n");
            printf("  - 上述修复使 Ctrl+F11 在 VSCode 未运行时按一次即可\n");
            printf("    启动 code、~1s 内锚定到第一个 Code 窗口、\n");
            printf("    后续按键正常 toggle 该窗口，不会累积进程。\n");
            printf("\n");
            printf("v1.9.1 主要更新：\n");
            printf("  - 支持 5 种修饰符快捷键绑定到同一个 Fx 键，互不覆盖：\n");
            printf("    裸 Fx、Ctrl+Fx、Ctrl+Alt+Fx、Ctrl+Shift+Fx、Super+Fx\n");
            printf("  - 每个绑定切换一个独立窗口\n");
            printf("  - dconf 命令通过 --key 参数携带修饰符，运行时可还原\n");
            printf("  - 修复了去重状态机损坏配置文件的 bug\n");
            printf("  - 修复了行缓冲过小截断长 window_title 的问题\n");
            printf("  - 新增 --version 命令，输出版本和本次主要更新\n");
            printf("  - 新增 --bind-app / --unbind-app / --show-app / --run-app：\n");
            printf("    把「启动应用 + 隐藏/显示」绑到一个修饰符快捷键\n");
            printf("    （典型: Ctrl+F12 启动并锚定第一个 nautilus 窗口）\n");
            printf("\n");
            printf("详细说明见 README.md 和 doc/IMPLEMENT_DAEMON_MODE.md。\n");
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }
    /* Execute based on mode */
    if (mode) {
        if (strcmp(mode, "configure") == 0) {
            configure_mode_with_path(config_path);
        } else if (strcmp(mode, "run") == 0) {
            run_mode_with_path(config_path, key_param);
        } else if (strcmp(mode, "show") == 0) {
            show_config(config_path);
        } else if (strcmp(mode, "clean") == 0) {
            clean_mode_with_path(config_path);
        } else if (strcmp(mode, "start") == 0) {
            start_mode_with_path(config_path);
        } else if (strcmp(mode, "stop") == 0) {
            daemon_stop();
        } else if (strcmp(mode, "status") == 0) {
            daemon_status();
        } else if (strcmp(mode, "run_app") == 0) {
            if (!key_param) {
                fprintf(stderr, "Error: --run-app requires --key <spec>\n");
                usage(argv[0]);
                return 1;
            }
            run_app_mode_with_path(config_path, key_param);
        } else if (strcmp(mode, "show_app") == 0) {
            show_app_mode_with_path(config_path);
        }
    } else {
        /* Auto-configure if needed */
        if (!config_exists(config_path)) {
            fprintf(stderr, "No configuration found at %s. Starting configuration...\n", config_path);
            configure_mode_with_path(config_path);
        } else {
            fprintf(stderr, "Configuration found at %s. Starting toggle mode...\n", config_path);
            run_mode_with_path(config_path, key_param);
        }
    }
    return 0;
}
void clean_mode_with_path(const char *config_path) {
    fprintf(stderr, COLOR_BOLD COLOR_CYAN "=== Window Toggle Clean Mode ===" COLOR_RESET "\n");

    /* --clean 顺带关掉 viewer 弹窗进程（用 SIGTERM 让它优雅退出，
     * 否则 --clean 之后 viewer 还会读到一个"已经清掉"的配置文件，
     * 显示空弹窗或干脆死循环找文件）。 */
    {
        FILE *vfp = fopen("/tmp/window-toggle-viewer.pid", "r");
        if (vfp) {
            long vpid = 0;
            if (fscanf(vfp, "%ld", &vpid) == 1 && vpid > 0) {
                if (kill((pid_t)vpid, SIGTERM) == 0) {
                    fprintf(stderr, COLOR_GREEN "✓ Stopped viewer daemon (pid %ld)" COLOR_RESET "\n", vpid);
                }
            }
            fclose(vfp);
            unlink("/tmp/window-toggle-viewer.pid");
        }
    }

    /* Clean up window-toggle related shortcuts by name matching */
    fprintf(stderr, COLOR_YELLOW "Cleaning window-toggle shortcuts (by name matching)..." COLOR_RESET "\n");
    int cleaned_count = 0;
    int cleaned_slot = 0, cleaned_viewer = 0;

    for (int i = 0; i < 100; i++) {
        /* Read name field */
        char check_cmd[512];
        snprintf(check_cmd, sizeof(check_cmd),
                 "dconf read /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/name 2>/dev/null",
                 i);
        FILE *fp = popen(check_cmd, "r");
        if (fp) {
            char name[256];
            if (fgets(name, sizeof(name), fp)) {
                pclose(fp);
                /* Remove quotes and whitespace */
                name[strcspn(name, "\r\n")] = 0;
                char *name_stripped = name;
                while (*name_stripped == '\'' || *name_stripped == ' ' || *name_stripped == '\t') name_stripped++;
                size_t len = strlen(name_stripped);
                while (len > 0 && (name_stripped[len-1] == '\'' || name_stripped[len-1] == ' ' || name_stripped[len-1] == '\t')) {
                    name_stripped[--len] = '\0';
                }

                /* 清掉两类仍归 --clean 管的快捷键:
                 *   - "window-toggle"          slot 系 (--configure 注册)
                 *   - "window-toggle-viewer"   viewer 自动注册
                 * 注：app 系 ("window-toggle-app") 不归 --clean, 那是用户
                 * 配的 --bind-app 数据, 单独走 --unbind-app 删。--clean 不
                 * 动 --bind-app 注册的快捷键和 ~/.config/window-toggle/
                 * bindings.json. */
                const char *name_kind = NULL;
                if (strcmp(name_stripped, "window-toggle") == 0)            name_kind = "slot";
                else if (strcmp(name_stripped, "window-toggle-viewer") == 0) name_kind = "viewer";
                if (name_kind) {
                    /* Build the path to remove from list */
                    char remove_path[256];
                    snprintf(remove_path, sizeof(remove_path), "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/", i);

                    /* Delete this slot configuration */
                    char delete_cmd[512];
                    snprintf(delete_cmd, sizeof(delete_cmd),
                             "dconf reset -f /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/ 2>/dev/null",
                             i);
                    int result = system(delete_cmd);

                    /* Also remove from the custom-keybindings list using Python script file */
                    FILE *fp = fopen("/tmp/wt_rm.py", "w");
                    if (fp) {
                        fprintf(fp, "#!/usr/bin/env python3\n");
                        fprintf(fp, "import subprocess, re, sys\n");
                        fprintf(fp, "slot = %d\n", i);
                        fprintf(fp, "l = subprocess.run(['dconf', 'read', '/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings'], capture_output=True, text=True).stdout.strip()\n");
                        fprintf(fp, "if l and l != '@as []':\n");
                        fprintf(fp, "    paths = re.findall(r\"'([^']+)'\", l)\n");
                        fprintf(fp, "    remove = '/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%%d/' %% slot\n");
                        fprintf(fp, "    new_paths = [p for p in paths if p != remove]\n");
                        fprintf(fp, "    if new_paths:\n");
                        fprintf(fp, "        new_list = '[' + ', '.join(\"'\" + p + \"'\" for p in new_paths) + ']'\n");
                        fprintf(fp, "    else:\n");
                        fprintf(fp, "        new_list = '@as []'\n");
                        fprintf(fp, "    subprocess.run(['dconf', 'write', '/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings', new_list])\n");
                        fclose(fp);
                        system("python3 /tmp/wt_rm.py");
                        unlink("/tmp/wt_rm.py");
                    }

                    if (result == 0) {
                        if (!strcmp(name_kind, "slot"))   cleaned_slot++;
                        else if (!strcmp(name_kind, "viewer"))  cleaned_viewer++;
                        fprintf(stderr, COLOR_GREEN "  ✓ Cleaned custom%d [%s]: %s" COLOR_RESET "\n", i, name_kind, name_stripped);
                        cleaned_count++;
                    }
                }
            } else {
                pclose(fp);
            }
        }
    }

    if (cleaned_count == 0) {
        fprintf(stderr, COLOR_YELLOW "No window-toggle shortcuts found." COLOR_RESET "\n");
    } else {
        fprintf(stderr, COLOR_GREEN "\n✓ Cleaned %d window-toggle shortcut(s)" COLOR_RESET "\n", cleaned_count);
        fprintf(stderr, COLOR_CYAN "  slot=%d  viewer=%d" COLOR_RESET "\n",
                cleaned_slot, cleaned_viewer);
    }

    /* --clean 不动 ~/.config/window-toggle/bindings.json: 那份配置
     * 代表用户已经 --bind-app 过的 app binding (dconf 端如果还在自然
     * 由 --unbind-app 管)。配置文件保留不动, 用户随时 --bind-app 重
     * 启即可. */

    /* Preserve the app_bindings section across --clean; the slot portion is gone. */
    {
        FILE *in = fopen(config_path, "r");
        char *preserved = NULL;
        size_t preserved_len = 0;
        if (in) {
            char line[8192];
            int seen_delim = 0;
            size_t cap = 0;
            while (fgets(line, sizeof(line), in)) {
                if (!seen_delim) {
                    line[strcspn(line, "\r\n")] = '\0';
                    if (strcmp(line, "### app_bindings ###") == 0) seen_delim = 1;
                    continue;
                }
                size_t n = strlen(line);
                if (preserved_len + n + 1 > cap) {
                    cap = cap ? cap * 2 : 4096;
                    if (preserved_len + n + 1 > cap) cap = preserved_len + n + 1;
                    preserved = realloc(preserved, cap);
                }
                if (preserved_len == 0) {
                    /* Re-emit the section delimiter so the slot parser stops here. */
                    const char *delim = "### app_bindings ###\n";
                    size_t dlen = strlen(delim);
                    memcpy(preserved, delim, dlen);
                    preserved_len = dlen;
                }
                memcpy(preserved + preserved_len, line, n);
                preserved_len += n;
            }
            if (preserved) preserved[preserved_len] = '\0';
            fclose(in);
        }
        fprintf(stderr, COLOR_YELLOW "Removing configuration file (slot portion)..." COLOR_RESET "\n");
        if (unlink(config_path) == 0) {
            fprintf(stderr, COLOR_GREEN "✓ Removed: %s" COLOR_RESET "\n", config_path);
        } else {
            fprintf(stderr, COLOR_YELLOW "Note: Config file not found (may already be removed)." COLOR_RESET "\n");
        }
        if (preserved_len > 0) {
            FILE *out = fopen(config_path, "w");
            if (out) {
                fwrite(preserved, 1, preserved_len, out);
                fclose(out);
                fprintf(stderr, COLOR_GREEN "✓ Preserved app_bindings section (%zu bytes)" COLOR_RESET "\n", preserved_len);
            }
            free(preserved);
        }
    }

    /* Clean up temporary files */
    fprintf(stderr, COLOR_YELLOW "Cleaning up temporary files..." COLOR_RESET "\n");
    unlink("/tmp/window-toggle-state");
    unlink("/tmp/window-toggle-active");
    unlink("/tmp/window-toggle-viewer.pid");
    fprintf(stderr, COLOR_GREEN "✓ Clean complete!" COLOR_RESET "\n");
}

/* ===== App-binding subcommands (v1.9) ===== */

/* Find a free dconf custom-keybinding slot for an app binding. Mirrors
 * find_next_slot_id in spirit but is local to this file. */
static int find_free_dconf_slot(void) {
    for (int i = 0; i < 100; i++) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "dconf read /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/name 2>/dev/null",
                 i);
        FILE *fp = popen(cmd, "r");
        if (!fp) continue;
        char name[256];
        int has = (fgets(name, sizeof(name), fp) != NULL);
        pclose(fp);
        if (!has) return i;
    }
    return -1;
}

/* Find an existing slot already used for an app binding with the given (binding).
 * If found, returns the slot index; -1 if none. Lets us reuse slots instead of
 * piling up duplicates when --bind-app is called repeatedly. */
static int find_existing_app_slot(const char *dconf_binding) {
    for (int i = 0; i < 100; i++) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "dconf read /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/name 2>/dev/null",
                 i);
        FILE *fp = popen(cmd, "r");
        if (!fp) continue;
        char name[256];
        int has_name = (fgets(name, sizeof(name), fp) != NULL);
        pclose(fp);
        if (!has_name) continue;
        name[strcspn(name, "\r\n")] = 0;
        if (strstr(name, "window-toggle-app") == NULL) continue;

        snprintf(cmd, sizeof(cmd),
                 "dconf read /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/binding 2>/dev/null",
                 i);
        fp = popen(cmd, "r");
        if (!fp) continue;
        char got[256];
        int has_bind = (fgets(got, sizeof(got), fp) != NULL);
        pclose(fp);
        if (!has_bind) continue;
        got[strcspn(got, "\r\n")] = 0;
        if (strcmp(got, dconf_binding) == 0) return i;
    }
    return -1;
}

/* Convert "(modifiers, key)" pair back to dconf binding string and a "--key" value
 * suitable for passing to --run-app.
 *   modifiers: "Ctrl+Alt" / "Super" / "Ctrl+Shift" / "Ctrl" / ""
 *   key:       "F12" / "F1" ...
 *   - binding_out: e.g. "'<Control><Alt>F12'" (GVariant string literal)
 *   - cmdkey_out:  e.g. "Ctrl+Alt+F12"
 */
static void shortcut_pair_to_dconf(const char *modifiers, const char *key,
                                   char *binding_out, size_t bsz,
                                   char *cmdkey_out, size_t csz) {
    /* Build dconf binding. */
    char inner[128];
    inner[0] = '\0';
    if (modifiers && modifiers[0]) {
        /* Tokenize on '+' and emit <X> tags. */
        char tmp[64];
        strncpy(tmp, modifiers, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char *save = NULL;
        for (char *tok = strtok_r(tmp, "+", &save); tok; tok = strtok_r(NULL, "+", &save)) {
            if (strcmp(tok, "Ctrl") == 0)      strcat(inner, "<Control>");
            else if (strcmp(tok, "Alt") == 0) strcat(inner, "<Alt>");
            else if (strcmp(tok, "Shift") == 0)strcat(inner, "<Shift>");
            else if (strcmp(tok, "Super") == 0)strcat(inner, "<Super>");
        }
    }
    strcat(inner, key);
    snprintf(binding_out, bsz, "'%s'", inner);
    snprintf(cmdkey_out, csz, "%s%s%s",
             modifiers && modifiers[0] ? modifiers : "",
             modifiers && modifiers[0] ? "+" : "",
             key);
}

static int register_dconf_app(const char *modifiers, const char *key, const char *cmd_action) {
    char dconf_key[256], cmdkey[64];
    shortcut_pair_to_dconf(modifiers, key, dconf_key, sizeof(dconf_key), cmdkey, sizeof(cmdkey));
    int slot = find_existing_app_slot(dconf_key);
    if (slot >= 0) {
        /* Reuse the existing slot; just refresh command/name. */
        char cmd[4096];
        snprintf(cmd, sizeof(cmd),
                 "dconf write /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/command \"'%s'\"",
                 slot, cmd_action);
        system(cmd);
        snprintf(cmd, sizeof(cmd),
                 "dconf write /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/name \"'window-toggle-app'\"",
                 slot);
        system(cmd);
        return slot;
    }
    slot = find_free_dconf_slot();
    if (slot < 0) {
        fprintf(stderr, COLOR_RED "No free dconf custom-keybinding slot" COLOR_RESET "\n");
        return -1;
    }
    char custom_path[256];
    snprintf(custom_path, sizeof(custom_path),
             "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/", slot);

    /* Add to list. */
    char cmd[4096];
    FILE *fp = popen("gsettings get org.gnome.settings-daemon.plugins.media-keys custom-keybindings 2>/dev/null", "r");
    if (!fp) {
        fprintf(stderr, COLOR_RED "  Failed to query existing shortcuts\n" COLOR_RESET);
        return -1;
    }
    char buffer[8192];
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        pclose(fp);
        fprintf(stderr, COLOR_RED "  Could not read shortcuts\n" COLOR_RESET);
        return -1;
    }
    pclose(fp);

    char new_list[8192];
    if (strncmp(buffer, "@as []", 6) == 0) {
        snprintf(new_list, sizeof(new_list), "['%s']", custom_path);
    } else {
        char *bracket = strrchr(buffer, ']');
        if (bracket) { *bracket = '\0'; snprintf(new_list, sizeof(new_list), "%s, '%s']", buffer, custom_path); }
        else { snprintf(new_list, sizeof(new_list), "%s, '%s']", buffer, custom_path); }
    }
    snprintf(cmd, sizeof(cmd),
             "dconf write /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings \"%s\"",
             new_list);
    if (system(cmd) != 0) {
        fprintf(stderr, COLOR_RED "Failed to update custom-keybindings list\n" COLOR_RESET);
        return -1;
    }
    usleep(100000);

    snprintf(cmd, sizeof(cmd),
             "dconf write /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/binding \"%s\"",
             slot, dconf_key);
    system(cmd);
    snprintf(cmd, sizeof(cmd),
             "dconf write /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/command \"'%s'\"",
             slot, cmd_action);
    system(cmd);
    snprintf(cmd, sizeof(cmd),
             "dconf write /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/name \"'window-toggle-app'\"",
             slot);
    system(cmd);
    return slot;
}

static int unregister_dconf_app(const char *modifiers, const char *key) {
    char dconf_key[256], cmdkey[64];
    shortcut_pair_to_dconf(modifiers, key, dconf_key, sizeof(dconf_key), cmdkey, sizeof(cmdkey));
    for (int i = 0; i < 100; i++) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "dconf read /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/binding 2>/dev/null",
                 i);
        FILE *fp = popen(cmd, "r");
        if (!fp) continue;
        char got[256];
        if (fgets(got, sizeof(got), fp) == NULL) { pclose(fp); continue; }
        pclose(fp);
        got[strcspn(got, "\r\n")] = '\0';
        /* dconf returns 'value'; compare on the inner value */
        if (strcmp(got, dconf_key) != 0) continue;
        /* Verify name marker */
        snprintf(cmd, sizeof(cmd),
                 "dconf read /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/name 2>/dev/null",
                 i);
        fp = popen(cmd, "r");
        if (!fp) continue;
        char name[256];
        int has = (fgets(name, sizeof(name), fp) != NULL);
        pclose(fp);
        if (!has) continue;
        name[strcspn(name, "\r\n")] = '\0';
        if (strstr(name, "window-toggle-app") == NULL) continue;

        /* Reset the slot, then drop it from the list. */
        char reset_cmd[512];
        snprintf(reset_cmd, sizeof(reset_cmd),
                 "dconf reset -f /org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/ 2>/dev/null",
                 i);
        system(reset_cmd);

        char remove_script[256];
        snprintf(remove_script, sizeof(remove_script), "/tmp/wt_rm_app_%d.py", i);
        FILE *sf = fopen(remove_script, "w");
        if (sf) {
            fprintf(sf, "#!/usr/bin/env python3\n");
            fprintf(sf, "import subprocess, re\n");
            fprintf(sf, "slot = %d\n", i);
            fprintf(sf, "l = subprocess.run(['dconf', 'read', '/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings'], capture_output=True, text=True).stdout.strip()\n");
            fprintf(sf, "if l and l != '@as []':\n");
            fprintf(sf, "    paths = re.findall(r\"'([^']+)'\", l)\n");
            fprintf(sf, "    remove = '/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%%d/' %% slot\n");
            fprintf(sf, "    new_paths = [p for p in paths if p != remove]\n");
            fprintf(sf, "    new_list = '[' + ', '.join(\"'\" + p + \"'\" for p in new_paths) + ']' if new_paths else '@as []'\n");
            fprintf(sf, "    subprocess.run(['dconf', 'write', '/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings', new_list])\n");
            fclose(sf);
            char py[512];
            snprintf(py, sizeof(py), "python3 %s", remove_script);
            system(py);
            unlink(remove_script);
        }
        return 0;
    }
    return -1;
}

void bind_app_mode_with_path(const char *config_path, const char *key_arg,
                             const char *cmd_arg, const char *class_arg) {
    (void)config_path; /* app binding stores under XDG, not the slot config_path */
    char xdg_path[1024];
    app_binding_xdg_path(xdg_path, sizeof(xdg_path));
    config_path = xdg_path;
    if (!key_arg || !cmd_arg || !class_arg) {
        fprintf(stderr, COLOR_RED "Usage: --bind-app <key> <cmd> <wm_class>\n" COLOR_RESET);
        fprintf(stderr, "  e.g. --bind-app Ctrl+Alt+F12 nautilus org.gnome.Nautilus\n");
        return;
    }
    char user_modifiers[64] = "", key[32] = "";
    if (parse_shortcut(key_arg, user_modifiers, sizeof(user_modifiers), key, sizeof(key)) != 0) {
        fprintf(stderr, COLOR_RED "Invalid key spec: %s\n" COLOR_RESET, key_arg);
        return;
    }
    if (strlen(key) == 0) {
        fprintf(stderr, COLOR_RED "Empty key name in: %s\n" COLOR_RESET, key_arg);
        return;
    }

    /* 不带修饰键的 key 默认对 Ctrl/Super/Alt 三个修饰键各注册一条 binding。
     * 三条 share 同一个 anchor XID, 任意按一个都切到同一个窗口。
     * 用户明确写了修饰键 (例如 "Ctrl+Shift+F11") 按用户写的来, 只注册一条。 */
    const char *mod_list[4];
    int mod_count = 0;
    if (user_modifiers[0] == '\0') {
        mod_list[mod_count++] = "Ctrl";
        mod_list[mod_count++] = "Super";
        mod_list[mod_count++] = "Alt";
    } else {
        mod_list[mod_count++] = user_modifiers;
    }

    /* mutter 默认 grab 了一些 Alt+Fx 组合 (begin-move / begin-resize), 物理键按下去
     * key event 被 mutter 截走, gsd-media-keys 拿不到, window-toggle 不会触发。
     * 要让 Alt+F7/F8 也能用, 提前把 mutter 那条清空, 末尾再 kill gsd-media-keys 让它重抓。
     * 硬编码这张表 (mutter 默认值), 不在用户已经清过的情况下重复执行。 */
    static const struct { const char *fx; const char *wm_action; } mutter_takeovers[] = {
        { "F7", "begin-move"   },
        { "F8", "begin-resize" },
    };
    int needs_kick = 0;
    for (size_t i = 0; i < sizeof(mutter_takeovers)/sizeof(mutter_takeovers[0]); i++) {
        if (strcmp(key, mutter_takeovers[i].fx) != 0) continue;
        /* 只有 Alt 系列会被 mutter 抓. 含 Alt 的 modifier 都需要让路。 */
        int has_alt = (strstr(user_modifiers, "Alt") != NULL) || user_modifiers[0] == '\0';
        if (!has_alt) continue;
        /* 看 gsettings 当前是不是还指着 Alt+Fx 默认绑定 (用户清过的就不用再动)。 */
        char buf[256];
        snprintf(buf, sizeof(buf),
            "gsettings get org.gnome.desktop.wm.keybindings %s 2>/dev/null",
            mutter_takeovers[i].wm_action);
        FILE *gp = popen(buf, "r");
        char line[256] = {0};
        if (gp) { if (fgets(line, sizeof(line), gp)) {} ; pclose(gp); }
        int already_clear = (strstr(line, "@as []") != NULL || strstr(line, "['']") != NULL);
        if (already_clear) continue;
        /* 清空 mutter 那条, X server 把 grab 让出来。 */
        fprintf(stderr, COLOR_YELLOW "  mutter 默认 grab 了 Alt+%s (%s), 抢占中\n" COLOR_RESET,
                mutter_takeovers[i].fx, mutter_takeovers[i].wm_action);
        char set_cmd[256];
        snprintf(set_cmd, sizeof(set_cmd),
            "gsettings set org.gnome.desktop.wm.keybindings %s \"@as []\" 2>/dev/null",
            mutter_takeovers[i].wm_action);
        int rc = system(set_cmd);
        if (rc == 0) needs_kick = 1;
        else fprintf(stderr, COLOR_YELLOW "  gsettings set 失败 (rc=%d), 可能 GNOME 没装, 跳过\n" COLOR_RESET, rc);
    }

    char exec_path[4096];
    get_exec_path(exec_path, sizeof(exec_path));

    fprintf(stderr, COLOR_BOLD COLOR_CYAN "=== Binding application to %s ===" COLOR_RESET "\n", key_arg);
    fprintf(stderr, "  cmd:      %s\n", cmd_arg);
    fprintf(stderr, "  wm_class: %s\n", class_arg);

    for (int mi = 0; mi < mod_count; mi++) {
        const char *modifiers = mod_list[mi];
        char cmdkey[64];
        shortcut_pair_to_dconf(modifiers, key,
                               (char[256]){0}, 0,
                               cmdkey, sizeof(cmdkey));
        char action[8192];
        snprintf(action, sizeof(action), "%s --key %s --run-app", exec_path, cmdkey);

        fprintf(stderr, "  [%d/%d] %s+%s -> %s\n", mi + 1, mod_count, modifiers, key, action);

        if (register_dconf_app(modifiers, key, action) < 0) {
            fprintf(stderr, COLOR_RED "  Failed to register dconf shortcut\n" COLOR_RESET);
            continue;
        }
        fprintf(stderr, COLOR_GREEN "  ok dconf shortcut registered\n" COLOR_RESET);

        /* 看下 (modifiers, key) 这条 binding 是否已经存在, 存在则保留 target_window。
         * 这样同 app 二次跑 --bind-app 不会把现存 anchor 抹成 0。 */
        unsigned long anchor = 0;
        AppBinding *exist_list = NULL; int exist_count = 0;
        app_binding_load(config_path, &exist_list, &exist_count);
        const AppBinding *exist = app_binding_find(exist_list, exist_count, modifiers, key);
        if (exist && exist->target_window != 0) {
            int cmd_match = (exist->cmd && strcmp(exist->cmd, cmd_arg) == 0);
            int wc_match  = (exist->wm_class && strcmp(exist->wm_class, class_arg) == 0);
            if (cmd_match && wc_match) anchor = exist->target_window;
        }
        app_binding_free(exist_list, exist_count);

        if (app_binding_add(config_path, modifiers, key, cmd_arg, class_arg, anchor) != 0) {
            fprintf(stderr, COLOR_RED "  Failed to write app binding to config\n" COLOR_RESET);
            continue;
        }
    }
    if (needs_kick) {
        /* 让 gsd-media-keys 重抓 grab, 这样之前被 mutter 占着的 Alt+Fx 回到它手里。
         * 进程会在 GNOME session 里自动重起一个。 */
        fprintf(stderr, COLOR_YELLOW "  重启 gsd-media-keys 让它重新拉 grab...\n" COLOR_RESET);
        int rc = system("pkill -KILL gsd-media-keys 2>/dev/null");
        (void)rc;
        sleep(1);
    }
    if (mod_count > 1) {
        fprintf(stderr, COLOR_GREEN "=== App binding saved. Press Ctrl/Super/Alt + %s to toggle. ===\n" COLOR_RESET, key);
    } else {
        fprintf(stderr, COLOR_GREEN "=== App binding saved. Press %s+%s to toggle. ===\n" COLOR_RESET, user_modifiers, key);
    }
}


void unbind_app_mode_with_path(const char *config_path, const char *key_arg) {
    char xdg_path[1024];
    app_binding_xdg_path(xdg_path, sizeof(xdg_path));
    config_path = xdg_path;
    if (!key_arg) {
        fprintf(stderr, COLOR_RED "Usage: --unbind-app <key>\n" COLOR_RESET);
        return;
    }
    char user_modifiers[64] = "", key[32] = "";
    if (parse_shortcut(key_arg, user_modifiers, sizeof(user_modifiers), key, sizeof(key)) != 0) {
        fprintf(stderr, COLOR_RED "Invalid key spec: %s\n" COLOR_RESET, key_arg);
        return;
    }

    /* 不带修饰键时, 默认同时删 Ctrl/Super/Alt 三条 binding。
     * 用户明确写了修饰键, 只删那一条。 */
    const char *mod_list[4];
    int mod_count = 0;
    if (user_modifiers[0] == '\0') {
        mod_list[mod_count++] = "Ctrl";
        mod_list[mod_count++] = "Super";
        mod_list[mod_count++] = "Alt";
    } else {
        mod_list[mod_count++] = user_modifiers;
    }

    for (int mi = 0; mi < mod_count; mi++) {
        const char *modifiers = mod_list[mi];
        if (unregister_dconf_app(modifiers, key) == 0)
            fprintf(stderr, COLOR_GREEN "  [%d/%d] removed dconf shortcut for %s+%s\n" COLOR_RESET, mi + 1, mod_count, modifiers, key);
        else
            fprintf(stderr, COLOR_YELLOW "  [%d/%d] no dconf shortcut for %s+%s\n" COLOR_RESET, mi + 1, mod_count, modifiers, key);

        if (app_binding_remove(config_path, modifiers, key) == 0)
            fprintf(stderr, COLOR_GREEN "  [%d/%d] removed config entry for %s+%s\n" COLOR_RESET, mi + 1, mod_count, modifiers, key);
    }
}


void show_app_mode_with_path(const char *config_path) {
    char xdg_path[1024];
    app_binding_xdg_path(xdg_path, sizeof(xdg_path));
    config_path = xdg_path;
    AppBinding *list = NULL; int count = 0;
    app_binding_load(config_path, &list, &count);
    if (count == 0) {
        fprintf(stderr, COLOR_YELLOW "No app bindings configured.\n" COLOR_RESET);
        fprintf(stderr, "Register one with: --bind-app <key> <cmd> <wm_class>\n");
        app_binding_free(list, count);
        return;
    }

    Display *display = XOpenDisplay(NULL);
    if (display) { XSync(display, False); XSetErrorHandler(silent_xerror_handler); }

    fprintf(stderr, COLOR_BOLD COLOR_CYAN "=== App Bindings (%d) ===" COLOR_RESET "\n", count);

    /* 按 (cmd, key) 排序, 让 (cmd, key) 三条相邻, 后面的 group 算法才能合 Ctrl+Super+Alt */
    qsort(list, count, sizeof(AppBinding), _show_binding_cmp);

    int i = 0;
    while (i < count) {
        const char *gcmd = list[i].cmd ? list[i].cmd : "?";
        const char *gkey = list[i].key ? list[i].key : "?";
        int j = i;
        while (j < count &&
               strcmp(list[j].cmd ? list[j].cmd : "", gcmd) == 0 &&
               strcmp(list[j].key ? list[j].key : "", gkey) == 0) {
            j++;
        }

        unsigned long anchor = 0;
        int has_ctrl = 0, has_super = 0, has_alt = 0, has_other = 0;
        for (int k = i; k < j; k++) {
            const AppBinding *b = &list[k];
            if (b->target_window != 0 && anchor == 0) anchor = b->target_window;
            const char *m = b->modifiers ? b->modifiers : "";
            if (strcmp(m, "Ctrl") == 0)      has_ctrl = 1;
            else if (strcmp(m, "Super") == 0) has_super = 1;
            else if (strcmp(m, "Alt") == 0)  has_alt = 1;
            else                              has_other = 1;
        }

        const char *status = "not-started";
        if (anchor != 0 && display) {
            XWindowAttributes attrs;
            if (XGetWindowAttributes(display, (Window)anchor, &attrs))
                status = "alive";
            else
                status = "dead";
        } else if (anchor != 0) {
            status = "anchored";
        }

        char shortcut[128];
        if (has_ctrl && has_super && has_alt && !has_other) {
            snprintf(shortcut, sizeof(shortcut), "Ctrl(S+A)+%s", gkey);
        } else if (has_ctrl && has_super && !has_alt && !has_other) {
            snprintf(shortcut, sizeof(shortcut), "Ctrl+S+%s", gkey);
        } else if (has_ctrl && has_alt && !has_super && !has_other) {
            snprintf(shortcut, sizeof(shortcut), "Ctrl+A+%s", gkey);
        } else if (has_super && has_alt && !has_ctrl && !has_other) {
            snprintf(shortcut, sizeof(shortcut), "Super+Alt+%s", gkey);
        } else {
            /* fallback: 把每条 modifier+key 用 / 串起来 */
            char buf[256] = "";
            for (int k = i; k < j; k++) {
                char piece[64];
                const char *m = list[k].modifiers ? list[k].modifiers : "";
                snprintf(piece, sizeof(piece), "%s%s%s", m[0] ? m : "", m[0] ? "+" : "", list[k].key ? list[k].key : "?");
                if (buf[0]) snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "/");
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%s", piece);
            }
            snprintf(shortcut, sizeof(shortcut), "%s", buf);
        }

        fprintf(stderr, COLOR_BOLD "\n%s" COLOR_RESET "\n", gcmd);
        fprintf(stderr, "  " COLOR_BOLD "%s" COLOR_RESET "   [anchor: 0x%lx, %s]\n",
                shortcut, anchor, status);

        i = j;
    }
    fprintf(stderr, "\n");

    if (display) XCloseDisplay(display);
    app_binding_free(list, count);
}



/* Snapshot the current _NET_CLIENT_LIST. Returns a malloc'd array of
 * Window XIDs terminated by 0, or NULL on error. Used to tell newly
 * spawned windows apart from older ones that happen to share the same
 * WM_CLASS (e.g. several X-terminal-emulator processes on the desktop). */
static Window *snapshot_existing_windows(Display *display) {
    Window root = DefaultRootWindow(display);
    Atom net_client_list = XInternAtom(display, "_NET_CLIENT_LIST", False);
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(display, root, net_client_list, 0, ~0L, False, XA_WINDOW,
                           &actual_type, &actual_format, &nitems, &bytes_after, &data) != Success)
        return NULL;
    if (!data) return NULL;
    Window *out = malloc(sizeof(Window) * (nitems + 1));
    if (!out) { XFree(data); return NULL; }
    memcpy(out, data, sizeof(Window) * nitems);
    out[nitems] = 0;
    XFree(data);
    return out;
}

static int window_in_list(Window w, const Window *list) {
    if (!list) return 0;
    for (const Window *p = list; *p; p++) {
        if (*p == w) return 1;
    }
    return 0;
}

/* Find a window by WM_CLASS match, restricted to windows that did NOT
 * exist in `exclude` (i.e. windows spawned by this process). Returns 0
 * if none. */
static unsigned long find_new_window_by_class(Display *display, const char *wm_class, const Window *exclude) {
    Window root = DefaultRootWindow(display);
    Atom net_client_list = XInternAtom(display, "_NET_CLIENT_LIST", False);
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(display, root, net_client_list, 0, ~0L, False, XA_WINDOW,
                           &actual_type, &actual_format, &nitems, &bytes_after, &data) != Success)
        return 0;
    if (!data) return 0;
    Window *list = (Window *)data;
    unsigned long found = 0;
    for (unsigned long i = 0; i < nitems; i++) {
        if (window_in_list(list[i], exclude)) continue;
        XClassHint hint;
        if (XGetClassHint(display, list[i], &hint)) {
            int match = 0;
            if (hint.res_class && strcasecmp(hint.res_class, wm_class) == 0) match = 1;
            if (hint.res_class) XFree(hint.res_class);
            if (hint.res_name)  XFree(hint.res_name);
            if (match) { found = (unsigned long)list[i]; break; }
        }
    }
    XFree(data);
    return found;
}

/* Find a window by WM_CLASS match. Returns 0 if none. */
static unsigned long find_window_by_class(Display *display, const char *wm_class) {
    Window root = DefaultRootWindow(display);
    Atom net_client_list = XInternAtom(display, "_NET_CLIENT_LIST", False);
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(display, root, net_client_list, 0, ~0L, False, XA_WINDOW,
                           &actual_type, &actual_format, &nitems, &bytes_after, &data) != Success)
        return 0;
    if (!data) return 0;
    Window *list = (Window *)data;
    unsigned long found = 0;
    for (unsigned long i = 0; i < nitems; i++) {
        XClassHint hint;
        if (XGetClassHint(display, list[i], &hint)) {
            int match = 0;
            if (hint.res_class && strcasecmp(hint.res_class, wm_class) == 0) match = 1;
            if (hint.res_class) XFree(hint.res_class);
            if (hint.res_name)  XFree(hint.res_name);
            if (match) { found = (unsigned long)list[i]; break; }
        }
    }
    XFree(data);
    return found;
}

void run_app_mode_with_path(const char *config_path, const char *key_arg) {
    char xdg_path[1024];
    app_binding_xdg_path(xdg_path, sizeof(xdg_path));
    config_path = xdg_path;
    if (!key_arg) {
        fprintf(stderr, COLOR_RED "--run-app requires --key <spec>\n" COLOR_RESET);
        return;
    }
    char modifiers[64] = "", key[32] = "";
    if (parse_shortcut(key_arg, modifiers, sizeof(modifiers), key, sizeof(key)) != 0) {
        fprintf(stderr, COLOR_RED "Invalid key spec: %s\n" COLOR_RESET, key_arg);
        return;
    }
    AppBinding *list = NULL; int count = 0;
    app_binding_load(config_path, &list, &count);
    const AppBinding *b = app_binding_find(list, count, modifiers, key);
    if (!b) {
        fprintf(stderr, COLOR_RED "No app binding for %s\n" COLOR_RESET, key_arg);
        app_binding_free(list, count);
        return;
    }
    char cmd[256], wc[256], bound_mod[64], bound_key[32];
    strncpy(cmd, b->cmd ? b->cmd : "", sizeof(cmd) - 1); cmd[sizeof(cmd)-1] = '\0';
    strncpy(wc,  b->wm_class ? b->wm_class : "", sizeof(wc) - 1); wc[sizeof(wc)-1] = '\0';
    strncpy(bound_mod, b->modifiers ? b->modifiers : "", sizeof(bound_mod) - 1); bound_mod[sizeof(bound_mod)-1] = '\0';
    strncpy(bound_key, b->key ? b->key : "", sizeof(bound_key) - 1); bound_key[sizeof(bound_key)-1] = '\0';
    unsigned long anchor = b->target_window;
    app_binding_free(list, count);

    fprintf(stderr, "Window Toggle (app): key=%s cmd=%s class=%s anchor=0x%lx\n",
            key_arg, cmd, wc, anchor);

    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, COLOR_RED "Failed to open X display\n" COLOR_RESET);
        return;
    }
    /* Tolerate stale XIDs: anchor may have died; suppress BadWindow noise. */
    XSync(display, False);
    XSetErrorHandler(silent_xerror_handler);


    /* Step 1: try the anchored window. */
    int state = STATE_NOT_RUNNING;
    if (anchor != 0) {
        state = get_window_state(display, (Window)anchor);
        if (state == STATE_NOT_RUNNING) {
            fprintf(stderr, COLOR_YELLOW "Anchored window 0x%lx is gone\n" COLOR_RESET, anchor);
            /* Anchor 没活着, 但是其它同名窗口可能已经在跑 (单实例 app 看起来这样:
             * 上次 anchor 指向的窗口被关了, 新开一个 nautilus/code 用新 XID, 
             * 老 anchor XID 被回收给别的进程而我们这边以为是死了)。
             * 直接扫一遍匹配 wm_class 的活窗口, 找到的话重新锚定它就直接 toggle,
             * 不用 fork+exec 再启动一个 (单实例 app 再 exec 也白搭)。 */
            unsigned long alive = find_window_by_class(display, wc);
            if (alive && alive != anchor) {
                fprintf(stderr, COLOR_GREEN "Re-anchoring to existing window 0x%lx\n" COLOR_RESET, alive);
                app_binding_update_anchor(config_path, bound_mod, bound_key, alive);
                /* 同步 sibling binding 的 anchor, 跟新启动走一样的逻辑 */
                AppBinding *rsibs = NULL; int rsc = 0;
                app_binding_load(config_path, &rsibs, &rsc);
                for (int rsi = 0; rsi < rsc; rsi++) {
                    const AppBinding *rsb = &rsibs[rsi];
                    if (!rsb->cmd || strcmp(rsb->cmd, cmd) != 0) continue;
                    if (!rsb->wm_class || strcmp(rsb->wm_class, wc) != 0) continue;
                    if (strcmp(rsb->modifiers ? rsb->modifiers : "", bound_mod) == 0 &&
                        strcmp(rsb->key ? rsb->key : "", bound_key) == 0) continue;
                    app_binding_update_anchor(config_path, rsb->modifiers ? rsb->modifiers : "",
                                              rsb->key ? rsb->key : "", alive);
                }
                app_binding_free(rsibs, rsc);
                anchor = alive;
                state = get_window_state(display, (Window)alive);
            }
        }
    }

    if (state == STATE_NOT_RUNNING) {
        /* Step 2: launch and poll for a matching window. */
        fprintf(stderr, COLOR_BOLD "Launching: %s\n" COLOR_RESET, cmd);
        fflush(stderr);
        /* Snapshot the desktop window list BEFORE forking. Several apps
         * share the same WM_CLASS (every X-terminal-emulator reports
         * "X-terminal-emulator"). Without this snapshot,
         * find_window_by_class would happily return some pre-existing
         * terminal window belonging to a different binding, and the
         * new shortcut would anchor to the wrong window. */
        Window *pre_existing = snapshot_existing_windows(display);

        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            execlp(cmd, cmd, NULL);
            perror("exec failed");
            _exit(1);
        } else if (pid < 0) {
            perror("fork failed");
            free(pre_existing);
            XCloseDisplay(display);
            return;
        }

        unsigned long found = 0;
        const int max_iters = 30; /* 30 * 100ms = 3s */
        for (int i = 0; i < max_iters; i++) {
            usleep(100000);
            found = find_new_window_by_class(display, wc, pre_existing);
            if (found) break;
        }
        free(pre_existing);
        if (!found) {
            fprintf(stderr, COLOR_RED "Timeout: no %s window appeared within 3s\n" COLOR_RESET, wc);
            XCloseDisplay(display);
            return;
        }
        fprintf(stderr, COLOR_GREEN "Anchoring to new window 0x%lx\n" COLOR_RESET, found);
        app_binding_update_anchor(config_path, bound_mod, bound_key, found);
        /* 把同 cmd/wm_class 的其他 modifier binding 也设成同一个 anchor XID。
         * 这样 Ctrl+Fx / Super+Fx / Alt+Fx 任意一个按下去都 toggle 同一个窗口。
         * 把所有 modifier 都强制更新, 这样老 anchor 死了时, 老 binding 也不会指向死 ID。 */
        AppBinding *siblings = NULL; int sc = 0;
        app_binding_load(config_path, &siblings, &sc);
        for (int si = 0; si < sc; si++) {
            const AppBinding *sb = &siblings[si];
            if (!sb->cmd || strcmp(sb->cmd, cmd) != 0) continue;
            if (!sb->wm_class || strcmp(sb->wm_class, wc) != 0) continue;
            /* 跳过自己 (run_app_mode_with_path 已经 update_anchor 自己这条) */
            if (strcmp(sb->modifiers ? sb->modifiers : "", bound_mod) == 0 &&
                strcmp(sb->key ? sb->key : "", bound_key) == 0) continue;
            app_binding_update_anchor(config_path, sb->modifiers ? sb->modifiers : "",
                                      sb->key ? sb->key : "", found);
        }
        app_binding_free(siblings, sc);
        /* New window is visible by default — do not toggle. */
        XCloseDisplay(display);
        return;
    }

    /* Step 3: anchored window exists → 三路 toggle
     *   - hidden → activate
     *   - visible & 在最顶 → minimize
     *   - visible & 被压在底下 → raise (不 minimize,否则用户看不到反应) */
    if (state == STATE_HIDDEN) {
        activate_window(display, (Window)anchor);
        fprintf(stderr, COLOR_GREEN "Window activated\n" COLOR_RESET);
    } else if (is_window_on_top(display, (Window)anchor)) {
        minimize_window(display, (Window)anchor);
        fprintf(stderr, COLOR_GREEN "Window minimized\n" COLOR_RESET);
    } else {
        raise_window(display, (Window)anchor);
        fprintf(stderr, COLOR_GREEN "Window raised\n" COLOR_RESET);
    }
    XCloseDisplay(display);
}
