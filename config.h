#ifndef CONFIG_H
#define CONFIG_H

#include <X11/Xlib.h>

typedef struct {
    char *modifiers[4];  // Ctrl, Alt, Shift, etc.
    char *key;           // F12, etc.
    Window target_window;
    char *window_title;
    char *window_class;
} Config;

/* Configuration management */
Config* load_config(const char *path);
void save_config(const char *path, Config *config);
void save_shortcut_mapping(const char *path, const char *shortcut, Window window_id, const char *window_title, const char *window_class, int slot_id);
Window find_window_by_key(const char *path, const char *shortcut);
int config_exists(const char *path);

/* Default config path */
#define CONFIG_PATH "/tmp/window-toggle-config.json"

#endif
