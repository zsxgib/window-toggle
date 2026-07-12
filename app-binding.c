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

/* Forward declarations for modifier-list encoding helpers (defined below). */
static char *enc_modifiers_from_array(const char *const *mods, int n);
static int split_modifiers(const char *s, char **outbuf, int max_out, int *out_count);
static void merge_duplicate_rows(AppBinding *list, int *count_io);

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
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return 0; }

    AppBinding *list = NULL;
    int cap = 0, n = 0;

    /* Each block in the file starts with `modifiers:` which may carry
     * several modifier names joined by '|'. When we read such a row we
     * expand it into multiple AppBinding rows (one per modifier), and
     * `block_start` records where this block begins so that subsequent
     * `key/cmd/wm_class/target_window` lines only apply to rows in this
     * block — not to every row we've read so far. */
    int block_start = 0;

    while (fgets(line, sizeof(line), fp)) {
        rtrim(line);
        if (line[0] == '\0') {
            /* Blank line ends the current block. Field writes after this
             * (none, because we just consumed the separator) belong to
             * the next block. block_start stays at n. */
            block_start = n;
            continue;
        }
        char *v = NULL;
        if (parse_kv(line, "modifiers", &v)) {
            char *pieces[8] = {0};
            int np = 0;
            if (split_modifiers(v, pieces, 8, &np) == 0 && np > 0) {
                block_start = n;
                for (int i = 0; i < np; i++) {
                    if (n == cap) {
                        cap = cap ? cap * 2 : 4;
                        list = realloc(list, cap * sizeof(AppBinding));
                    }
                    list[n].modifiers = strdup(pieces[i]);
                    list[n].key = NULL;
                    list[n].cmd = NULL;
                    list[n].wm_class = NULL;
                    list[n].target_window = 0;
                    n++;
                }
            } else {
                /* `modifiers: ""` (bare Fx) is a single row. */
                block_start = n;
                if (n == cap) {
                    cap = cap ? cap * 2 : 4;
                    list = realloc(list, cap * sizeof(AppBinding));
                }
                list[n].modifiers = strdup("");
                list[n].key = NULL;
                list[n].cmd = NULL;
                list[n].wm_class = NULL;
                list[n].target_window = 0;
                n++;
            }
            for (int i = 0; i < np; i++) free(pieces[i]);
        } else if (parse_kv(line, "key", &v)) {
            for (int k = block_start; k < n; k++) {
                free(list[k].key);
                list[k].key = strdup(v);
            }
        } else if (parse_kv(line, "cmd", &v)) {
            for (int k = block_start; k < n; k++) {
                free(list[k].cmd);
                list[k].cmd = strdup(v);
            }
        } else if (parse_kv(line, "wm_class", &v)) {
            for (int k = block_start; k < n; k++) {
                free(list[k].wm_class);
                list[k].wm_class = strdup(v);
            }
        } else if (parse_kv(line, "target_window", &v)) {
            unsigned long win = 0;
            sscanf(v, "0x%lx", &win);
            for (int k = block_start; k < n; k++) list[k].target_window = win;
        }
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

/* ----- Modifier-list encoding --------------------------------------------
 *
 * On disk we store multiple modifiers on a single record by joining them
 * with the pipe character ("|") and ordering them alphanumerically. This
 * lets a single record represent the trio (Ctrl+Super+Alt) without three
 * near-identical blocks. The in-memory layout still uses one AppBinding
 * per (modifiers, key) pair, so rest of the code is unchanged.
 *
 * Storage forms:
 *   "Ctrl"           single modifier
 *   "Ctrl+Super+Alt" "+" inside one entry -> rejected, we use pipes
 *   "Ctrl|Super|Alt" three modifiers on one logical binding
 *   ""               bare (no modifier) Fx
 *
 * Encoding sorts the modifiers so reordering after writes doesn't churn
 * the file.
 */

static int sort_strs(char **strs, int n) {
    /* simple insertion sort; modifier count is tiny (<= 5) */
    for (int i = 1; i < n; i++) {
        char *cur = strs[i];
        int j = i - 1;
        while (j >= 0 && strcmp(strs[j], cur) > 0) {
            strs[j + 1] = strs[j];
            j--;
        }
        strs[j + 1] = cur;
    }
    return 0;
}

/* Returns a freshly-malloc'd string like "Ctrl|Super|Alt". Caller frees. */
static char *enc_modifiers_from_array(const char *const *mods, int n) {
    /* copy + dedupe */
    char *seen[8] = {0};
    int kept = 0;
    for (int i = 0; i < n; i++) {
        const char *s = mods[i] ? mods[i] : "";
        int dup = 0;
        for (int k = 0; k < kept; k++) {
            if (strcmp(seen[k], s) == 0) { dup = 1; break; }
        }
        if (!dup) seen[kept++] = strdup(s);
    }
    /* sort */
    sort_strs(seen, kept);
    /* join with "|" */
    size_t total = 1;
    for (int i = 0; i < kept; i++) total += strlen(seen[i]) + 1;
    char *out = malloc(total);
    out[0] = '\0';
    for (int i = 0; i < kept; i++) {
        if (i > 0) strcat(out, "|");
        strcat(out, seen[i]);
    }
    for (int i = 0; i < kept; i++) free(seen[i]);
    return out;
}

/* Split a stored modifiers field into N modifier strings. Caller passes
 * an array of char* outbuf[8]; out_count receives the populated count.
 * Returns 0 on success. */
static int split_modifiers(const char *s, char **outbuf, int max_out, int *out_count) {
    if (!s || max_out <= 0) { if (out_count) *out_count = 0; return 0; }
    int n = 0;
    char *dup = strdup(s);
    if (!dup) return -1;
    char *saveptr = NULL;
    for (char *tok = strtok_r(dup, "|", &saveptr); tok != NULL;
         tok = strtok_r(NULL, "|", &saveptr)) {
        if (n >= max_out) break;
        outbuf[n++] = strdup(tok);
    }
    free(dup);
    if (out_count) *out_count = n;
    return 0;
}

/* Try to merge three AppBinding rows that share (key, cmd, wm_class,
 * target_window) into one by combining their modifiers into the first
 * row and deleting the rest. Called by app_binding_save via serialize.
 * Reorders the list in-place by moving merged rows down. */
static void merge_duplicate_rows(AppBinding *list, int *count_io) {
    int n = *count_io;
    int w = 0;
    for (int i = 0; i < n; i++) {
        if (!list[i].key || !list[i].cmd) { w++; continue; }
        int dup = -1;
        for (int j = 0; j < w; j++) {
            if (!list[j].key || !list[j].cmd) continue;
            if (strcmp(list[i].key, list[j].key) == 0 &&
                strcmp(list[i].cmd, list[j].cmd) == 0 &&
                strcmp(list[i].wm_class, list[j].wm_class) == 0 &&
                list[i].target_window == list[j].target_window) {
                dup = j; break;
            }
        }
        if (dup < 0) {
            /* Keep this row as the merge anchor. Deep-copy fields (don't
             * share char* with the trailing row, otherwise freeing the
             * trailing row's slots after compaction would double-free). */
            if (w != i) {
                list[w].modifiers = list[i].modifiers ? strdup(list[i].modifiers) : NULL;
                list[w].key       = list[i].key       ? strdup(list[i].key)       : NULL;
                list[w].cmd       = list[i].cmd       ? strdup(list[i].cmd)       : NULL;
                list[w].wm_class  = list[i].wm_class  ? strdup(list[i].wm_class)  : NULL;
                list[w].target_window = list[i].target_window;
            }
            w++;
        } else {
            /* Merge i into dup: split both stored modifier strings into
             * their individual names, then re-encode so duplicates are
             * collapsed (and list[i]'s dangling strings are released so
             * the trailing row's slots are safe to free). */
            char *names[16] = {0};
            int nn = 0;
            char *mpieces[8] = {0}; int mn = 0;
            if (split_modifiers(list[dup].modifiers ? list[dup].modifiers : "",
                                mpieces, 8, &mn) == 0) {
                for (int k = 0; k < mn && nn < 16; k++) names[nn++] = mpieces[k];
            }
            char *ipieces[8] = {0}; int in_ = 0;
            if (split_modifiers(list[i].modifiers ? list[i].modifiers : "",
                                ipieces, 8, &in_) == 0) {
                for (int k = 0; k < in_ && nn < 16; k++) names[nn++] = ipieces[k];
            }
            char *combined = enc_modifiers_from_array(
                (const char *const *)names, nn);
            free(list[dup].modifiers);
            list[dup].modifiers = combined;
            /* Free any names slots that were not consumed (when nn hit cap). */
            for (int k = 0; k < nn; k++) free(names[k]);
            /* Release list[i]'s own fields now: after compaction the slot
             * at index i will be freed by app_binding_free using the
             * caller's original count; clearing the strings prevents
             * double-free of strdup'd buffers. */
            free(list[i].modifiers);
            free(list[i].key);
            free(list[i].cmd);
            free(list[i].wm_class);
            list[i].modifiers = NULL;
            list[i].key       = NULL;
            list[i].cmd       = NULL;
            list[i].wm_class  = NULL;
        }
    }
    *count_io = w;
}

/* Serialize the full list back into a string (caller frees).
 *
 * Rows that share (key, cmd, wm_class, target_window) are merged into a
 * single record with their modifiers joined by '|', producing a compact
 * representation (e.g. one row per app+Fx instead of three). */
static char *serialize(AppBinding *list, int count) {
    merge_duplicate_rows(list, &count);
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

    if (ensure_parent_dir(config_path) != 0) {
        app_binding_free(list, count);
        if (in) fclose(in);
        return -1;
    }
    rename(tmp_path, config_path);
    return 0;
}

/* Wipe all app bindings from the config. Keeps the rest of the file (slot
 * section) and the ### app_bindings ### delimiter so existing slot data and
 * the slot parser's stop-marker remain valid. Returns 0 on success, including
 * the no-op case where the file does not exist or has no app_bindings section. */
int app_binding_clear_all(const char *config_path) {
    if (!config_path) return -1;
    FILE *in = fopen(config_path, "r");
    if (!in) return 0; /* nothing to clear */

    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.app.tmp", config_path);
    if (ensure_parent_dir(config_path) != 0) { fclose(in); return -1; }
    FILE *out = fopen(tmp_path, "w");
    if (!out) { fclose(in); return -1; }

    /* Copy the slot section verbatim up to and including the delimiter line.
     * If the delimiter is missing, the file has no app_bindings section;
     * leave the file untouched and exit cleanly. */
    long sect = find_section_start(in);
    if (sect < 0) {
        rewind(in);
        char line[8192];
        while (fgets(line, sizeof(line), in)) fputs(line, out);
        fclose(in); fclose(out);
        rename(tmp_path, config_path);
        return 0;
    }
    rewind(in);
    long copied = 0;
    char line[8192];
    while (copied < sect && fgets(line, sizeof(line), in)) {
        fputs(line, out);
        copied = ftell(in);
    }
    fclose(in);

    /* Re-emit the delimiter so the slot parser still stops here on reload. */
    fputs("### app_bindings ###\n", out);
    fclose(out);
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
    /* Shift rows left; deep-copy each surviving row's strings so the slot
     * at the old tail position does not share pointers with them (otherwise
     * app_binding_free would double-free). */
    for (int j = found; j < count - 1; j++) {
        list[j].modifiers = list[j+1].modifiers ? strdup(list[j+1].modifiers) : NULL;
        list[j].key       = list[j+1].key       ? strdup(list[j+1].key)       : NULL;
        list[j].cmd       = list[j+1].cmd       ? strdup(list[j+1].cmd)       : NULL;
        list[j].wm_class  = list[j+1].wm_class  ? strdup(list[j+1].wm_class)  : NULL;
        list[j].target_window = list[j+1].target_window;
        free(list[j+1].modifiers);
        free(list[j+1].key);
        free(list[j+1].cmd);
        free(list[j+1].wm_class);
        list[j+1].modifiers = NULL;
        list[j+1].key       = NULL;
        list[j+1].cmd       = NULL;
        list[j+1].wm_class  = NULL;
    }
    /* Clear the freed tail row so app_binding_free(N) is a no-op there. */
    list[count - 1].modifiers = NULL;
    list[count - 1].key       = NULL;
    list[count - 1].cmd       = NULL;
    list[count - 1].wm_class  = NULL;
    list[count - 1].target_window = 0;
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
