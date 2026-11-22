/**
 * @file editor_link.c
 * @brief Hypertext file linking, navigation history, and anchor resolution.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "editor.h"
#include "markdown.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

/* =========================================================================
 * Hypertext File Linking & Navigation
 * ========================================================================= */

/** @brief Return true if url begins with an external URI scheme (http, https, mailto, etc.). */
static bool is_external_url(const char *url) {
    if (!url || !url[0]) return false;
    if (strncmp(url, "http://", 7) == 0 ||
        strncmp(url, "https://", 8) == 0 ||
        strncmp(url, "mailto:", 7) == 0 ||
        strncmp(url, "ftp://", 6) == 0 ||
        strncmp(url, "tel:", 4) == 0 ||
        strncmp(url, "data:", 5) == 0 ||
        strncmp(url, "//", 2) == 0) {
        return true;
    }
    return false;
}

/**
 * @brief Normalize an absolute path lexically (collapsing '.' and '..' components).
 */
static void normalize_abs_path(const char *in_path, char *out_path, size_t out_cap) {
    if (!in_path || !out_path || out_cap == 0) return;

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
    size_t part_count = 0;
    char *copy = strdup(abs_buf);
    if (!copy) {
        snprintf(out_path, out_cap, "%s", abs_buf);
        return;
    }

    char *saveptr = NULL;
    char *tok = strtok_r(copy, "/", &saveptr);
    while (tok) {
        if (strcmp(tok, ".") == 0 || tok[0] == '\0') {
            // Skip
        } else if (strcmp(tok, "..") == 0) {
            if (part_count > 0) part_count--;
        } else if (part_count < 256) {
            parts[part_count++] = tok;
        }
        tok = strtok_r(NULL, "/", &saveptr);
    }

    size_t pos = 0;
    if (part_count == 0) {
        snprintf(out_path, out_cap, "/");
    } else {
        out_path[0] = '\0';
        for (size_t i = 0; i < part_count; ++i) {
            int n = snprintf(out_path + pos, out_cap - pos, "/%s", parts[i]);
            if (n < 0 || pos + (size_t)n >= out_cap) break;
            pos += (size_t)n;
        }
    }
    free(copy);
}

/** @brief Extract the parent directory of file_path (or CWD when untitled). */
static void get_base_dir(const char *file_path, char *out_dir, size_t out_cap) {
    if (!out_dir || out_cap == 0) return;
    if (file_path && file_path[0]) {
        char norm[PATH_MAX];
        normalize_abs_path(file_path, norm, sizeof(norm));
        char *slash = strrchr(norm, '/');
        if (slash == norm) {
            snprintf(out_dir, out_cap, "/");
        } else if (slash) {
            *slash = '\0';
            snprintf(out_dir, out_cap, "%s", norm);
        } else {
            snprintf(out_dir, out_cap, "/");
        }
    } else {
        if (!getcwd(out_dir, out_cap)) {
            snprintf(out_dir, out_cap, ".");
        }
    }
}

/** @brief Compute a portable relative path from from_file's directory to to_file. */
bool editor_make_relative_path(const char *from_file, const char *to_file,
                               char *out_rel, size_t out_cap) {
    if (!to_file || !to_file[0] || !out_rel || out_cap == 0) return false;

    char from_dir[PATH_MAX];
    char to_abs[PATH_MAX];
    get_base_dir(from_file, from_dir, sizeof(from_dir));
    normalize_abs_path(to_file, to_abs, sizeof(to_abs));

    char *from_copy = strdup(from_dir);
    char *to_copy = strdup(to_abs);
    if (!from_copy || !to_copy) {
        free(from_copy);
        free(to_copy);
        snprintf(out_rel, out_cap, "%s", to_file);
        return true;
    }

    char *from_parts[256];
    char *to_parts[256];
    size_t nf = 0, nt = 0;
    char *sp1 = NULL, *sp2 = NULL;

    for (char *t = strtok_r(from_copy, "/", &sp1); t && nf < 256; t = strtok_r(NULL, "/", &sp1)) {
        if (t[0]) from_parts[nf++] = t;
    }
    for (char *t = strtok_r(to_copy, "/", &sp2); t && nt < 256; t = strtok_r(NULL, "/", &sp2)) {
        if (t[0]) to_parts[nt++] = t;
    }

    size_t common = 0;
    while (common < nf && common < nt && strcmp(from_parts[common], to_parts[common]) == 0) {
        common++;
    }

    size_t pos = 0;
    out_rel[0] = '\0';
    for (size_t i = common; i < nf; ++i) {
        int n = snprintf(out_rel + pos, out_cap - pos, "%s..", pos > 0 ? "/" : "");
        if (n < 0 || pos + (size_t)n >= out_cap) break;
        pos += (size_t)n;
    }
    for (size_t i = common; i < nt; ++i) {
        int n = snprintf(out_rel + pos, out_cap - pos, "%s%s", pos > 0 ? "/" : "", to_parts[i]);
        if (n < 0 || pos + (size_t)n >= out_cap) break;
        pos += (size_t)n;
    }

    if (out_rel[0] == '\0') {
        snprintf(out_rel, out_cap, ".");
    }

    free(from_copy);
    free(to_copy);
    return true;
}

/** @brief Convert heading text into a lowercase hyphenated anchor slug. */
static void make_heading_slug(const char *text, size_t len, char *out_slug, size_t cap) {
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

/** @brief Scan the active buffer for a heading matching anchor slug and jump to it. */
static bool jump_to_heading_anchor(Editor *ed, const char *anchor) {
    if (!ed || !anchor || !anchor[0]) return false;

    char target_slug[128];
    make_heading_slug(anchor, strlen(anchor), target_slug, sizeof(target_slug));
    if (!target_slug[0]) return false;

    bool in_code = false;
    char lang[32] = {0};
    size_t total = buffer_line_count(&ed->buffer);
    for (size_t r = 0; r < total; ++r) {
        const char *l = buffer_line(&ed->buffer, r);
        size_t len = buffer_line_len(&ed->buffer, r);
        MdParsedLine pl;
        md_parse_line(l, len, &in_code, lang, sizeof(lang), &pl);
        if (pl.header_level > 0) {
            char heading_txt[256];
            size_t hpos = 0;
            for (size_t s = 0; s < pl.span_count; ++s) {
                if (md_is_header_marker(pl.spans[s].type) ||
                    pl.spans[s].type == MD_BOLD_MARKER ||
                    pl.spans[s].type == MD_ITALIC_MARKER ||
                    pl.spans[s].type == MD_INLINE_CODE_MARKER) {
                    continue;
                }
                size_t slen = pl.spans[s].end - pl.spans[s].start;
                if (hpos + slen >= sizeof(heading_txt) - 1) slen = sizeof(heading_txt) - 1 - hpos;
                if (slen > 0) {
                    memcpy(heading_txt + hpos, l + pl.spans[s].start, slen);
                    hpos += slen;
                }
            }
            heading_txt[hpos] = '\0';
            char slug[128];
            make_heading_slug(heading_txt, hpos, slug, sizeof(slug));
            md_parsed_line_free(&pl);
            if (strcmp(slug, target_slug) == 0) {
                ed->cursor_row = r;
                ed->cursor_col = 0;
                ed->target_visual_col = 0;
                return true;
            }
        } else {
            md_parsed_line_free(&pl);
        }
    }
    return false;
}

/** @brief Extract the Markdown link URL and optional `#anchor` at the cursor or on the active line. */
bool editor_extract_link_at_cursor(const Editor *ed,
                                   char *out_url, size_t url_cap,
                                   char *out_anchor, size_t anchor_cap) {
    if (!ed || !out_url || url_cap == 0) return false;
    out_url[0] = '\0';
    if (out_anchor && anchor_cap > 0) out_anchor[0] = '\0';

    size_t row = ed->cursor_row;
    if (row >= buffer_line_count(&ed->buffer)) return false;

    // Determine code-block state up to cursor_row
    bool in_code = false;
    char lang[32] = {0};
    for (size_t r = 0; r < row; ++r) {
        const char *l = buffer_line(&ed->buffer, r);
        size_t len = buffer_line_len(&ed->buffer, r);
        MdParsedLine tmp;
        md_parse_line(l, len, &in_code, lang, sizeof(lang), &tmp);
        md_parsed_line_free(&tmp);
    }

    const char *line = buffer_line(&ed->buffer, row);
    size_t len = buffer_line_len(&ed->buffer, row);
    MdParsedLine pl;
    md_parse_line(line, len, &in_code, lang, sizeof(lang), &pl);

    if (pl.is_code_block) {
        md_parsed_line_free(&pl);
        return false;
    }

    const char *best_url = NULL;
    size_t best_url_len = 0;
    const char *first_url = NULL;
    size_t first_url_len = 0;

    for (size_t s = 0; s < pl.span_count; ++s) {
        // Skip images (![alt](url))
        if (pl.spans[s].type == MD_IMAGE_MARKER) {
            s++;
            while (s < pl.span_count &&
                   (pl.spans[s].type == MD_LINK_BRACKET ||
                    pl.spans[s].type == MD_LINK_TEXT ||
                    pl.spans[s].type == MD_LINK_URL_PAREN ||
                    pl.spans[s].type == MD_LINK_URL)) {
                bool end_p = (pl.spans[s].type == MD_LINK_URL_PAREN && line[pl.spans[s].start] == ')');
                if (end_p) break;
                s++;
            }
            continue;
        }

        if (pl.spans[s].type == MD_LINK_BRACKET && line[pl.spans[s].start] == '[') {
            size_t link_start = pl.spans[s].start;
            size_t link_end = pl.spans[s].end;
            const char *url_ptr = NULL;
            size_t url_len = 0;
            size_t k = s + 1;
            while (k < pl.span_count &&
                   (pl.spans[k].type == MD_LINK_BRACKET ||
                    pl.spans[k].type == MD_LINK_TEXT ||
                    pl.spans[k].type == MD_LINK_URL_PAREN ||
                    pl.spans[k].type == MD_LINK_URL)) {
                link_end = pl.spans[k].end;
                if (pl.spans[k].type == MD_LINK_URL) {
                    url_ptr = line + pl.spans[k].start;
                    url_len = pl.spans[k].end - pl.spans[k].start;
                }
                bool end_p = (pl.spans[k].type == MD_LINK_URL_PAREN && line[pl.spans[k].start] == ')');
                k++;
                if (end_p) break;
            }
            if (url_ptr && url_len > 0) {
                if (!first_url) {
                    first_url = url_ptr;
                    first_url_len = url_len;
                }
                if (ed->cursor_col >= link_start && ed->cursor_col <= link_end) {
                    best_url = url_ptr;
                    best_url_len = url_len;
                    break;
                }
            }
            s = k - 1;
        }
    }

    const char *chosen = best_url ? best_url : first_url;
    size_t chosen_len = best_url ? best_url_len : first_url_len;

    if (!chosen || chosen_len == 0) {
        md_parsed_line_free(&pl);
        return false;
    }

    // Trim surrounding whitespace
    while (chosen_len > 0 && isspace((unsigned char)chosen[0])) { chosen++; chosen_len--; }
    while (chosen_len > 0 && isspace((unsigned char)chosen[chosen_len - 1])) { chosen_len--; }

    // Split optional #anchor fragment
    const char *hash = (const char *)memchr(chosen, '#', chosen_len);
    if (hash) {
        size_t path_part_len = (size_t)(hash - chosen);
        size_t anc_len = chosen_len - path_part_len - 1;
        if (path_part_len >= url_cap) path_part_len = url_cap - 1;
        memcpy(out_url, chosen, path_part_len);
        out_url[path_part_len] = '\0';

        if (out_anchor && anchor_cap > 0) {
            if (anc_len >= anchor_cap) anc_len = anchor_cap - 1;
            memcpy(out_anchor, hash + 1, anc_len);
            out_anchor[anc_len] = '\0';
        }
    } else {
        if (chosen_len >= url_cap) chosen_len = url_cap - 1;
        memcpy(out_url, chosen, chosen_len);
        out_url[chosen_len] = '\0';
    }

    md_parsed_line_free(&pl);
    return true;
}

/** @brief Resolve a local relative or absolute link target against the active file's directory. */
bool editor_resolve_link_path(const Editor *ed, const char *link_target,
                              char *out_path, size_t out_cap) {
    if (!ed || !link_target || !out_path || out_cap == 0) return false;
    if (is_external_url(link_target)) return false;

    if (!link_target[0]) {
        if (ed->buffer.file_path && ed->buffer.file_path[0]) {
            normalize_abs_path(ed->buffer.file_path, out_path, out_cap);
        } else {
            out_path[0] = '\0';
        }
        return true;
    }

    char combined[PATH_MAX * 2];
    if (link_target[0] == '/') {
        snprintf(combined, sizeof(combined), "%s", link_target);
    } else {
        char base_dir[PATH_MAX];
        get_base_dir(ed->buffer.file_path, base_dir, sizeof(base_dir));
        if (strcmp(base_dir, "/") == 0) {
            snprintf(combined, sizeof(combined), "/%s", link_target);
        } else {
            snprintf(combined, sizeof(combined), "%s/%s", base_dir, link_target);
        }
    }

    normalize_abs_path(combined, out_path, out_cap);
    return true;
}

/** @brief Derive a default link label from target_path's first H1 heading or filename stem. */
static void derive_link_label(const char *target_path, char *out_label, size_t cap) {
    if (!out_label || cap == 0) return;
    out_label[0] = '\0';

    // Try reading the first H1 heading if the target file exists
    FILE *f = fopen(target_path, "rb");
    if (f) {
        char line[512];
        bool in_code = false;
        char lang[32] = {0};
        for (int i = 0; i < 64 && fgets(line, sizeof(line), f); ++i) {
            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
            MdParsedLine pl;
            md_parse_line(line, l, &in_code, lang, sizeof(lang), &pl);
            if (pl.header_level == 1 && pl.span_count > 1) {
                size_t pos = 0;
                for (size_t s = 0; s < pl.span_count; ++s) {
                    if (md_is_header_marker(pl.spans[s].type)) continue;
                    size_t slen = pl.spans[s].end - pl.spans[s].start;
                    if (pos + slen >= cap - 1) slen = cap - 1 - pos;
                    if (slen > 0) {
                        memcpy(out_label + pos, line + pl.spans[s].start, slen);
                        pos += slen;
                    }
                }
                out_label[pos] = '\0';
                md_parsed_line_free(&pl);
                if (out_label[0]) {
                    fclose(f);
                    return;
                }
            }
            md_parsed_line_free(&pl);
        }
        fclose(f);
    }

    // Fallback to basename without extension
    const char *slash = strrchr(target_path, '/');
    const char *base = slash ? (slash + 1) : target_path;
    snprintf(out_label, cap, "%s", base);
    char *dot = strrchr(out_label, '.');
    if (dot && dot != out_label) *dot = '\0';
    if (!out_label[0]) snprintf(out_label, cap, "link");
}

/** @brief Insert a relative Markdown hyperlink `[Label](relative/path)` at the cursor. */
bool editor_insert_file_link(Editor *ed, const char *target_path) {
    if (!ed || !target_path || !target_path[0]) return false;
    editor_touch_key(ed);

    char rel_path[PATH_MAX];
    if (!editor_make_relative_path(ed->buffer.file_path, target_path, rel_path, sizeof(rel_path))) {
        return false;
    }

    char label[256] = {0};
    uint64_t group = buffer_next_group(&ed->buffer);

    size_t sel_len = 0;
    char *sel_text = editor_get_selected_text(ed, &sel_len);
    if (sel_text && sel_len > 0) {
        // Use single-line portion of selected text as label
        size_t copy_len = sel_len;
        const char *nl = (const char *)memchr(sel_text, '\n', sel_len);
        if (nl) copy_len = (size_t)(nl - sel_text);
        if (copy_len >= sizeof(label)) copy_len = sizeof(label) - 1;
        memcpy(label, sel_text, copy_len);
        label[copy_len] = '\0';
        free(sel_text);
        editor_delete_selection(ed, group);
    } else {
        free(sel_text);
        derive_link_label(target_path, label, sizeof(label));
    }

    char md_link[PATH_MAX + 300];
    int n = snprintf(md_link, sizeof(md_link), "[%s](%s)", label, rel_path);
    if (n <= 0) return false;
    size_t link_len = (size_t)n < sizeof(md_link) ? (size_t)n : sizeof(md_link) - 1;

    buffer_insert_string(&ed->buffer, ed->cursor_row, ed->cursor_col, md_link, link_len, group);
    ed->cursor_col += link_len;
    const char *l = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(l, ed->cursor_col);
    ed->rendered_dirty = true;
    editor_set_status(ed, "Linked: [%s](%s)", label, rel_path);
    return true;
}

/** @brief Push the current file path and cursor position onto the link navigation history stack. */
static void push_link_history(Editor *ed) {
    if (!ed) return;

    if (ed->link_history_count >= MDE_LINK_HISTORY_MAX) {
        free(ed->link_history[0].file_path);
        memmove(&ed->link_history[0], &ed->link_history[1],
                (MDE_LINK_HISTORY_MAX - 1) * sizeof(LinkHistoryEntry));
        ed->link_history_count = MDE_LINK_HISTORY_MAX - 1;
    }

    const char *fpath = (ed->buffer.file_path && ed->buffer.file_path[0]) ? ed->buffer.file_path : "";
    char norm[PATH_MAX] = {0};
    if (fpath[0]) {
        normalize_abs_path(fpath, norm, sizeof(norm));
    }
    ed->link_history[ed->link_history_count].file_path = strdup(norm);
    ed->link_history[ed->link_history_count].cursor_row = ed->cursor_row;
    ed->link_history[ed->link_history_count].cursor_col = ed->cursor_col;
    ed->link_history_count++;
}

/** @brief Return true if following the link at cursor will load a different local file. */
bool editor_link_will_switch_file(const Editor *ed, char *out_resolved, size_t cap) {
    if (out_resolved && cap > 0) out_resolved[0] = '\0';
    if (!ed) return false;

    char url[PATH_MAX] = {0};
    char anchor[128] = {0};
    if (!editor_extract_link_at_cursor(ed, url, sizeof(url), anchor, sizeof(anchor))) {
        return false;
    }

    // Pure #anchor or external URL will not load another file
    if (!url[0] || is_external_url(url)) {
        return false;
    }

    char resolved[PATH_MAX];
    if (!editor_resolve_link_path(ed, url, resolved, sizeof(resolved))) {
        return false;
    }

    // Same-file link (with or without #anchor) does not switch files
    if (ed->buffer.file_path && ed->buffer.file_path[0]) {
        char cur_norm[PATH_MAX];
        normalize_abs_path(ed->buffer.file_path, cur_norm, sizeof(cur_norm));
        if (strcmp(cur_norm, resolved) == 0) {
            return false;
        }
    }

    if (out_resolved && cap > 0) {
        snprintf(out_resolved, cap, "%s", resolved);
    }
    return true;
}

/** @brief Check whether the cursor is on or adjacent to a footnote reference or definition. */
bool editor_is_footnote_at_cursor(const Editor *ed) {
    if (!ed) return false;
    size_t cur_row = ed->cursor_row;
    if (cur_row >= buffer_line_count(&ed->buffer)) return false;

    const char *line = buffer_line(&ed->buffer, cur_row);
    size_t line_len = buffer_line_len(&ed->buffer, cur_row);
    if (!line || line_len == 0) return false;

    // Check for footnote definition: optional whitespace, then "[^...]:"
    size_t indent = 0;
    while (indent < line_len && (line[indent] == ' ' || line[indent] == '\t')) indent++;
    if (indent + 3 < line_len && line[indent] == '[' && line[indent + 1] == '^') {
        const char *cb = memchr(line + indent + 2, ']', line_len - (indent + 2));
        if (cb && (size_t)(cb - line) + 1 < line_len && cb[1] == ':') {
            return true;
        }
    }

    // Check for footnote reference: "[^...]"
    const char *p = line;
    while ((p = strstr(p, "[^")) != NULL) {
        const char *cb = memchr(p + 2, ']', line_len - (size_t)(p + 2 - line));
        if (cb) {
            size_t cb_idx = (size_t)(cb - line);
            if (cb_idx + 1 < line_len && line[cb_idx + 1] == ':') {
                p = cb + 2;
                continue;
            }
            size_t match_col = (size_t)(p - line);
            if (ed->cursor_col >= match_col && ed->cursor_col <= cb_idx + 1) {
                return true;
            }
            p = cb + 1;
        } else {
            p += 2;
        }
    }
    return false;
}

/** @brief Follow footnote: jump from reference to definition, or definition to first reference. */
bool editor_follow_footnote(Editor *ed) {
    if (!ed) return false;
    size_t cur_row = ed->cursor_row;
    if (cur_row >= buffer_line_count(&ed->buffer)) return false;

    const char *line = buffer_line(&ed->buffer, cur_row);
    size_t line_len = buffer_line_len(&ed->buffer, cur_row);
    if (!line || line_len == 0) return false;

    // Case 1: Active line is a footnote definition: optional whitespace + "[^<label>]:"
    size_t indent = 0;
    while (indent < line_len && (line[indent] == ' ' || line[indent] == '\t')) indent++;
    if (indent + 3 < line_len && line[indent] == '[' && line[indent + 1] == '^') {
        const char *cb = memchr(line + indent + 2, ']', line_len - (indent + 2));
        if (cb && (size_t)(cb - line) + 1 < line_len && cb[1] == ':') {
            size_t label_len = (size_t)(cb - (line + indent + 2));
            if (label_len > 0 && label_len < 128) {
                char label[128];
                memcpy(label, line + indent + 2, label_len);
                label[label_len] = '\0';

                // Search document from top for the FIRST reference [^<label>] (not followed by ':')
                char target_ref[140];
                snprintf(target_ref, sizeof(target_ref), "[^%s]", label);
                size_t target_ref_len = strlen(target_ref);

                size_t total_lines = buffer_line_count(&ed->buffer);
                for (size_t r = 0; r < total_lines; ++r) {
                    const char *l = buffer_line(&ed->buffer, r);
                    size_t llen = buffer_line_len(&ed->buffer, r);
                    if (!l || llen < target_ref_len) continue;

                    const char *pos = l;
                    while ((pos = strstr(pos, target_ref)) != NULL) {
                        size_t match_col = (size_t)(pos - l);
                        // Make sure it is not followed by ':' (which would be a definition)
                        if (match_col + target_ref_len < llen && pos[target_ref_len] == ':') {
                            pos += target_ref_len;
                            continue;
                        }

                        // Found first reference!
                        push_link_history(ed);
                        ed->cursor_row = r;
                        ed->cursor_col = match_col;
                        const char *dest_line = buffer_line(&ed->buffer, r);
                        ed->target_visual_col = utf8_byte_to_col(dest_line, match_col);
                        ed->mark_active = false;
                        ed->rendered_dirty = true;
                        editor_update_scroll(ed, ed->view_rows > 0 ? ed->view_rows : 24);
                        editor_set_status(ed, "Jumped to footnote reference [^%s] (F8/M-- to go back)", label);
                        return true;
                    }
                }

                editor_set_status(ed, "Footnote reference [^%s] not found", label);
                return true;
            }
        }
    }

    // Case 2: Footnote reference on current line: "[^<label>]"
    const char *best_ref = NULL;
    size_t best_len = 0;

    const char *first_ref = NULL;
    size_t first_len = 0;

    const char *p = line;
    while ((p = strstr(p, "[^")) != NULL) {
        size_t match_col = (size_t)(p - line);
        const char *cb = memchr(p + 2, ']', line_len - (size_t)(p + 2 - line));
        if (cb) {
            size_t cb_idx = (size_t)(cb - line);
            if (cb_idx + 1 < line_len && line[cb_idx + 1] == ':') {
                p = cb + 2;
                continue;
            }
            size_t l_len = (size_t)(cb - (p + 2));
            if (l_len > 0 && l_len < 128) {
                if (!first_ref) {
                    first_ref = p + 2;
                    first_len = l_len;
                }
                if (ed->cursor_col >= match_col && ed->cursor_col <= cb_idx + 1) {
                    best_ref = p + 2;
                    best_len = l_len;
                    break;
                }
            }
            p = cb + 1;
        } else {
            p += 2;
        }
    }

    const char *chosen = best_ref ? best_ref : first_ref;
    size_t chosen_len = best_ref ? best_len : first_len;

    if (chosen && chosen_len > 0) {
        // If cursor was NOT on the footnote and a normal link is on cursor, let normal link take precedence
        if (!best_ref) {
            char dummy_url[PATH_MAX];
            char dummy_anc[128];
            if (editor_extract_link_at_cursor(ed, dummy_url, sizeof(dummy_url), dummy_anc, sizeof(dummy_anc))) {
                return false;
            }
        }

        char label[128];
        memcpy(label, chosen, chosen_len);
        label[chosen_len] = '\0';

        // Search document for footnote definition "[^<label>]:"
        char def_prefix[140];
        snprintf(def_prefix, sizeof(def_prefix), "[^%s]:", label);
        size_t def_prefix_len = strlen(def_prefix);

        size_t total_lines = buffer_line_count(&ed->buffer);
        for (size_t r = 0; r < total_lines; ++r) {
            const char *l = buffer_line(&ed->buffer, r);
            size_t llen = buffer_line_len(&ed->buffer, r);
            if (!l) continue;

            size_t ind = 0;
            while (ind < llen && (l[ind] == ' ' || l[ind] == '\t')) ind++;

            if (llen >= ind + def_prefix_len && strncmp(l + ind, def_prefix, def_prefix_len) == 0) {
                push_link_history(ed);
                ed->cursor_row = r;
                ed->cursor_col = ind;
                const char *dest_line = buffer_line(&ed->buffer, r);
                ed->target_visual_col = utf8_byte_to_col(dest_line, ind);
                ed->mark_active = false;
                ed->rendered_dirty = true;
                editor_update_scroll(ed, ed->view_rows > 0 ? ed->view_rows : 24);
                editor_set_status(ed, "Jumped to footnote [^%s] (F8/M-- to go back)", label);
                return true;
            }
        }

        editor_set_status(ed, "Footnote definition '[^%s]:' not found", label);
        return true;
    }

    return false;
}

/** @brief Follow a local Markdown file link or `#heading` anchor under the cursor. */
bool editor_follow_link(Editor *ed) {
    if (!ed) return false;
    editor_touch_key(ed);

    // Footnote navigation: jump between reference and definition
    if (editor_follow_footnote(ed)) {
        return true;
    }

    char url[PATH_MAX] = {0};
    char anchor[128] = {0};
    if (!editor_extract_link_at_cursor(ed, url, sizeof(url), anchor, sizeof(anchor))) {
        editor_set_status(ed, "No link or footnote found on current line");
        return false;
    }

    if (is_external_url(url)) {
        editor_set_status(ed, "External URL: %s", url);
        return false;
    }

    // Same-document #anchor jump
    if (!url[0] && anchor[0]) {
        push_link_history(ed);
        if (jump_to_heading_anchor(ed, anchor)) {
            editor_set_status(ed, "Jumped to #%s (F8/M-- to go back)", anchor);
            return true;
        }
        if (ed->link_history_count > 0) {
            free(ed->link_history[--ed->link_history_count].file_path);
            ed->link_history[ed->link_history_count].file_path = NULL;
        }
        editor_set_status(ed, "Heading '#%s' not found", anchor);
        return false;
    }

    char resolved[PATH_MAX];
    if (!editor_resolve_link_path(ed, url, resolved, sizeof(resolved))) {
        editor_set_status(ed, "Cannot resolve link: %s", url);
        return false;
    }

    // Check if link points to the same file
    if (ed->buffer.file_path && ed->buffer.file_path[0]) {
        char cur_norm[PATH_MAX];
        normalize_abs_path(ed->buffer.file_path, cur_norm, sizeof(cur_norm));
        if (strcmp(cur_norm, resolved) == 0) {
            if (anchor[0] && jump_to_heading_anchor(ed, anchor)) {
                editor_set_status(ed, "Jumped to #%s", anchor);
            } else {
                editor_set_status(ed, "Already viewing %s", url);
            }
            return true;
        }
    }

    push_link_history(ed);

    struct stat st;
    if (stat(resolved, &st) == 0 && S_ISREG(st.st_mode)) {
        if (!buffer_load(&ed->buffer, resolved)) {
            editor_set_status(ed, "Cannot open linked file: %s", url);
            return false;
        }
        ed->cursor_row = 0;
        ed->cursor_col = 0;
        ed->target_visual_col = 0;
        ed->scroll_row = 0;
        ed->scroll_col = 0;
        ed->mark_active = false;
        ed->rendered_dirty = true;
        if (anchor[0]) {
            jump_to_heading_anchor(ed, anchor);
        }
        editor_set_status(ed, "Opened link: %s (F8/M-- to go back)", url);
        return true;
    }

    // Target does not exist yet: open a new buffer at that path so user can author it
    buffer_free(&ed->buffer);
    buffer_init(&ed->buffer);
    ed->buffer.file_path = strdup(resolved);
    ed->cursor_row = 0;
    ed->cursor_col = 0;
    ed->target_visual_col = 0;
    ed->scroll_row = 0;
    ed->scroll_col = 0;
    ed->mark_active = false;
    ed->rendered_dirty = true;
    editor_set_status(ed, "New linked file: %s (F8/M-- to go back)", url);
    return true;
}

/** @brief Pop link history, reload the previous file, and restore saved cursor coordinates. */
bool editor_navigate_back(Editor *ed) {
    if (!ed) return false;
    editor_touch_key(ed);

    if (ed->link_history_count == 0) {
        editor_set_status(ed, "No previous file in link history");
        return false;
    }

    LinkHistoryEntry entry = ed->link_history[--ed->link_history_count];
    ed->link_history[ed->link_history_count].file_path = NULL;

    char cur_norm[PATH_MAX] = {0};
    if (ed->buffer.file_path && ed->buffer.file_path[0]) {
        normalize_abs_path(ed->buffer.file_path, cur_norm, sizeof(cur_norm));
    }

    bool same_file = (entry.file_path[0] == '\0' && cur_norm[0] == '\0') ||
                     (entry.file_path[0] != '\0' && cur_norm[0] != '\0' && strcmp(entry.file_path, cur_norm) == 0);

    if (!same_file && entry.file_path[0] != '\0') {
        bool loaded = buffer_load(&ed->buffer, entry.file_path);
        if (!loaded) {
            buffer_free(&ed->buffer);
            buffer_init(&ed->buffer);
            ed->buffer.file_path = strdup(entry.file_path);
        }
    }

    size_t max_row = buffer_line_count(&ed->buffer);
    ed->cursor_row = (entry.cursor_row < max_row) ? entry.cursor_row : (max_row > 0 ? max_row - 1 : 0);
    size_t line_len = buffer_line_len(&ed->buffer, ed->cursor_row);
    ed->cursor_col = (entry.cursor_col <= line_len) ? entry.cursor_col : line_len;
    const char *l = buffer_line(&ed->buffer, ed->cursor_row);
    ed->target_visual_col = utf8_byte_to_col(l, ed->cursor_col);
    ed->scroll_col = 0;
    ed->mark_active = false;
    ed->rendered_dirty = true;
    editor_update_scroll(ed, ed->view_rows > 0 ? ed->view_rows : 24);

    if (same_file) {
        editor_set_status(ed, "Navigated back to line %zu", ed->cursor_row + 1);
    } else {
        editor_set_status(ed, "Back to %s", entry.file_path);
    }
    free(entry.file_path);
    return true;
}

