/**
 * @file backup.c
 * @brief Implementation of Emacs-style backup (~) and autosave (#...#) files.
 */

#define _POSIX_C_SOURCE 200809L
#include "backup.h"
#include "buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

bool backup_create_tilde(const char *file_path) {
    if (!file_path || !file_path[0]) return false;

    struct stat st;
    if (stat(file_path, &st) != 0) return false;

    char backup_path[PATH_MAX];
    snprintf(backup_path, sizeof(backup_path), "%s~", file_path);

    FILE *src = fopen(file_path, "rb");
    if (!src) return false;

    FILE *dst = fopen(backup_path, "wb");
    if (!dst) {
        fclose(src);
        return false;
    }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            fclose(src);
            fclose(dst);
            return false;
        }
    }

    fclose(src);
    fclose(dst);
    return true;
}

/**
 * @brief Construct the autosave recovery file path (#filename#) from a target path.
 *
 * @param file_path Source file path.
 * @param out Buffer to receive the generated autosave file path.
 * @param cap Capacity of the output buffer in bytes.
 */
static void get_autosave_path(const char *file_path, char *out, size_t cap) {
    const char *slash = strrchr(file_path, '/');
    if (slash) {
        size_t dlen = (size_t)(slash - file_path);
        snprintf(out, cap, "%.*s/#%s#", (int)dlen, file_path, slash + 1);
    } else {
        snprintf(out, cap, "#%s#", file_path);
    }
}

bool backup_create_autosave(const char *file_path, const void *buf_ptr) {
    if (!file_path || !file_path[0] || !buf_ptr) return false;
    const Buffer *buf = (const Buffer *)buf_ptr;

    char as_path[PATH_MAX];
    get_autosave_path(file_path, as_path, sizeof(as_path));

    FILE *f = fopen(as_path, "wb");
    if (!f) return false;

    for (size_t i = 0; i < buf->count; ++i) {
        const char *line = buffer_line(buf, i);
        size_t len = buffer_line_len(buf, i);
        if (len > 0) {
            fwrite(line, 1, len, f);
        }
        if (buf->has_crlf_endings) {
            fputs("\r\n", f);
        } else {
            fputc('\n', f);
        }
    }

    fclose(f);
    return true;
}

bool backup_remove_autosave(const char *file_path) {
    if (!file_path || !file_path[0]) return false;
    char as_path[PATH_MAX];
    get_autosave_path(file_path, as_path, sizeof(as_path));
    unlink(as_path);
    return true;
}
