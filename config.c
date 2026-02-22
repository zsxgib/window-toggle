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
    /* Parse shortcut to extract modifiers and key */
    char modifiers[64] = "Super";
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

    /* Debug output */
    fprintf(stderr, "DEBUG: Parsed shortcut '%s' -> modifiers='%s', key='%s'\n", shortcut, modifiers, key);

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

        char line[512];
        int in_duplicate_block = 0;

        while (fgets(line, sizeof(line), old_fp)) {
            /* Check if this line starts a new block */
            if (strncmp(line, "modifiers:", 10) == 0) {
                in_duplicate_block = 0;
            }

            /* Check if this line contains the key we're looking for */
            if (strncmp(line, "key:", 4) == 0) {
                char *p = line + 4;
                while (*p == ' ' || *p == '\t') p++;
                p[strcspn(p, "\r\n")] = 0;
                /* If this key matches, skip this entire block */
                if (strcmp(p, key) == 0) {
                    in_duplicate_block = 1;
                    continue;
                }
            }

            /* Skip lines in duplicate block */
            if (in_duplicate_block) {
                /* Check if this is an empty line (end of block) */
                if (line[0] == '\n' || line[0] == '\r') {
                    in_duplicate_block = 0;
                }
                continue;
            }

            /* Keep all other lines */
            fputs(line, new_fp);
        }

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

/* Find window ID by key from config file */
Window find_window_by_key(const char *path, const char *key) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return 0;
    }

    /* Parse JSON format */
    char current_modifiers[64] = "";
    char current_key[32] = "";
    unsigned long current_window_id = 0;
    char line[512];
    Window found_window = 0;

    while (fgets(line, sizeof(line), fp)) {
        /* Remove newline */
        line[strcspn(line, "\r\n")] = 0;

        /* Skip empty lines - if we have a complete config, check if it matches */
        if (line[0] == '\0') {
            if (current_modifiers[0] != '\0' && current_key[0] != '\0' && current_window_id != 0) {
                /* Check if this config matches the requested key */
                if (strcmp(current_key, key) == 0) {
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
