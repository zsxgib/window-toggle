#ifndef APP_BINDING_H
#define APP_BINDING_H

#include <stddef.h>
#include <X11/Xlib.h>

/* One app binding row stored in the config's app_bindings section. */
typedef struct {
    char *modifiers;   /* e.g. "Ctrl+Alt", "Super", "Ctrl+Shift", "Ctrl", or "" (bare Fx) */
    char *key;         /* e.g. "F12" */
    char *cmd;         /* e.g. "nautilus" */
    char *wm_class;    /* e.g. "org.gnome.Nautilus" */
    unsigned long target_window; /* first-launch-anchored XID; 0 = not yet anchored */
} AppBinding;

/* Section delimiter in the config file. Slot parsing stops at this line. */
#define APP_BINDINGS_DELIM "### app_bindings ###"

/* Load all app bindings from the config. Returns count; *out is malloc'd
 * array (caller frees with app_binding_free). Returns 0 if section absent. */
int app_binding_load(const char *config_path, AppBinding **out, int *count);

/* Free array returned by app_binding_load. */
void app_binding_free(AppBinding *list, int count);

/* Add or update a binding keyed by (modifiers, key). Returns 0 on success. */
int app_binding_add(const char *config_path, const char *modifiers, const char *key,
                    const char *cmd, const char *wm_class, unsigned long target_window);

/* Remove a binding keyed by (modifiers, key). Returns 0 on success. */
int app_binding_remove(const char *config_path, const char *modifiers, const char *key);

/* Wipe all app bindings (the ### app_bindings ### section) while leaving
 * the slot section and the delimiter intact. Used by --clean so a single
 * command tears down every shortcut (slot + app binding + viewer) without
 * leaving dangling config rows. Returns 0 on success, including the no-op
 * case where the section does not exist. */
int app_binding_clear_all(const char *config_path);

/* Find binding by (modifiers, key). Returns NULL if not found.
 * modifiers may be NULL or "" to match bare-Fx. */
const AppBinding *app_binding_find(const AppBinding *list, int count,
                                   const char *modifiers, const char *key);

/* Update only the target_window field for an existing binding. */
int app_binding_update_anchor(const char *config_path, const char *modifiers, const char *key,
                              unsigned long target_window);

/* Parse a full shortcut string like "Ctrl+Alt+F12" into modifiers + key.
 * Returns 0 on success, -1 on failure. Buffers must be at least 64 / 32 bytes. */
int parse_shortcut(const char *shortcut, char *modifiers_out, size_t mod_size,
                   char *key_out, size_t key_size);



/* Resolve the XDG config path for app bindings: $XDG_CONFIG_HOME/window-toggle/bindings.json
 * (default ~/.config/window-toggle/bindings.json). Caller passes a buffer of size >= 256. */
void app_binding_xdg_path(char *out, size_t out_size);

#endif /* APP_BINDING_H */
