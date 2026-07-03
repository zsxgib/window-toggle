#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app-binding.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <libgen.h>

void app_binding_xdg_path(char *out, size_t out_size) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        snprintf(out, out_size, "%s/window-toggle/bindings.json", xdg);
    } else {
        const char *home = getenv("HOME");
        if (!home) home = "/tmp";
        snprintf(out, out_size, "%s/.config/window-toggle/bindings.json", home);
    }
}

/* Ensure the parent directory of `path` exists (mkdir -p semantics). */
static int ensure_parent_dir(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    /* dirname() may modify its argument. */
    char *d = dirname(tmp);
    if (!d || !*d) return 0;
    /* Walk path components, mkdir each. */
    char accum[1024] = {0};
    if (d[0] == '/') { accum[0] = '/'; accum[1] = 0; }
    for (const char *p = d; *p; ) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len > 0) {
            size_t cur = strlen(accum);
            if (cur + len + 2 > sizeof(accum)) return -1;
            memcpy(accum + cur, p, len);
            accum[cur + len] = '\0';
        }
        if (mkdir(accum, 0755) != 0 && errno != EEXIST) return -1;
        if (!slash) break;
        p = slash + 1;
        size_t cur = strlen(accum);
        accum[cur] = '/';
        accum[cur + 1] = '\0';
    }
    return 0;
}

int parse_shortcut(const char *shortcut, char *modifiers_out, size_t mod_size,
                   char *key_out, size_t key_size) {
    if (!shortcut || !modifiers_out || !key_out) return -1;
    const char *last_plus = strrchr(shortcut, '+');
    if (last_plus) {
        size_t mod_len = (size_t)(last_plus - shortcut);
        if (mod_len >= mod_size) mod_len = mod_size - 1;
        memcpy(modifiers_out, shortcut, mod_len);
        modifiers_out[mod_len] = '\0';
        strncpy(key_out, last_plus + 1, key_size - 1);
        key_out[key_size - 1] = '\0';
    } else {
        modifiers_out[0] = '\0';
        strncpy(key_out, shortcut, key_size - 1);
        key_out[key_size - 1] = '\0';
    }
    return 0;
}

/* Trim trailing CR/LF/whitespace in place. */
static void rtrim(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' ||
                       s[len-1] == ' '  || s[len-1] == '\t')) {
        s[--len] = '\0';
    }
}

/* Trim leading whitespace in place; returns pointer into original buffer. */
static char *ltrim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Parse "key: value" into key and value pointers; returns 0 on match. */
static int parse_kv(char *line, const char *key, char **value_out) {
    size_t klen = strlen(key);
    if (strncmp(line, key, klen) != 0) return 0;
    if (line[klen] != ':') return 0;
    char *p = ltrim(line + klen + 1);
    rtrim(p);
    *value_out = p;
    return 1;
}

/* Locate the line index where the app_bindings section starts.
 * Returns -1 if section not present. */
static long find_section_start(FILE *fp) {
    char line[8192];
    rewind(fp);
    long pos = 0;
    while (fgets(line, sizeof(line), fp)) {
        rtrim(line);
        if (strcmp(line, APP_BINDINGS_DELIM) == 0) {
            return pos;
        }
        pos = ftell(fp);
    }
    return -1;
}

int app_binding_load(const char *config_path, AppBinding **out, int *count) {
    *out = NULL;
    *count = 0;
    FILE *fp = fopen(config_path, "r");
    if (!fp) return 0;

    long start = find_section_start(fp);
    if (start < 0) { fclose(fp); return 0; }

    /* Re-read from section start, skip the delimiter line. */
    fseek(fp, start, SEEK_SET);
    char line[8192];
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return 0; } /* skip delimiter */

    AppBinding *list = NULL;
    int cap = 0, n = 0;
    AppBinding cur = {0};

    while (fgets(line, sizeof(line), fp)) {
        rtrim(line);
        if (line[0] == '\0') {
            if (cur.key) {
                if (n == cap) {
                    cap = cap ? cap * 2 : 4;
                    list = realloc(list, cap * sizeof(AppBinding));
                }
                list[n++] = cur;
                cur = (AppBinding){0};
            }
            continue;
        }
        char *v = NULL;
        if (parse_kv(line, "modifiers", &v)) {
            free(cur.modifiers);
            cur.modifiers = strdup(v);
        } else if (parse_kv(line, "key", &v)) {
            free(cur.key);
            cur.key = strdup(v);
        } else if (parse_kv(line, "cmd", &v)) {
            free(cur.cmd);
            cur.cmd = strdup(v);
        } else if (parse_kv(line, "wm_class", &v)) {
            free(cur.wm_class);
            cur.wm_class = strdup(v);
        } else if (parse_kv(line, "target_window", &v)) {
            sscanf(v, "0x%lx", &cur.target_window);
        }
    }
    /* EOF inside last block */
    if (cur.key) {
        if (n == cap) {
            cap = cap ? cap * 2 : 4;
            list = realloc(list, cap * sizeof(AppBinding));
        }
        list[n++] = cur;
        cur = (AppBinding){0};
    }

    fclose(fp);
    *out = list;
    *count = n;
    return n;
}

void app_binding_free(AppBinding *list, int count) {
    if (!list) return;
    for (int i = 0; i < count; i++) {
        free(list[i].modifiers);
        free(list[i].key);
        free(list[i].cmd);
        free(list[i].wm_class);
    }
    free(list);
}

const AppBinding *app_binding_find(const AppBinding *list, int count,
                                   const char *modifiers, const char *key) {
    if (!list || !key) return NULL;
    const char *m = modifiers ? modifiers : "";
    /* Same alias rule as slots: bare/empty/Super all match. */
    int m_bare = (m[0] == '\0');
    for (int i = 0; i < count; i++) {
        const char *cm = list[i].modifiers ? list[i].modifiers : "";
        if (strcmp(list[i].key, key) != 0) continue;
        if (strcmp(cm, m) == 0) return &list[i];
        int cm_bare = (cm[0] == '\0');
        if (m_bare && cm_bare) return &list[i];
    }
    return NULL;
}

/* Serialize the full list back into a string (caller frees). */
static char *serialize(const AppBinding *list, int count) {
    size_t cap = 4096;
    char *buf = malloc(cap);
    size_t len = 0;
    buf[0] = '\0';
    /* Delimiter + trailing newline. */
    {
        const char *delim = APP_BINDINGS_DELIM "\n";
        size_t dlen = strlen(delim);
        if (len + dlen + 1 > cap) { cap = len + dlen + 4096; buf = realloc(buf, cap); }
        memcpy(buf + len, delim, dlen); len += dlen; buf[len] = '\0';
    }
    for (int i = 0; i < count; i++) {
        const char *m = list[i].modifiers ? list[i].modifiers : "";
        char line[1024];
        int n = snprintf(line, sizeof(line),
                         "modifiers: %s\n"
                         "key: %s\n"
                         "cmd: %s\n"
                         "wm_class: %s\n"
                         "target_window: 0x%lx\n"
                         "\n",
                         m,
                         list[i].key ? list[i].key : "",
                         list[i].cmd ? list[i].cmd : "",
                         list[i].wm_class ? list[i].wm_class : "",
                         list[i].target_window);
        if (n < 0) continue;
        if (len + (size_t)n + 1 > cap) { cap = len + n + 4096; buf = realloc(buf, cap); }
        memcpy(buf + len, line, (size_t)n); len += (size_t)n; buf[len] = '\0';
    }
    return buf;
}

int app_binding_add(const char *config_path, const char *modifiers, const char *key,
                    const char *cmd, const char *wm_class, unsigned long target_window) {
    if (!key || !cmd || !wm_class) return -1;
    AppBinding *list = NULL; int count = 0;
    app_binding_load(config_path, &list, &count);

    /* Replace if same (modifiers, key) exists. */
    int found = -1;
    const char *m = modifiers ? modifiers : "";
    for (int i = 0; i < count; i++) {
        const char *cm = list[i].modifiers ? list[i].modifiers : "";
        int match = (strcmp(list[i].key, key) == 0) &&
                    (strcmp(cm, m) == 0 || (m[0] == '\0' && cm[0] == '\0'));
        if (match) { found = i; break; }
    }
    if (found >= 0) {
        free(list[found].cmd);      list[found].cmd = strdup(cmd);
        free(list[found].wm_class); list[found].wm_class = strdup(wm_class);
        list[found].target_window = target_window;
    } else {
        list = realloc(list, (count + 1) * sizeof(AppBinding));
        list[count].modifiers = strdup(m);
        list[count].key = strdup(key);
        list[count].cmd = strdup(cmd);
        list[count].wm_class = strdup(wm_class);
        list[count].target_window = target_window;
        count++;
    }

    /* Write: keep slot portion untouched, replace/append the app section. */
    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.app.tmp", config_path);
    if (ensure_parent_dir(config_path) != 0) {
        app_binding_free(list, count);
        return -1;
    }
    FILE *in = fopen(config_path, "r");
    FILE *out = fopen(tmp_path, "w");
    if (!out) {
        app_binding_free(list, count);
        if (in) fclose(in);
        return -1;
    }

    if (in) {
        char line[8192];
        long sect = find_section_start(in);
        if (sect < 0) {
            /* No section yet: copy entire file, then write section. */
            rewind(in);
            while (fgets(line, sizeof(line), in)) fputs(line, out);
        } else {
            /* Copy up to (but not including) the delimiter. */
            rewind(in);
            long copied = 0;
            while (copied < sect && fgets(line, sizeof(line), in)) {
                fputs(line, out);
                copied = ftell(in);
            }
        }
        fclose(in);
    }

    char *body = serialize(list, count);
    fputs(body, out);
    free(body);
    fclose(out);

    fprintf(stderr, "DBG add: about to ensure_parent_dir, config_path=%s\n", config_path);
    if (ensure_parent_dir(config_path) != 0) {
        app_binding_free(list, count);
        if (in) fclose(in);
        return -1;
    }
    fprintf(stderr, "DBG add: ensure ok, renaming %s -> %s\n", tmp_path, config_path);
    rename(tmp_path, config_path);
    return 0;
}

int app_binding_remove(const char *config_path, const char *modifiers, const char *key) {
    if (!key) return -1;
    AppBinding *list = NULL; int count = 0;
    app_binding_load(config_path, &list, &count);
    const char *m = modifiers ? modifiers : "";
    int found = -1;
    for (int i = 0; i < count; i++) {
        const char *cm = list[i].modifiers ? list[i].modifiers : "";
        if (strcmp(list[i].key, key) == 0 &&
            (strcmp(cm, m) == 0 || (m[0] == '\0' && cm[0] == '\0'))) {
            found = i; break;
        }
    }
    if (found < 0) { app_binding_free(list, count); return -1; }

    free(list[found].modifiers);
    free(list[found].key);
    free(list[found].cmd);
    free(list[found].wm_class);
    for (int j = found; j < count - 1; j++) list[j] = list[j+1];
    count--;

    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.app.tmp", config_path);
    if (ensure_parent_dir(config_path) != 0) {
        app_binding_free(list, count);
        return -1;
    }
    FILE *in = fopen(config_path, "r");
    FILE *out = fopen(tmp_path, "w");
    if (!out) { app_binding_free(list, count); if (in) fclose(in); return -1; }
    if (in) {
        char line[8192];
        long sect = find_section_start(in);
        if (sect < 0) {
            rewind(in);
            while (fgets(line, sizeof(line), in)) fputs(line, out);
        } else {
            rewind(in);
            long copied = 0;
            while (copied < sect && fgets(line, sizeof(line), in)) {
                fputs(line, out);
                copied = ftell(in);
            }
        }
        fclose(in);
    }
    if (count > 0) {
        char *body = serialize(list, count);
        fputs(body, out);
        free(body);
    }
    fclose(out);
    app_binding_free(list, count);
    rename(tmp_path, config_path);
    return 0;
}

int app_binding_update_anchor(const char *config_path, const char *modifiers, const char *key,
                              unsigned long target_window) {
    AppBinding *list = NULL; int count = 0;
    app_binding_load(config_path, &list, &count);
    const char *m = modifiers ? modifiers : "";
    int found = -1;
    for (int i = 0; i < count; i++) {
        const char *cm = list[i].modifiers ? list[i].modifiers : "";
        if (strcmp(list[i].key, key) == 0 &&
            (strcmp(cm, m) == 0 || (m[0] == '\0' && cm[0] == '\0'))) {
            found = i; break;
        }
    }
    if (found < 0) { app_binding_free(list, count); return -1; }
    list[found].target_window = target_window;

    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.app.tmp", config_path);
    if (ensure_parent_dir(config_path) != 0) {
        app_binding_free(list, count);
        return -1;
    }
    FILE *in = fopen(config_path, "r");
    FILE *out = fopen(tmp_path, "w");
    if (!out) { app_binding_free(list, count); if (in) fclose(in); return -1; }
    if (in) {
        char line[8192];
        long sect = find_section_start(in);
        if (sect < 0) {
            rewind(in);
            while (fgets(line, sizeof(line), in)) fputs(line, out);
        } else {
            rewind(in);
            long copied = 0;
            while (copied < sect && fgets(line, sizeof(line), in)) {
                fputs(line, out);
                copied = ftell(in);
            }
        }
        fclose(in);
    }
    char *body = serialize(list, count);
    fputs(body, out);
    free(body);
    fclose(out);
    app_binding_free(list, count);
    rename(tmp_path, config_path);
    return 0;
}
