/**
 * @file export.c
 * @brief Implementation of Markdown export to HTML, XML, EPUB, and LaTeX,
 *        with recursive hypertext linked-file inclusion.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "export.h"
#include "export_internal.h"
#include "markdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MDE_EXPORT_MAX_DOCS 128

/** @brief Safely copy msg into dst (if non-NULL and cap > 0). */
void set_str(char *dst, size_t cap, const char *msg) {
    if (dst && cap > 0) {
        snprintf(dst, cap, "%s", msg ? msg : "");
    }
}

/** @brief Return the human-readable display name for an ExportFormat. */
const char *export_format_name(ExportFormat fmt) {
    switch (fmt) {
        case EXPORT_HTML:  return "HTML";
        case EXPORT_XML:   return "XML";
        case EXPORT_EPUB:  return "EPUB";
        case EXPORT_LATEX: return "LaTeX";
        default:           return "Unknown";
    }
}

/** @brief Return the canonical file extension (including dot) for an ExportFormat. */
const char *export_format_ext(ExportFormat fmt) {
    switch (fmt) {
        case EXPORT_HTML:  return ".html";
        case EXPORT_XML:   return ".xml";
        case EXPORT_EPUB:  return ".epub";
        case EXPORT_LATEX: return ".tex";
        default:           return ".out";
    }
}

/** @brief Derive a default output file path from src_path and target ExportFormat. */
void export_default_path(const char *src_path, ExportFormat fmt, char *out_path, size_t max_len) {
    if (!out_path || max_len == 0) return;
    const char *ext = export_format_ext(fmt);

    if (!src_path || !src_path[0]) {
        snprintf(out_path, max_len, "untitled%s", ext);
        return;
    }

    const char *slash = strrchr(src_path, '/');
    const char *base = slash ? (slash + 1) : src_path;
    const char *dot = strrchr(base, '.');

    if (dot && dot != base &&
        (strcasecmp(dot, ".md") == 0 || strcasecmp(dot, ".markdown") == 0 ||
         strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".html") == 0 ||
         strcasecmp(dot, ".xml") == 0 || strcasecmp(dot, ".epub") == 0 ||
         strcasecmp(dot, ".tex") == 0)) {
        size_t stem_len = (size_t)(dot - src_path);
        snprintf(out_path, max_len, "%.*s%s", (int)stem_len, src_path, ext);
    } else {
        snprintf(out_path, max_len, "%s%s", src_path, ext);
    }
}

/** @brief Scan PATH for an executable pandoc binary. */
bool export_has_pandoc(void) {
    const char *path_env = getenv("PATH");
    if (!path_env || !path_env[0]) return false;

    char *path_copy = strdup(path_env);
    if (!path_copy) return false;

    bool found = false;
    char *saveptr = NULL;
    char *dir = strtok_r(path_copy, ":", &saveptr);
    while (dir) {
        char candidate[PATH_MAX];
        snprintf(candidate, sizeof(candidate), "%s/pandoc", dir[0] ? dir : ".");
        if (access(candidate, X_OK) == 0) {
            found = true;
            break;
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }

    free(path_copy);
    return found;
}

/**
 * @brief Extract a clean document title from the first H1 heading or source filename.
 */
static void extract_doc_title(const Buffer *buf, char *out_title, size_t cap) {
    if (!out_title || cap == 0) return;
    out_title[0] = '\0';

    if (buf) {
        bool in_code = false;
        char lang[32] = {0};
        size_t lines = buffer_line_count(buf);
        for (size_t r = 0; r < lines; ++r) {
            const char *l = buffer_line(buf, r);
            size_t len = buffer_line_len(buf, r);
            MdParsedLine pl;
            md_parse_line(l, len, &in_code, lang, sizeof(lang), &pl);
            if (pl.header_level == 1 && pl.span_count > 1) {
                size_t pos = 0;
                for (size_t s = 0; s < pl.span_count; ++s) {
                    if (md_is_header_marker(pl.spans[s].type)) continue;
                    size_t slen = pl.spans[s].end - pl.spans[s].start;
                    if (pos + slen >= cap - 1) slen = (cap - 1) - pos;
                    if (slen > 0) {
                        memcpy(out_title + pos, l + pl.spans[s].start, slen);
                        pos += slen;
                    }
                }
                out_title[pos] = '\0';
                md_parsed_line_free(&pl);
                if (out_title[0]) return;
            }
            md_parsed_line_free(&pl);
        }

        if (buf->file_path && buf->file_path[0]) {
            const char *slash = strrchr(buf->file_path, '/');
            const char *base = slash ? slash + 1 : buf->file_path;
            snprintf(out_title, cap, "%s", base);
            char *dot = strrchr(out_title, '.');
            if (dot && dot != out_title) *dot = '\0';
            if (out_title[0]) return;
        }
    }

    snprintf(out_title, cap, "Document");
}

/* =========================================================================
 * Hypertext Link Graph Discovery & Resolution
 * ========================================================================= */

/** @brief Initialize an empty ExportDocList container. */
void export_doc_list_init(ExportDocList *list) {
    list->nodes = NULL;
    list->count = 0;
    list->cap = 0;
}

/** @brief Free all owned child buffers and node array inside an ExportDocList. */
void export_doc_list_free(ExportDocList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; ++i) {
        if (list->nodes[i].owned_buf) {
            buffer_free(list->nodes[i].owned_buf);
            free(list->nodes[i].owned_buf);
        }
    }
    free(list->nodes);
    list->nodes = NULL;
    list->count = 0;
    list->cap = 0;
}

/** @brief Canonicalize in_path via realpath with lexical dot/dot-dot fallback. */
static void canonicalize_path(const char *in_path, char *out_path, size_t out_cap) {
    if (!in_path || !in_path[0] || !out_path || out_cap == 0) {
        if (out_path && out_cap > 0) out_path[0] = '\0';
        return;
    }

    char resolved[PATH_MAX];
    if (realpath(in_path, resolved)) {
        snprintf(out_path, out_cap, "%s", resolved);
        return;
    }

    // Lexical fallback when path does not exist yet
    char abs_buf[PATH_MAX * 2];
    if (in_path[0] == '/') {
        snprintf(abs_buf, sizeof(abs_buf), "%s", in_path);
    } else {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
            snprintf(abs_buf, sizeof(abs_buf), "%s/%s", cwd, in_path);
        } else {
            snprintf(abs_buf, sizeof(abs_buf), "/%s", in_path);
        }
    }

    char *parts[256];
    size_t nparts = 0;
    char *copy = strdup(abs_buf);
    if (!copy) {
        snprintf(out_path, out_cap, "%.*s", (int)(out_cap - 1), abs_buf);
        return;
    }

    char *saveptr = NULL;
    for (char *tok = strtok_r(copy, "/", &saveptr); tok; tok = strtok_r(NULL, "/", &saveptr)) {
        if (strcmp(tok, ".") == 0 || !tok[0]) continue;
        if (strcmp(tok, "..") == 0) {
            if (nparts > 0) nparts--;
        } else if (nparts < 256) {
            parts[nparts++] = tok;
        }
    }

    if (nparts == 0) {
        snprintf(out_path, out_cap, "/");
    } else {
        size_t pos = 0;
        out_path[0] = '\0';
        for (size_t i = 0; i < nparts; ++i) {
            int n = snprintf(out_path + pos, out_cap - pos, "/%s", parts[i]);
            if (n < 0 || pos + (size_t)n >= out_cap) break;
            pos += (size_t)n;
        }
    }
    free(copy);
}

/** @brief Extract the parent directory of abs_path (or CWD if empty). */
static void dir_from_path(const char *abs_path, char *out_dir, size_t out_cap) {
    if (!out_dir || out_cap == 0) return;
    if (abs_path && abs_path[0]) {
        const char *slash = strrchr(abs_path, '/');
        if (slash == abs_path) {
            snprintf(out_dir, out_cap, "/");
        } else if (slash) {
            size_t len = (size_t)(slash - abs_path);
            if (len >= out_cap) len = out_cap - 1;
            memcpy(out_dir, abs_path, len);
            out_dir[len] = '\0';
        } else {
            snprintf(out_dir, out_cap, ".");
        }
    } else {
        if (!getcwd(out_dir, out_cap)) {
            snprintf(out_dir, out_cap, ".");
        }
    }
}

/** @brief Return true if path has a recognized Markdown filename extension. */
static bool has_markdown_ext(const char *path) {
    if (!path || !path[0]) return false;
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base) return false;
    return (strcasecmp(dot, ".md") == 0 ||
            strcasecmp(dot, ".markdown") == 0 ||
            strcasecmp(dot, ".mdown") == 0 ||
            strcasecmp(dot, ".mkd") == 0);
}

/** @brief Parse a link URL and return true if it targets a local Markdown file (splitting #frag). */
static bool is_local_md_target(const char *url, size_t url_len,
                               char *out_file, size_t file_cap,
                               char *out_frag, size_t frag_cap) {
    if (out_file && file_cap > 0) out_file[0] = '\0';
    if (out_frag && frag_cap > 0) out_frag[0] = '\0';
    if (!url || url_len == 0) return false;

    while (url_len > 0 && isspace((unsigned char)url[0])) { url++; url_len--; }
    while (url_len > 0 && isspace((unsigned char)url[url_len - 1])) { url_len--; }
    if (url_len == 0 || url[0] == '#') return false;

    // Exclude external URLs
    if ((url_len >= 7 && strncasecmp(url, "http://", 7) == 0) ||
        (url_len >= 8 && strncasecmp(url, "https://", 8) == 0) ||
        (url_len >= 7 && strncasecmp(url, "mailto:", 7) == 0) ||
        (url_len >= 6 && strncasecmp(url, "ftp://", 6) == 0) ||
        (url_len >= 4 && strncasecmp(url, "tel:", 4) == 0) ||
        (url_len >= 5 && strncasecmp(url, "data:", 5) == 0) ||
        (url_len >= 2 && strncmp(url, "//", 2) == 0)) {
        return false;
    }

    const char *hash = (const char *)memchr(url, '#', url_len);
    size_t path_len = hash ? (size_t)(hash - url) : url_len;
    if (path_len == 0) return false;

    char file_buf[PATH_MAX];
    if (path_len >= sizeof(file_buf)) path_len = sizeof(file_buf) - 1;
    memcpy(file_buf, url, path_len);
    file_buf[path_len] = '\0';

    if (!has_markdown_ext(file_buf)) return false;

    if (out_file && file_cap > 0) {
        snprintf(out_file, file_cap, "%s", file_buf);
    }
    if (hash && out_frag && frag_cap > 0) {
        size_t flen = url_len - (size_t)(hash - url) - 1;
        if (flen >= frag_cap) flen = frag_cap - 1;
        memcpy(out_frag, hash + 1, flen);
        out_frag[flen] = '\0';
    }
    return true;
}

/** @brief Resolve rel_target relative to base_dir into a canonicalized absolute path. */
static void resolve_against_dir(const char *base_dir, const char *rel_target,
                                char *out_abs, size_t out_cap) {
    char combined[PATH_MAX * 2];
    if (rel_target[0] == '/') {
        snprintf(combined, sizeof(combined), "%s", rel_target);
    } else if (strcmp(base_dir, "/") == 0) {
        snprintf(combined, sizeof(combined), "/%s", rel_target);
    } else {
        snprintf(combined, sizeof(combined), "%s/%s", base_dir, rel_target);
    }
    canonicalize_path(combined, out_abs, out_cap);
}

/** @brief Look up abs_path in list and return its node index, or -1 if absent. */
static ssize_t export_doc_list_find_abs(const ExportDocList *list, const char *abs_path) {
    if (!list || !abs_path || !abs_path[0]) return -1;
    for (size_t i = 0; i < list->count; ++i) {
        if (list->nodes[i].abs_path[0] && strcmp(list->nodes[i].abs_path, abs_path) == 0) {
            return (ssize_t)i;
        }
    }
    return -1;
}

/** @brief Append a document node to list with a stable anchor ID and extracted title. */
static ssize_t export_doc_list_add(ExportDocList *list, const Buffer *buf,
                                   Buffer *owned_buf, const char *abs_path) {
    if (list->count >= MDE_EXPORT_MAX_DOCS) return -1;
    if (list->count >= list->cap) {
        size_t new_cap = list->cap == 0 ? 8 : list->cap * 2;
        ExportDocNode *nn = (ExportDocNode *)realloc(list->nodes, new_cap * sizeof(ExportDocNode));
        if (!nn) return -1;
        list->nodes = nn;
        list->cap = new_cap;
    }

    size_t idx = list->count++;
    ExportDocNode *node = &list->nodes[idx];
    memset(node, 0, sizeof(*node));
    node->buf = buf;
    node->owned_buf = owned_buf;
    if (abs_path && abs_path[0]) {
        snprintf(node->abs_path, sizeof(node->abs_path), "%s", abs_path);
        const char *slash = strrchr(abs_path, '/');
        snprintf(node->display_name, sizeof(node->display_name), "%s", slash ? slash + 1 : abs_path);
    } else {
        snprintf(node->display_name, sizeof(node->display_name), "untitled.md");
    }
    dir_from_path(node->abs_path, node->base_dir, sizeof(node->base_dir));
    snprintf(node->anchor_id, sizeof(node->anchor_id), "mde-doc-%zu", idx);
    extract_doc_title(buf, node->title, sizeof(node->title));
    return (ssize_t)idx;
}

/** @brief Recursively discover and load locally linked Markdown files in DFS order without cycles. */
static void collect_linked_recursive(ExportDocList *list, size_t doc_idx) {
    if (!list || doc_idx >= list->count) return;

    const Buffer *buf = list->nodes[doc_idx].buf;
    char base_dir[PATH_MAX];
    snprintf(base_dir, sizeof(base_dir), "%s", list->nodes[doc_idx].base_dir);

    bool in_code = false;
    char lang[32] = {0};
    size_t nlines = buffer_line_count(buf);

    for (size_t r = 0; r < nlines; ++r) {
        const char *raw = buffer_line(buf, r);
        size_t len = buffer_line_len(buf, r);

        MdParsedLine pl;
        md_parse_line(raw, len, &in_code, lang, sizeof(lang), &pl);
        if (pl.is_code_block) {
            md_parsed_line_free(&pl);
            continue;
        }

        for (size_t s = 0; s < pl.span_count; ++s) {
            // Skip image links (![alt](url))
            if (pl.spans[s].type == MD_IMAGE_MARKER) {
                s++;
                while (s < pl.span_count &&
                       (pl.spans[s].type == MD_LINK_BRACKET ||
                        pl.spans[s].type == MD_LINK_TEXT ||
                        pl.spans[s].type == MD_LINK_URL_PAREN ||
                        pl.spans[s].type == MD_LINK_URL)) {
                    if (pl.spans[s].type == MD_LINK_URL_PAREN && (raw[pl.spans[s].start] == ')' || raw[pl.spans[s].start] == ']')) break;
                    s++;
                }
                continue;
            }

            if (pl.spans[s].type == MD_LINK_URL) {
                const char *url = raw + pl.spans[s].start;
                size_t url_len = pl.spans[s].end - pl.spans[s].start;
                char file_part[PATH_MAX];
                if (is_local_md_target(url, url_len, file_part, sizeof(file_part), NULL, 0)) {
                    char target_abs[PATH_MAX];
                    resolve_against_dir(base_dir, file_part, target_abs, sizeof(target_abs));

                    if (export_doc_list_find_abs(list, target_abs) >= 0) {
                        continue; // Already visited (prevents cycles & duplicates)
                    }

                    struct stat st;
                    if (stat(target_abs, &st) == 0 && S_ISREG(st.st_mode)) {
                        Buffer *child = (Buffer *)malloc(sizeof(Buffer));
                        if (!child) continue;
                        buffer_init(child);
                        if (buffer_load(child, target_abs)) {
                            ssize_t new_idx = export_doc_list_add(list, child, child, target_abs);
                            if (new_idx >= 0) {
                                collect_linked_recursive(list, (size_t)new_idx);
                            } else {
                                buffer_free(child);
                                free(child);
                            }
                        } else {
                            buffer_free(child);
                            free(child);
                        }
                    }
                }
            }
        }

        md_parsed_line_free(&pl);
    }
}

/** @brief Populate list with root (node 0) and optionally all recursively linked Markdown files. */
bool export_doc_list_collect(ExportDocList *list, const Buffer *root, bool include_linked) {
    export_doc_list_init(list);
    if (!root) return false;

    char root_abs[PATH_MAX] = {0};
    if (root->file_path && root->file_path[0]) {
        canonicalize_path(root->file_path, root_abs, sizeof(root_abs));
    }

    if (export_doc_list_add(list, root, NULL, root_abs) < 0) {
        return false;
    }

    if (include_linked) {
        collect_linked_recursive(list, 0);
    }
    return true;
}

/** @brief Resolve a link URL from node from_idx to its target node index in list, or -1. */
ssize_t find_linked_doc_index(const ExportDocList *list, size_t from_idx,
                              const char *url, size_t url_len,
                              char *out_frag, size_t frag_cap) {
    if (!list || list->count <= 1 || from_idx >= list->count) return -1;

    char file_part[PATH_MAX];
    if (!is_local_md_target(url, url_len, file_part, sizeof(file_part), out_frag, frag_cap)) {
        return -1;
    }

    char target_abs[PATH_MAX];
    resolve_against_dir(list->nodes[from_idx].base_dir, file_part, target_abs, sizeof(target_abs));
    return export_doc_list_find_abs(list, target_abs);
}

/** @brief Convert heading text into a lowercase hyphenated URL anchor slug. */
void slugify_heading(const char *text, size_t len, char *out_slug, size_t cap) {
    if (!out_slug || cap == 0) return;
    size_t pos = 0;
    bool prev_dash = true;
    for (size_t i = 0; i < len && pos + 1 < cap; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (isalnum(c)) {
            out_slug[pos++] = (char)tolower(c);
            prev_dash = false;
        } else if ((c == ' ' || c == '-' || c == '_') && !prev_dash) {
            out_slug[pos++] = '-';
            prev_dash = true;
        }
    }
    while (pos > 0 && out_slug[pos - 1] == '-') pos--;
    out_slug[pos] = '\0';
}

/** @brief Count distinct local Markdown files recursively linked from buf. */
size_t export_count_linked_files(const Buffer *buf) {
    if (!buf) return 0;
    ExportDocList docs;
    if (!export_doc_list_collect(&docs, buf, true)) return 0;
    size_t linked = docs.count > 1 ? (docs.count - 1) : 0;
    export_doc_list_free(&docs);
    return linked;
}

/* =========================================================================
 * External Pandoc Backend (fork + execvp, no shell, multi-file aware)
 * ========================================================================= */

/** @brief Write a single line to a Pandoc temp file, rewriting local linked `.md` URLs to anchors. */
static void write_pandoc_rewritten_line(FILE *tf, const char *raw, size_t len,
                                        const ExportDocList *docs, size_t doc_idx,
                                        bool *in_code, char *lang, size_t lang_cap) {
    MdParsedLine pl;
    md_parse_line(raw, len, in_code, lang, lang_cap, &pl);

    if (pl.is_code_block || docs->count <= 1) {
        if (len > 0) fwrite(raw, 1, len, tf);
        md_parsed_line_free(&pl);
        return;
    }

    size_t cursor = 0;
    for (size_t s = 0; s < pl.span_count; ++s) {
        // Leave images untouched
        if (pl.spans[s].type == MD_IMAGE_MARKER) {
            s++;
            while (s < pl.span_count &&
                   (pl.spans[s].type == MD_LINK_BRACKET ||
                    pl.spans[s].type == MD_LINK_TEXT ||
                    pl.spans[s].type == MD_LINK_URL_PAREN ||
                    pl.spans[s].type == MD_LINK_URL)) {
                if (pl.spans[s].type == MD_LINK_URL_PAREN && (raw[pl.spans[s].start] == ')' || raw[pl.spans[s].start] == ']')) break;
                s++;
            }
            continue;
        }

        if (pl.spans[s].type == MD_LINK_URL) {
            const char *url = raw + pl.spans[s].start;
            size_t ulen = pl.spans[s].end - pl.spans[s].start;
            char frag[128] = {0};
            ssize_t target_idx = find_linked_doc_index(docs, doc_idx, url, ulen, frag, sizeof(frag));
            if (target_idx >= 0) {
                if (pl.spans[s].start > cursor) {
                    fwrite(raw + cursor, 1, pl.spans[s].start - cursor, tf);
                }
                if (frag[0]) {
                    char slug[128];
                    slugify_heading(frag, strlen(frag), slug, sizeof(slug));
                    fprintf(tf, "#%s", slug[0] ? slug : docs->nodes[target_idx].anchor_id);
                } else {
                    fprintf(tf, "#%s", docs->nodes[target_idx].anchor_id);
                }
                cursor = pl.spans[s].end;
            }
        }
    }

    if (cursor < len) {
        fwrite(raw + cursor, 1, len - cursor, tf);
    }
    md_parsed_line_free(&pl);
}

/** @brief Materialize a document node into a temporary Markdown file with Pandoc anchors. */
static bool write_pandoc_temp_file(const ExportDocList *docs, size_t doc_idx,
                                   char *out_tmp_path, size_t path_cap) {
    char tmp_template[] = "/tmp/mde_export_XXXXXX";
    int tmp_fd = mkstemp(tmp_template);
    if (tmp_fd < 0) return false;

    FILE *tf = fdopen(tmp_fd, "wb");
    if (!tf) {
        close(tmp_fd);
        unlink(tmp_template);
        return false;
    }

    const Buffer *buf = docs->nodes[doc_idx].buf;
    size_t line_count = buffer_line_count(buf);
    bool in_code = false;
    char lang[32] = {0};
    bool anchor_emitted = (docs->count <= 1);

    for (size_t i = 0; i < line_count; ++i) {
        const char *l = buffer_line(buf, i);
        size_t len = buffer_line_len(buf, i);

        // When combining multiple documents, attach a stable Pandoc anchor ID to each document
        if (!anchor_emitted && len > 0) {
            bool probe_code = in_code;
            char probe_lang[32];
            snprintf(probe_lang, sizeof(probe_lang), "%s", lang);
            MdParsedLine probe;
            md_parse_line(l, len, &probe_code, probe_lang, sizeof(probe_lang), &probe);
            bool is_heading = (probe.header_level > 0);
            md_parsed_line_free(&probe);

            if (is_heading) {
                write_pandoc_rewritten_line(tf, l, len, docs, doc_idx, &in_code, lang, sizeof(lang));
                fprintf(tf, " {#%s}\n", docs->nodes[doc_idx].anchor_id);
                anchor_emitted = true;
                continue;
            } else {
                fprintf(tf, "[]{#%s}\n\n", docs->nodes[doc_idx].anchor_id);
                anchor_emitted = true;
            }
        }

        write_pandoc_rewritten_line(tf, l, len, docs, doc_idx, &in_code, lang, sizeof(lang));
        fputc('\n', tf);
    }

    if (!anchor_emitted) {
        fprintf(tf, "[]{#%s}\n", docs->nodes[doc_idx].anchor_id);
    }

    fclose(tf);
    snprintf(out_tmp_path, path_cap, "%s", tmp_template);
    return true;
}

/** @brief Export all collected document nodes via external pandoc using fork/execvp. */
static bool export_via_pandoc(const ExportDocList *docs, ExportFormat fmt, const char *out_path,
                              char *err_msg, size_t err_cap) {
    if (!docs || docs->count == 0) {
        set_str(err_msg, err_cap, "No documents to export");
        return false;
    }

    char (*tmp_files)[64] = calloc(docs->count, sizeof(*tmp_files));
    if (!tmp_files) {
        set_str(err_msg, err_cap, "Out of memory preparing pandoc export");
        return false;
    }

    size_t created = 0;
    for (size_t i = 0; i < docs->count; ++i) {
        if (!write_pandoc_temp_file(docs, i, tmp_files[i], sizeof(tmp_files[i]))) {
            for (size_t j = 0; j < created; ++j) unlink(tmp_files[j]);
            free(tmp_files);
            set_str(err_msg, err_cap, "Failed to create temporary file for pandoc");
            return false;
        }
        created++;
    }

    // Build colon-separated --resource-path from all unique document directories
    char resource_path[PATH_MAX * 2] = {0};
    size_t rp_pos = 0;
    for (size_t i = 0; i < docs->count; ++i) {
        const char *dir = docs->nodes[i].base_dir;
        if (!dir[0]) continue;
        bool dup = false;
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(docs->nodes[j].base_dir, dir) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            int n = snprintf(resource_path + rp_pos, sizeof(resource_path) - rp_pos,
                             "%s%s", rp_pos > 0 ? ":" : "", dir);
            if (n > 0 && rp_pos + (size_t)n < sizeof(resource_path)) {
                rp_pos += (size_t)n;
            }
        }
    }

    const char *pandoc_to = "html5";
    bool standalone = true;
    switch (fmt) {
        case EXPORT_HTML:  pandoc_to = "html5";    standalone = true;  break;
        case EXPORT_XML:   pandoc_to = "docbook5"; standalone = true;  break;
        case EXPORT_EPUB:  pandoc_to = "epub3";    standalone = false; break;
        case EXPORT_LATEX: pandoc_to = "latex";    standalone = true;  break;
        default: break;
    }

    char meta_title[160];
    snprintf(meta_title, sizeof(meta_title), "title=%s", docs->nodes[0].title);

    int err_pipe[2];
    if (pipe(err_pipe) != 0) {
        for (size_t j = 0; j < created; ++j) unlink(tmp_files[j]);
        free(tmp_files);
        set_str(err_msg, err_cap, "Failed to create pipe for pandoc");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(err_pipe[0]);
        close(err_pipe[1]);
        for (size_t j = 0; j < created; ++j) unlink(tmp_files[j]);
        free(tmp_files);
        set_str(err_msg, err_cap, "Failed to fork pandoc process");
        return false;
    }

    if (pid == 0) {
        // Child process: redirect stdout to /dev/null and stderr to pipe
        close(err_pipe[0]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            close(devnull);
        }
        dup2(err_pipe[1], STDERR_FILENO);
        close(err_pipe[1]);

        size_t max_args = 16 + docs->count;
        char **argv = (char **)calloc(max_args, sizeof(char *));
        if (!argv) _exit(126);

        size_t argc = 0;
        argv[argc++] = "pandoc";
        argv[argc++] = "-f";
        argv[argc++] = "markdown";
        argv[argc++] = "-t";
        argv[argc++] = (char *)pandoc_to;
        if (standalone) {
            argv[argc++] = "-s";
        }
        argv[argc++] = "--metadata";
        argv[argc++] = meta_title;
        if (resource_path[0]) {
            argv[argc++] = "--resource-path";
            argv[argc++] = resource_path;
        }
        argv[argc++] = "-o";
        argv[argc++] = (char *)out_path;
        for (size_t i = 0; i < docs->count; ++i) {
            argv[argc++] = tmp_files[i];
        }
        argv[argc] = NULL;

        execvp("pandoc", argv);
        _exit(127);
    }

    // Parent process
    close(err_pipe[1]);
    char stderr_buf[512];
    size_t err_pos = 0;
    ssize_t n;
    while ((n = read(err_pipe[0], stderr_buf + err_pos, sizeof(stderr_buf) - 1 - err_pos)) > 0) {
        err_pos += (size_t)n;
        if (err_pos >= sizeof(stderr_buf) - 1) break;
    }
    stderr_buf[err_pos] = '\0';
    close(err_pipe[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}

    for (size_t j = 0; j < created; ++j) unlink(tmp_files[j]);
    free(tmp_files);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return true;
    }

    // Trim stderr to first line for concise status message
    char *nl = strchr(stderr_buf, '\n');
    if (nl) *nl = '\0';
    if (stderr_buf[0]) {
        char formatted[256];
        snprintf(formatted, sizeof(formatted), "pandoc: %.200s", stderr_buf);
        set_str(err_msg, err_cap, formatted);
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        set_str(err_msg, err_cap, "pandoc not found in PATH");
    } else {
        set_str(err_msg, err_cap, "pandoc exited with an error");
    }
    return false;
}

/* =========================================================================
 * Helper Utilities for Built-In Exporters
 * ========================================================================= */

/**
 * @brief Return true if a token is a raw Markdown delimiter omitted from exported output.
 */
bool is_skipped_export_marker(MdTokenType type) {
    return md_is_header_marker(type) ||
           type == MD_BLOCKQUOTE_MARKER ||
           type == MD_LIST_BULLET ||
           type == MD_TASK_UNCHECKED ||
           type == MD_TASK_CHECKED ||
           type == MD_BOLD_MARKER ||
           type == MD_ITALIC_MARKER ||
           type == MD_BOLD_ITALIC_MARKER ||
           type == MD_STRIKE_MARKER ||
           type == MD_INLINE_CODE_MARKER ||
           type == MD_TABLE_BORDER || type == MD_FOOTNOTE_DEF;
}

/**
 * @brief Extract label/alt text and destination URL from a link or image span sequence.
 *
 * Advances past the closing ')' span and returns the next span index to process.
 */
size_t extract_link_or_image_span(const char *raw, const MdParsedLine *pl,
                                  size_t start_idx, size_t end_idx,
                                  const char **out_label, size_t *out_label_len,
                                  const char **out_url, size_t *out_url_len) {
    *out_label = "";
    *out_label_len = 0;
    *out_url = "";
    *out_url_len = 0;

    size_t k = start_idx + 1;
    while (k < end_idx &&
           (pl->spans[k].type == MD_LINK_BRACKET ||
            pl->spans[k].type == MD_LINK_TEXT ||
            pl->spans[k].type == MD_LINK_URL_PAREN ||
            pl->spans[k].type == MD_LINK_URL)) {
        if (pl->spans[k].type == MD_LINK_TEXT) {
            *out_label = raw + pl->spans[k].start;
            *out_label_len = pl->spans[k].end - pl->spans[k].start;
        } else if (pl->spans[k].type == MD_LINK_URL) {
            *out_url = raw + pl->spans[k].start;
            *out_url_len = pl->spans[k].end - pl->spans[k].start;
        }
        bool done = (pl->spans[k].type == MD_LINK_URL_PAREN && (raw[pl->spans[k].start] == ')' || raw[pl->spans[k].start] == ']'));
        k++;
        if (done) break;
    }
    return k;
}

/** @brief Test whether a line is a Markdown table alignment separator row (e.g. `| --- | --- |`). */
bool is_table_sep_row(const char *line, size_t len) {
    bool has_dash = false;
    bool has_pipe = false;
    for (size_t i = 0; i < len; ++i) {
        char c = line[i];
        if (c == '-') has_dash = true;
        else if (c == '|') has_pipe = true;
        else if (c != ':' && !isspace((unsigned char)c)) return false;
    }
    return has_dash && has_pipe;
}

/** @brief Count pipe-delimited columns in a Markdown table row. */
size_t count_table_cols(const char *line, size_t len) {
    size_t i = 0;
    while (i < len && isspace((unsigned char)line[i])) i++;
    if (i < len && line[i] == '|') i++;
    size_t end = len;
    while (end > i && isspace((unsigned char)line[end - 1])) end--;
    if (end > i && line[end - 1] == '|') end--;

    size_t cols = 1;
    for (; i < end; ++i) {
        if (line[i] == '|') cols++;
    }
    return cols;
}

/** @brief Return true if a list line starts with an ordered numeric bullet (e.g. `1.`). */
bool is_ordered_bullet(const char *line, size_t len) {
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    return (i < len && isdigit((unsigned char)line[i]));
}

/* =========================================================================
 * Master Export Dispatch
 * ========================================================================= */

/** @brief Master export dispatcher supporting Pandoc/built-in engines and linked-file bundling. */
bool export_document_ex(const Buffer *buf, ExportFormat fmt, const char *out_path,
                        ExportEngine engine, bool include_linked, size_t *out_linked_count,
                        char *out_engine_used, size_t engine_cap,
                        char *err_msg, size_t err_cap) {
    if (out_linked_count) *out_linked_count = 0;

    if (!buf || !out_path || !out_path[0]) {
        set_str(err_msg, err_cap, "Invalid buffer or empty output path");
        return false;
    }

    bool has_pandoc = export_has_pandoc();

    bool use_pandoc = false;
    if (engine == EXPORT_ENGINE_PANDOC) {
        if (!has_pandoc) {
            set_str(err_msg, err_cap, "pandoc is not installed on PATH");
            return false;
        }
        use_pandoc = true;
    } else if (engine == EXPORT_ENGINE_AUTO) {
        use_pandoc = has_pandoc;
    }

    ExportDocList docs;
    if (!export_doc_list_collect(&docs, buf, include_linked)) {
        set_str(err_msg, err_cap, "Failed to collect document sources");
        return false;
    }

    if (out_linked_count) {
        *out_linked_count = docs.count > 1 ? (docs.count - 1) : 0;
    }

    bool ok = false;
    if (use_pandoc) {
        set_str(out_engine_used, engine_cap, "pandoc");
        ok = export_via_pandoc(&docs, fmt, out_path, err_msg, err_cap);
    } else {
        set_str(out_engine_used, engine_cap, "built-in");
        switch (fmt) {
            case EXPORT_HTML:  ok = export_builtin_html(&docs, out_path, err_msg, err_cap); break;
            case EXPORT_XML:   ok = export_builtin_xml(&docs, out_path, err_msg, err_cap); break;
            case EXPORT_EPUB:  ok = export_builtin_epub(&docs, out_path, NULL, err_msg, err_cap); break;
            case EXPORT_LATEX: ok = export_builtin_latex(&docs, out_path, err_msg, err_cap); break;
            default:
                set_str(err_msg, err_cap, "Unsupported export format");
                ok = false;
                break;
        }
    }

    export_doc_list_free(&docs);
    return ok;
}

/** @brief Export buf (including all locally linked Markdown files) to out_path. */
bool export_document(const Buffer *buf, ExportFormat fmt, const char *out_path,
                     ExportEngine engine, char *out_engine_used, size_t engine_cap,
                     char *err_msg, size_t err_cap) {
    return export_document_ex(buf, fmt, out_path, engine, true, NULL,
                              out_engine_used, engine_cap, err_msg, err_cap);
}
