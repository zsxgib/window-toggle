#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>

#include "config.h"

int config_exists(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp) {
        fclose(fp);
        return 1;
    }
    return 0;
}

/* Treat ("", F1) as alias of ("Super", F1) for legacy migration.
 * Returns 1 if the two modifier strings should be considered equivalent
 * for the purpose of dedup or lookup. */
static int mod_aliases_match(const char *a, const char *b) {
    if (strcmp(a, b) == 0) return 1;
    int a_bare = (a[0] == '\0' || strcmp(a, "Super") == 0);
    int b_bare = (b[0] == '\0' || strcmp(b, "Super") == 0);
    if (a_bare && b_bare) return 1;
    return 0;
}

void free_config(Config *config);

Config* load_config(const char *path) {
    Config *config = malloc(sizeof(Config));
    if (!config) return NULL;

    for (int i = 0; i < 4; i++) {
        config->modifiers[i] = NULL;
    }
    config->key = NULL;
    config->target_window = 0;
    config->window_title = NULL;
    config->window_class = NULL;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        free_config(config);
        return NULL;
    }

    char line[256];

    while (fgets(line, sizeof(line), fp)) {
        /* Stop at the app_bindings section delimiter; load_config only reads slot rows. */
        if (strncmp(line, "### app_bindings ###", 21) == 0) break;
        /* Parse modifiers - simple format: "modifiers: Super" */
        if (strncmp(line, "modifiers:", 10) == 0) {
            char *p = line + 10;
            while (*p == ' ' || *p == '\t') p++;
            char *end = line + strlen(line) - 1;
            while (end > p && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')) {
                *end = '\0';
                end--;
            }
            if (strlen(p) > 0) {
                config->modifiers[0] = strdup(p);
            }
        }

        /* Parse key - simple format: "key: F2" */
        else if (strncmp(line, "key:", 4) == 0) {
            char *p = line + 4;
            while (*p == ' ' || *p == '\t') p++;
            char *end = line + strlen(line) - 1;
            while (end > p && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')) {
                *end = '\0';
                end--;
            }
            if (strlen(p) > 0) {
                config->key = strdup(p);
            }
        }

        /* Parse window ID - simple format: "window_id: 0x123456" */
        else if (strncmp(line, "window_id:", 10) == 0) {
            char *p = line + 10;
            while (*p == ' ' || *p == '\t') p++;
            sscanf(p, "0x%lx", &config->target_window);
        }

        /* Parse window title - simple format: "window_title: Some Title" */
        else if (strncmp(line, "window_title:", 13) == 0) {
            char *p = line + 13;
            while (*p == ' ' || *p == '\t') p++;
            char *end = line + strlen(line) - 1;
            while (end > p && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')) {
                *end = '\0';
                end--;
            }
            if (strlen(p) > 0) {
                config->window_title = strdup(p);
            }
        }

        /* Parse window class - simple format: "window_class: SomeClass" */
        else if (strncmp(line, "window_class:", 13) == 0) {
            char *p = line + 13;
            while (*p == ' ' || *p == '\t') p++;
            char *end = line + strlen(line) - 1;
            while (end > p && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')) {
                *end = '\0';
                end--;
            }
            if (strlen(p) > 0) {
                config->window_class = strdup(p);
            }
        }
    }

    fclose(fp);
    return config;
}

void save_config(const char *path, Config *config) {
    FILE *fp = fopen(path, "w");
    if (!fp) return;

    /* Simplified flat format */
    fprintf(fp, "modifiers: %s\n",
            config->modifiers[0] ? config->modifiers[0] : "Super");
    fprintf(fp, "key: %s\n", config->key);
    fprintf(fp, "window_id: 0x%lx\n", config->target_window);
    fprintf(fp, "window_title: %s\n", config->window_title);
    fprintf(fp, "window_class: %s\n", config->window_class);

    fclose(fp);
}

/* New function to save shortcut mapping (with overwrite support) */
void save_shortcut_mapping(const char *path, const char *shortcut, Window window_id, const char *window_title, const char *window_class, int slot_id) {
    /* Parse shortcut to extract modifiers and key.
     * Default modifiers is "" (empty) so bare Fx is stored distinctly from Super+Fx. */
    char modifiers[64] = "";
    char key[32] = "";

    /* Find the last '+' to separate modifiers from key */
    const char *last_plus = strrchr(shortcut, '+');
    if (last_plus) {
        /* Extract modifiers */
        size_t modifiers_len = last_plus - shortcut;
        if (modifiers_len < sizeof(modifiers)) {
            strncpy(modifiers, shortcut, modifiers_len);
            modifiers[modifiers_len] = '\0';
        }
        /* Extract key */
        strncpy(key, last_plus + 1, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';
    } else {
        /* No modifiers, just a key */
        strncpy(key, shortcut, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';
    }

    FILE *fp = fopen(path, "r");
    if (fp) {
        fclose(fp);

        /* File exists, read all lines and filter out duplicate shortcuts */
        FILE *old_fp = fopen(path, "r");
        FILE *new_fp = fopen("/tmp/window-toggle-config-temp.json", "w");

        if (!old_fp || !new_fp) {
            if (old_fp) fclose(old_fp);
            if (new_fp) fclose(new_fp);
            return;
        }

        char line[4096];
        int in_duplicate_block = 0;
        char current_modifiers[64] = "";
        char current_key[32] = "";
        char pending_modifiers_line[4096] = "";
        int has_pending = 0;

        while (fgets(line, sizeof(line), old_fp)) {
            /* Check for empty line - end of block */
            if (line[0] == '\n' || line[0] == '\r') {
                if (in_duplicate_block) {
                    in_duplicate_block = 0;
                    current_modifiers[0] = '\0';
                    current_key[0] = '\0';
                    has_pending = 0;
                    continue;
                }
                current_modifiers[0] = '\0';
                current_key[0] = '\0';
                has_pending = 0;
                fputs(line, new_fp);
                continue;
            }

            /* Check for "modifiers:" line - track and buffer for delayed write.
             * IMPORTANT: copy line to pending FIRST (preserving \n) before
             * the strcspn below clobbers line's \n with \0. */
            if (strncmp(line, "modifiers:", 10) == 0) {
                strncpy(pending_modifiers_line, line, sizeof(pending_modifiers_line) - 1);
                pending_modifiers_line[sizeof(pending_modifiers_line) - 1] = '\0';
                has_pending = 1;
                char *p = line + 10;
                while (*p == ' ' || *p == '\t') p++;
                p[strcspn(p, "\r\n")] = 0;
                strncpy(current_modifiers, p, sizeof(current_modifiers) - 1);
                current_modifiers[sizeof(current_modifiers) - 1] = '\0';
                continue;
            }

            /* Check for "key:" line and extract the value */
            if (strncmp(line, "key:", 4) == 0) {
                char *p = line + 4;
                while (*p == ' ' || *p == '\t') p++;
                p[strcspn(p, "\r\n")] = 0;
                strncpy(current_key, p, sizeof(current_key) - 1);
                current_key[sizeof(current_key) - 1] = '\0';
                /* Skip this block only if BOTH key and modifiers match the incoming */
                if (strcmp(current_key, key) == 0 && mod_aliases_match(current_modifiers, modifiers)) {
                    in_duplicate_block = 1;
                    has_pending = 0;  /* discard pending modifiers line */
                    continue;
                }
                if (in_duplicate_block) continue;
                /* Not a duplicate - flush pending modifiers line first, then write key: */
                if (has_pending) {
                    fputs(pending_modifiers_line, new_fp);
                    has_pending = 0;
                }
                /* Reconstruct key line via fprintf */
                fprintf(new_fp, "key: %s\n", current_key);
                continue;
            }

            /* Skip lines in duplicate block */
            if (in_duplicate_block) {
                continue;
            }

            /* Keep all other lines - add newline if not present */
            size_t len = strlen(line);
            if (len > 0 && line[len-1] != '\n') {
                fputs(line, new_fp);
                fputc('\n', new_fp);
            } else {
                fputs(line, new_fp);
            }
        }

        /* If EOF inside a non-duplicate block, the block has no trailing blank line.
         * No special handling needed - the block was already emitted. */

        fclose(old_fp);
        fclose(new_fp);

        /* Add the new shortcut mapping with slot_id */
        fp = fopen("/tmp/window-toggle-config-temp.json", "a");
        if (fp) {
            fprintf(fp, "modifiers: %s\n", modifiers);
            fprintf(fp, "key: %s\n", key);
            fprintf(fp, "window_id: 0x%lx\n", window_id);
            fprintf(fp, "window_title: %s\n", window_title);
            fprintf(fp, "window_class: %s\n", window_class);
            fprintf(fp, "slot_id: %d\n", slot_id);
            fprintf(fp, "\n");  /* Add blank line between entries */
            fclose(fp);
        }

        /* Preserve the app_bindings section (anything after the delimiter).
         * 注意：old_fp 在上面已经 fclose()。用已关闭的 FILE* 调
         * rewind()/fgets() 是未定义行为，glibc 在 IO 清理时会撞上
         * 'free(): invalid pointer' 断言 → IOT core dump。
         * 重新 fopen 一次得到独立的 preserved_fp 解决 UAF。 */
        {
            FILE *preserved_fp = fopen(path, "r");
            if (preserved_fp) {
                char sect_line[8192];
                int seen_delim = 0;
                FILE *append_fp = fopen("/tmp/window-toggle-config-temp.json", "a");
                if (append_fp) {
                    while (fgets(sect_line, sizeof(sect_line), preserved_fp)) {
                        if (seen_delim) fputs(sect_line, append_fp);
                        else {
                            sect_line[strcspn(sect_line, "\r\n")] = '\0';
                            if (strcmp(sect_line, "### app_bindings ###") == 0) seen_delim = 1;
                        }
                    }
                    fclose(append_fp);
                }
                fclose(preserved_fp);
            }
        }

        /* Replace original file */
        rename("/tmp/window-toggle-config-temp.json", path);
    } else {
        /* File doesn't exist, create new file */
        fp = fopen(path, "w");
        if (!fp) return;
        fprintf(fp, "modifiers: %s\n", modifiers);
        fprintf(fp, "key: %s\n", key);
        fprintf(fp, "window_id: 0x%lx\n", window_id);
        fprintf(fp, "window_title: %s\n", window_title);
        fprintf(fp, "window_class: %s\n", window_class);
        fprintf(fp, "slot_id: %d\n", slot_id);
        fclose(fp);
    }
}

/* Find window ID by shortcut from config file.
 * `shortcut` is the full dconf command `--key` value: "F1", "Ctrl+F1",
 * "Ctrl+Alt+F1", "Ctrl+Shift+F1", "Super+F1". Parsed internally. */
Window find_window_by_key(const char *path, const char *shortcut) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return 0;
    }

    /* Parse the requested shortcut into modifiers and key */
    char req_modifiers[64] = "";
    char req_key[32] = "";
    if (shortcut) {
        const char *last_plus = strrchr(shortcut, '+');
        if (last_plus) {
            size_t mod_len = last_plus - shortcut;
            if (mod_len < sizeof(req_modifiers)) {
                strncpy(req_modifiers, shortcut, mod_len);
                req_modifiers[mod_len] = '\0';
            }
            strncpy(req_key, last_plus + 1, sizeof(req_key) - 1);
            req_key[sizeof(req_key) - 1] = '\0';
        } else {
            strncpy(req_key, shortcut, sizeof(req_key) - 1);
            req_key[sizeof(req_key) - 1] = '\0';
        }
    }

    /* Parse JSON format */
    char current_modifiers[64] = "";
    char current_key[32] = "";
    unsigned long current_window_id = 0;
    char line[4096];
    Window found_window = 0;

    while (fgets(line, sizeof(line), fp)) {
        /* Remove newline */
        line[strcspn(line, "\r\n")] = 0;

        /* Stop at the app_bindings section delimiter; slot data ends here. */
        if (strcmp(line, "### app_bindings ###") == 0) {
            break;
        }

        /* Skip empty lines - if we have a complete config, check if it matches */
        if (line[0] == '\0') {
            if (current_key[0] != '\0' && current_window_id != 0) {
                /* Match (modifiers, key) pair. Use alias matcher for legacy "Super" rows. */
                if (strcmp(current_key, req_key) == 0 && mod_aliases_match(current_modifiers, req_modifiers)) {
                    found_window = (Window)current_window_id;
                    break;
                }
                /* Reset for next block */
                current_modifiers[0] = '\0';
                current_key[0] = '\0';
                current_window_id = 0;
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
        }
    }

    fclose(fp);
    return found_window;
}

void free_config(Config *config) {
    if (!config) return;

    for (int i = 0; i < 4; i++) {
        if (config->modifiers[i]) {
            free(config->modifiers[i]);
        }
    }

    if (config->key) free(config->key);
    if (config->window_title) free(config->window_title);
    if (config->window_class) free(config->window_class);

    free(config);
}
