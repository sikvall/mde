/**
 * @file export.c
 * @brief Implementation of Markdown export to HTML, XML, EPUB, and LaTeX.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "export.h"
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
#include <sys/types.h>
#include <sys/wait.h>

static void set_str(char *dst, size_t cap, const char *msg) {
    if (dst && cap > 0) {
        snprintf(dst, cap, "%s", msg ? msg : "");
    }
}

const char *export_format_name(ExportFormat fmt) {
    switch (fmt) {
        case EXPORT_HTML:  return "HTML";
        case EXPORT_XML:   return "XML";
        case EXPORT_EPUB:  return "EPUB";
        case EXPORT_LATEX: return "LaTeX";
        default:           return "Unknown";
    }
}

const char *export_format_ext(ExportFormat fmt) {
    switch (fmt) {
        case EXPORT_HTML:  return ".html";
        case EXPORT_XML:   return ".xml";
        case EXPORT_EPUB:  return ".epub";
        case EXPORT_LATEX: return ".tex";
        default:           return ".out";
    }
}

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
 * External Pandoc Backend (fork + execvp, no shell)
 * ========================================================================= */

static bool export_via_pandoc(const Buffer *buf, ExportFormat fmt, const char *out_path,
                              char *err_msg, size_t err_cap) {
    char tmp_template[] = "/tmp/mde_export_XXXXXX";
    int tmp_fd = mkstemp(tmp_template);
    if (tmp_fd < 0) {
        set_str(err_msg, err_cap, "Failed to create temporary file for pandoc");
        return false;
    }

    FILE *tf = fdopen(tmp_fd, "wb");
    if (!tf) {
        close(tmp_fd);
        unlink(tmp_template);
        set_str(err_msg, err_cap, "Failed to open temporary file stream");
        return false;
    }

    size_t line_count = buffer_line_count(buf);
    for (size_t i = 0; i < line_count; ++i) {
        const char *l = buffer_line(buf, i);
        size_t len = buffer_line_len(buf, i);
        if (len > 0) fwrite(l, 1, len, tf);
        fputc('\n', tf);
    }
    fclose(tf);

    const char *pandoc_to = "html5";
    bool standalone = true;
    switch (fmt) {
        case EXPORT_HTML:  pandoc_to = "html5";    standalone = true;  break;
        case EXPORT_XML:   pandoc_to = "docbook5"; standalone = true;  break;
        case EXPORT_EPUB:  pandoc_to = "epub3";    standalone = false; break;
        case EXPORT_LATEX: pandoc_to = "latex";    standalone = true;  break;
        default: break;
    }

    char title[128];
    extract_doc_title(buf, title, sizeof(title));
    char meta_title[160];
    snprintf(meta_title, sizeof(meta_title), "title=%s", title);

    int err_pipe[2];
    if (pipe(err_pipe) != 0) {
        unlink(tmp_template);
        set_str(err_msg, err_cap, "Failed to create pipe for pandoc");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(err_pipe[0]);
        close(err_pipe[1]);
        unlink(tmp_template);
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

        char *argv[16];
        int argc = 0;
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
        argv[argc++] = "-o";
        argv[argc++] = (char *)out_path;
        argv[argc++] = tmp_template;
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
    unlink(tmp_template);

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

static bool is_table_sep_row(const char *line, size_t len) {
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

static size_t count_table_cols(const char *line, size_t len) {
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

static bool is_ordered_bullet(const char *line, size_t len) {
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    return (i < len && isdigit((unsigned char)line[i]));
}

/* =========================================================================
 * Built-In HTML5 Exporter
 * ========================================================================= */

static void html_escape(FILE *f, const char *s, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        switch (s[i]) {
            case '&':  fputs("&amp;", f);  break;
            case '<':  fputs("&lt;", f);   break;
            case '>':  fputs("&gt;", f);   break;
            case '"':  fputs("&quot;", f); break;
            default:   fputc(s[i], f);     break;
        }
    }
}

static void html_write_spans(FILE *f, const char *raw, const MdParsedLine *pl, size_t start_idx, size_t end_idx) {
    size_t s = start_idx;
    while (s < end_idx) {
        MdSpan sp = pl->spans[s];

        // Skip block/inline delimiter markers
        if (md_is_header_marker(sp.type) ||
            sp.type == MD_BLOCKQUOTE_MARKER ||
            sp.type == MD_LIST_BULLET ||
            sp.type == MD_TASK_UNCHECKED ||
            sp.type == MD_TASK_CHECKED ||
            sp.type == MD_BOLD_MARKER ||
            sp.type == MD_ITALIC_MARKER ||
            sp.type == MD_BOLD_ITALIC_MARKER ||
            sp.type == MD_STRIKE_MARKER ||
            sp.type == MD_INLINE_CODE_MARKER ||
            sp.type == MD_TABLE_BORDER) {
            s++;
            continue;
        }

        // Image sequence: ! [ alt ] ( url )
        if (sp.type == MD_IMAGE_MARKER) {
            const char *alt = "";
            size_t alt_len = 0;
            const char *url = "";
            size_t url_len = 0;
            size_t k = s + 1;
            while (k < end_idx &&
                   (pl->spans[k].type == MD_LINK_BRACKET ||
                    pl->spans[k].type == MD_LINK_TEXT ||
                    pl->spans[k].type == MD_LINK_URL_PAREN ||
                    pl->spans[k].type == MD_LINK_URL)) {
                if (pl->spans[k].type == MD_LINK_TEXT) {
                    alt = raw + pl->spans[k].start;
                    alt_len = pl->spans[k].end - pl->spans[k].start;
                } else if (pl->spans[k].type == MD_LINK_URL) {
                    url = raw + pl->spans[k].start;
                    url_len = pl->spans[k].end - pl->spans[k].start;
                }
                k++;
                if (k > s + 1 && pl->spans[k - 1].type == MD_LINK_URL_PAREN &&
                    raw[pl->spans[k - 1].start] == ')') {
                    break;
                }
            }
            fputs("<img src=\"", f);
            html_escape(f, url, url_len);
            fputs("\" alt=\"", f);
            html_escape(f, alt, alt_len);
            fputs("\">", f);
            s = k;
            continue;
        }

        // Link sequence: [ text ] ( url )
        if (sp.type == MD_LINK_BRACKET && raw[sp.start] == '[') {
            const char *ltext = "";
            size_t ltext_len = 0;
            const char *url = "";
            size_t url_len = 0;
            size_t k = s + 1;
            while (k < end_idx &&
                   (pl->spans[k].type == MD_LINK_BRACKET ||
                    pl->spans[k].type == MD_LINK_TEXT ||
                    pl->spans[k].type == MD_LINK_URL_PAREN ||
                    pl->spans[k].type == MD_LINK_URL)) {
                if (pl->spans[k].type == MD_LINK_TEXT) {
                    ltext = raw + pl->spans[k].start;
                    ltext_len = pl->spans[k].end - pl->spans[k].start;
                } else if (pl->spans[k].type == MD_LINK_URL) {
                    url = raw + pl->spans[k].start;
                    url_len = pl->spans[k].end - pl->spans[k].start;
                }
                k++;
                if (pl->spans[k - 1].type == MD_LINK_URL_PAREN &&
                    raw[pl->spans[k - 1].start] == ')') {
                    break;
                }
            }
            fputs("<a href=\"", f);
            html_escape(f, url, url_len);
            fputs("\">", f);
            html_escape(f, ltext, ltext_len);
            fputs("</a>", f);
            s = k;
            continue;
        }

        const char *txt = raw + sp.start;
        size_t tlen = sp.end - sp.start;

        switch (sp.type) {
            case MD_BOLD_TEXT:
                fputs("<strong>", f);
                html_escape(f, txt, tlen);
                fputs("</strong>", f);
                break;
            case MD_ITALIC_TEXT:
                fputs("<em>", f);
                html_escape(f, txt, tlen);
                fputs("</em>", f);
                break;
            case MD_BOLD_ITALIC_TEXT:
                fputs("<strong><em>", f);
                html_escape(f, txt, tlen);
                fputs("</em></strong>", f);
                break;
            case MD_STRIKE_TEXT:
                fputs("<del>", f);
                html_escape(f, txt, tlen);
                fputs("</del>", f);
                break;
            case MD_INLINE_CODE_TEXT:
                fputs("<code>", f);
                html_escape(f, txt, tlen);
                fputs("</code>", f);
                break;
            default:
                html_escape(f, txt, tlen);
                break;
        }
        s++;
    }
}

static bool export_builtin_html(const Buffer *buf, const char *out_path, char *err_msg, size_t err_cap) {
    FILE *f = fopen(out_path, "wb");
    if (!f) {
        set_str(err_msg, err_cap, "Cannot open output HTML file for writing");
        return false;
    }

    char title[128];
    extract_doc_title(buf, title, sizeof(title));

    fputs("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n", f);
    fputs("  <meta charset=\"UTF-8\">\n", f);
    fputs("  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n", f);
    fputs("  <title>", f);
    html_escape(f, title, strlen(title));
    fputs("</title>\n", f);
    fputs("  <style>\n"
          "    body { font-family: system-ui, -apple-system, sans-serif; line-height: 1.6; max-width: 820px; margin: 2rem auto; padding: 0 1.25rem; color: #222; }\n"
          "    pre { background: #f4f4f6; padding: 1rem; border-radius: 6px; overflow-x: auto; }\n"
          "    code { background: #f4f4f6; padding: 0.15rem 0.35rem; border-radius: 4px; font-family: monospace; }\n"
          "    pre code { background: none; padding: 0; }\n"
          "    blockquote { border-left: 4px solid #4a90d9; margin: 1rem 0; padding: 0.25rem 1rem; color: #555; }\n"
          "    table { border-collapse: collapse; margin: 1rem 0; width: 100%; }\n"
          "    th, td { border: 1px solid #ccc; padding: 0.45rem 0.75rem; text-align: left; }\n"
          "    th { background: #f4f4f6; }\n"
          "    ul.task-list { list-style: none; padding-left: 1.2rem; }\n"
          "  </style>\n", f);
    fputs("</head>\n<body>\n", f);

    enum { BLK_NONE, BLK_PARA, BLK_UL, BLK_OL, BLK_TASK, BLK_QUOTE, BLK_TABLE } blk = BLK_NONE;
    bool in_code = false;
    bool code_tag_open = false;
    bool table_header_done = false;
    char lang[32] = {0};

    size_t nlines = buffer_line_count(buf);
    for (size_t r = 0; r < nlines; ++r) {
        const char *raw = buffer_line(buf, r);
        size_t len = buffer_line_len(buf, r);
        bool was_in_code = in_code;

        MdParsedLine pl;
        md_parse_line(raw, len, &in_code, lang, sizeof(lang), &pl);

        // Code fence transitions
        if (pl.is_code_block && pl.span_count > 0 && pl.spans[0].type == MD_CODE_FENCE) {
            if (blk != BLK_NONE) {
                if (blk == BLK_PARA) fputs("</p>\n", f);
                else if (blk == BLK_UL || blk == BLK_TASK) fputs("</ul>\n", f);
                else if (blk == BLK_OL) fputs("</ol>\n", f);
                else if (blk == BLK_QUOTE) fputs("</blockquote>\n", f);
                else if (blk == BLK_TABLE) fputs("</tbody>\n</table>\n", f);
                blk = BLK_NONE;
            }
            if (!was_in_code && in_code) {
                if (lang[0]) {
                    fputs("<pre><code class=\"language-", f);
                    html_escape(f, lang, strlen(lang));
                    fputs("\">", f);
                } else {
                    fputs("<pre><code>", f);
                }
                code_tag_open = true;
            } else if (was_in_code && !in_code) {
                fputs("</code></pre>\n", f);
                code_tag_open = false;
            }
            md_parsed_line_free(&pl);
            continue;
        }

        if (code_tag_open) {
            html_escape(f, raw, len);
            fputc('\n', f);
            md_parsed_line_free(&pl);
            continue;
        }

        // Empty line closes active block
        if (len == 0) {
            if (blk == BLK_PARA) fputs("</p>\n", f);
            else if (blk == BLK_UL || blk == BLK_TASK) fputs("</ul>\n", f);
            else if (blk == BLK_OL) fputs("</ol>\n", f);
            else if (blk == BLK_QUOTE) fputs("</blockquote>\n", f);
            else if (blk == BLK_TABLE) fputs("</tbody>\n</table>\n", f);
            blk = BLK_NONE;
            md_parsed_line_free(&pl);
            continue;
        }

        // Determine desired block transition
        if (pl.header_level > 0) {
            if (blk != BLK_NONE) {
                if (blk == BLK_PARA) fputs("</p>\n", f);
                else if (blk == BLK_UL || blk == BLK_TASK) fputs("</ul>\n", f);
                else if (blk == BLK_OL) fputs("</ol>\n", f);
                else if (blk == BLK_QUOTE) fputs("</blockquote>\n", f);
                else if (blk == BLK_TABLE) fputs("</tbody>\n</table>\n", f);
                blk = BLK_NONE;
            }
            fprintf(f, "<h%d>", pl.header_level);
            html_write_spans(f, raw, &pl, 0, pl.span_count);
            fprintf(f, "</h%d>\n", pl.header_level);
        } else if (pl.is_hrule) {
            if (blk != BLK_NONE) {
                if (blk == BLK_PARA) fputs("</p>\n", f);
                else if (blk == BLK_UL || blk == BLK_TASK) fputs("</ul>\n", f);
                else if (blk == BLK_OL) fputs("</ol>\n", f);
                else if (blk == BLK_QUOTE) fputs("</blockquote>\n", f);
                else if (blk == BLK_TABLE) fputs("</tbody>\n</table>\n", f);
                blk = BLK_NONE;
            }
            fputs("<hr>\n", f);
        } else if (pl.is_blockquote) {
            if (blk != BLK_QUOTE) {
                if (blk == BLK_PARA) fputs("</p>\n", f);
                else if (blk == BLK_UL || blk == BLK_TASK) fputs("</ul>\n", f);
                else if (blk == BLK_OL) fputs("</ol>\n", f);
                else if (blk == BLK_TABLE) fputs("</tbody>\n</table>\n", f);
                fputs("<blockquote>\n", f);
                blk = BLK_QUOTE;
            }
            fputs("  <p>", f);
            html_write_spans(f, raw, &pl, 0, pl.span_count);
            fputs("</p>\n", f);
        } else if (pl.is_list_item) {
            int want = pl.is_task ? BLK_TASK : (is_ordered_bullet(raw, len) ? BLK_OL : BLK_UL);
            if ((int)blk != want) {
                if (blk == BLK_PARA) fputs("</p>\n", f);
                else if (blk == BLK_UL || blk == BLK_TASK) fputs("</ul>\n", f);
                else if (blk == BLK_OL) fputs("</ol>\n", f);
                else if (blk == BLK_QUOTE) fputs("</blockquote>\n", f);
                else if (blk == BLK_TABLE) fputs("</tbody>\n</table>\n", f);

                if (want == BLK_TASK) fputs("<ul class=\"task-list\">\n", f);
                else if (want == BLK_OL) fputs("<ol>\n", f);
                else fputs("<ul>\n", f);
                blk = want;
            }
            if (pl.is_task) {
                fprintf(f, "  <li><input type=\"checkbox\" disabled%s> ",
                        pl.task_checked ? " checked" : "");
            } else {
                fputs("  <li>", f);
            }
            html_write_spans(f, raw, &pl, 0, pl.span_count);
            fputs("</li>\n", f);
        } else if (pl.is_table_row) {
            if (is_table_sep_row(raw, len)) {
                table_header_done = true;
                md_parsed_line_free(&pl);
                continue;
            }
            bool is_hdr = false;
            if (blk != BLK_TABLE) {
                if (blk == BLK_PARA) fputs("</p>\n", f);
                else if (blk == BLK_UL || blk == BLK_TASK) fputs("</ul>\n", f);
                else if (blk == BLK_OL) fputs("</ol>\n", f);
                else if (blk == BLK_QUOTE) fputs("</blockquote>\n", f);
                fputs("<table>\n", f);
                blk = BLK_TABLE;
                table_header_done = false;
                if (r + 1 < nlines && is_table_sep_row(buffer_line(buf, r + 1), buffer_line_len(buf, r + 1))) {
                    is_hdr = true;
                }
                if (is_hdr) fputs("<thead>\n", f);
                else fputs("<tbody>\n", f);
            }
            const char *cell_tag = is_hdr ? "th" : "td";
            fputs("  <tr>", f);
            size_t s = 0;
            while (s < pl.span_count) {
                if (pl.spans[s].type == MD_TABLE_BORDER) {
                    s++;
                    continue;
                }
                size_t cell_end = s;
                while (cell_end < pl.span_count && pl.spans[cell_end].type != MD_TABLE_BORDER) {
                    cell_end++;
                }
                fprintf(f, "<%s>", cell_tag);
                html_write_spans(f, raw, &pl, s, cell_end);
                fprintf(f, "</%s>", cell_tag);
                s = cell_end;
            }
            fputs("</tr>\n", f);
            if (is_hdr) {
                fputs("</thead>\n<tbody>\n", f);
            }
        } else {
            // Paragraph text
            if (blk != BLK_PARA) {
                if (blk == BLK_UL || blk == BLK_TASK) fputs("</ul>\n", f);
                else if (blk == BLK_OL) fputs("</ol>\n", f);
                else if (blk == BLK_QUOTE) fputs("</blockquote>\n", f);
                else if (blk == BLK_TABLE) fputs("</tbody>\n</table>\n", f);
                fputs("<p>", f);
                blk = BLK_PARA;
            } else {
                fputc(' ', f);
            }
            html_write_spans(f, raw, &pl, 0, pl.span_count);
        }

        md_parsed_line_free(&pl);
    }

    if (code_tag_open) fputs("</code></pre>\n", f);
    if (blk == BLK_PARA) fputs("</p>\n", f);
    else if (blk == BLK_UL || blk == BLK_TASK) fputs("</ul>\n", f);
    else if (blk == BLK_OL) fputs("</ol>\n", f);
    else if (blk == BLK_QUOTE) fputs("</blockquote>\n", f);
    else if (blk == BLK_TABLE) {
        (void)table_header_done;
        fputs("</tbody>\n</table>\n", f);
    }

    fputs("</body>\n</html>\n", f);
    fclose(f);
    return true;
}

/* =========================================================================
 * Built-In XML Exporter
 * ========================================================================= */

static void xml_escape(FILE *f, const char *s, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        switch (s[i]) {
            case '&':  fputs("&amp;", f);  break;
            case '<':  fputs("&lt;", f);   break;
            case '>':  fputs("&gt;", f);   break;
            case '"':  fputs("&quot;", f); break;
            case '\'': fputs("&apos;", f); break;
            default:   fputc(s[i], f);     break;
        }
    }
}

static void xml_write_spans(FILE *f, const char *raw, const MdParsedLine *pl, size_t start_idx, size_t end_idx) {
    size_t s = start_idx;
    while (s < end_idx) {
        MdSpan sp = pl->spans[s];

        if (md_is_header_marker(sp.type) ||
            sp.type == MD_BLOCKQUOTE_MARKER ||
            sp.type == MD_LIST_BULLET ||
            sp.type == MD_TASK_UNCHECKED ||
            sp.type == MD_TASK_CHECKED ||
            sp.type == MD_BOLD_MARKER ||
            sp.type == MD_ITALIC_MARKER ||
            sp.type == MD_BOLD_ITALIC_MARKER ||
            sp.type == MD_STRIKE_MARKER ||
            sp.type == MD_INLINE_CODE_MARKER ||
            sp.type == MD_TABLE_BORDER) {
            s++;
            continue;
        }

        if (sp.type == MD_IMAGE_MARKER) {
            const char *alt = ""; size_t alt_len = 0;
            const char *url = ""; size_t url_len = 0;
            size_t k = s + 1;
            while (k < end_idx &&
                   (pl->spans[k].type == MD_LINK_BRACKET ||
                    pl->spans[k].type == MD_LINK_TEXT ||
                    pl->spans[k].type == MD_LINK_URL_PAREN ||
                    pl->spans[k].type == MD_LINK_URL)) {
                if (pl->spans[k].type == MD_LINK_TEXT) {
                    alt = raw + pl->spans[k].start;
                    alt_len = pl->spans[k].end - pl->spans[k].start;
                } else if (pl->spans[k].type == MD_LINK_URL) {
                    url = raw + pl->spans[k].start;
                    url_len = pl->spans[k].end - pl->spans[k].start;
                }
                k++;
                if (pl->spans[k - 1].type == MD_LINK_URL_PAREN && raw[pl->spans[k - 1].start] == ')') break;
            }
            fputs("<image src=\"", f);
            xml_escape(f, url, url_len);
            fputs("\" alt=\"", f);
            xml_escape(f, alt, alt_len);
            fputs("\"/>", f);
            s = k;
            continue;
        }

        if (sp.type == MD_LINK_BRACKET && raw[sp.start] == '[') {
            const char *ltext = ""; size_t ltext_len = 0;
            const char *url = ""; size_t url_len = 0;
            size_t k = s + 1;
            while (k < end_idx &&
                   (pl->spans[k].type == MD_LINK_BRACKET ||
                    pl->spans[k].type == MD_LINK_TEXT ||
                    pl->spans[k].type == MD_LINK_URL_PAREN ||
                    pl->spans[k].type == MD_LINK_URL)) {
                if (pl->spans[k].type == MD_LINK_TEXT) {
                    ltext = raw + pl->spans[k].start;
                    ltext_len = pl->spans[k].end - pl->spans[k].start;
                } else if (pl->spans[k].type == MD_LINK_URL) {
                    url = raw + pl->spans[k].start;
                    url_len = pl->spans[k].end - pl->spans[k].start;
                }
                k++;
                if (pl->spans[k - 1].type == MD_LINK_URL_PAREN && raw[pl->spans[k - 1].start] == ')') break;
            }
            fputs("<link url=\"", f);
            xml_escape(f, url, url_len);
            fputs("\">", f);
            xml_escape(f, ltext, ltext_len);
            fputs("</link>", f);
            s = k;
            continue;
        }

        const char *txt = raw + sp.start;
        size_t tlen = sp.end - sp.start;
        switch (sp.type) {
            case MD_BOLD_TEXT:
                fputs("<bold>", f); xml_escape(f, txt, tlen); fputs("</bold>", f);
                break;
            case MD_ITALIC_TEXT:
                fputs("<italic>", f); xml_escape(f, txt, tlen); fputs("</italic>", f);
                break;
            case MD_BOLD_ITALIC_TEXT:
                fputs("<bolditalic>", f); xml_escape(f, txt, tlen); fputs("</bolditalic>", f);
                break;
            case MD_STRIKE_TEXT:
                fputs("<strike>", f); xml_escape(f, txt, tlen); fputs("</strike>", f);
                break;
            case MD_INLINE_CODE_TEXT:
                fputs("<code>", f); xml_escape(f, txt, tlen); fputs("</code>", f);
                break;
            default:
                xml_escape(f, txt, tlen);
                break;
        }
        s++;
    }
}

static bool export_builtin_xml(const Buffer *buf, const char *out_path, char *err_msg, size_t err_cap) {
    FILE *f = fopen(out_path, "wb");
    if (!f) {
        set_str(err_msg, err_cap, "Cannot open output XML file for writing");
        return false;
    }

    char title[128];
    extract_doc_title(buf, title, sizeof(title));

    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", f);
    fputs("<document title=\"", f);
    xml_escape(f, title, strlen(title));
    fputs("\">\n", f);

    enum { BLK_NONE, BLK_PARA, BLK_LIST, BLK_QUOTE, BLK_TABLE } blk = BLK_NONE;
    bool in_code = false;
    bool code_open = false;
    char lang[32] = {0};

    size_t nlines = buffer_line_count(buf);
    for (size_t r = 0; r < nlines; ++r) {
        const char *raw = buffer_line(buf, r);
        size_t len = buffer_line_len(buf, r);
        bool was_in_code = in_code;

        MdParsedLine pl;
        md_parse_line(raw, len, &in_code, lang, sizeof(lang), &pl);

        if (pl.is_code_block && pl.span_count > 0 && pl.spans[0].type == MD_CODE_FENCE) {
            if (blk != BLK_NONE) {
                if (blk == BLK_PARA) fputs("</paragraph>\n", f);
                else if (blk == BLK_LIST) fputs("  </list>\n", f);
                else if (blk == BLK_QUOTE) fputs("  </blockquote>\n", f);
                else if (blk == BLK_TABLE) fputs("  </table>\n", f);
                blk = BLK_NONE;
            }
            if (!was_in_code && in_code) {
                if (lang[0]) {
                    fputs("  <codeblock language=\"", f);
                    xml_escape(f, lang, strlen(lang));
                    fputs("\">", f);
                } else {
                    fputs("  <codeblock>", f);
                }
                code_open = true;
            } else if (was_in_code && !in_code) {
                fputs("</codeblock>\n", f);
                code_open = false;
            }
            md_parsed_line_free(&pl);
            continue;
        }

        if (code_open) {
            xml_escape(f, raw, len);
            fputc('\n', f);
            md_parsed_line_free(&pl);
            continue;
        }

        if (len == 0) {
            if (blk == BLK_PARA) fputs("</paragraph>\n", f);
            else if (blk == BLK_LIST) fputs("  </list>\n", f);
            else if (blk == BLK_QUOTE) fputs("  </blockquote>\n", f);
            else if (blk == BLK_TABLE) fputs("  </table>\n", f);
            blk = BLK_NONE;
            md_parsed_line_free(&pl);
            continue;
        }

        if (pl.header_level > 0) {
            if (blk != BLK_NONE) {
                if (blk == BLK_PARA) fputs("</paragraph>\n", f);
                else if (blk == BLK_LIST) fputs("  </list>\n", f);
                else if (blk == BLK_QUOTE) fputs("  </blockquote>\n", f);
                else if (blk == BLK_TABLE) fputs("  </table>\n", f);
                blk = BLK_NONE;
            }
            fprintf(f, "  <heading level=\"%d\">", pl.header_level);
            xml_write_spans(f, raw, &pl, 0, pl.span_count);
            fputs("</heading>\n", f);
        } else if (pl.is_hrule) {
            if (blk != BLK_NONE) {
                if (blk == BLK_PARA) fputs("</paragraph>\n", f);
                else if (blk == BLK_LIST) fputs("  </list>\n", f);
                else if (blk == BLK_QUOTE) fputs("  </blockquote>\n", f);
                else if (blk == BLK_TABLE) fputs("  </table>\n", f);
                blk = BLK_NONE;
            }
            fputs("  <hrule/>\n", f);
        } else if (pl.is_blockquote) {
            if (blk != BLK_QUOTE) {
                if (blk == BLK_PARA) fputs("</paragraph>\n", f);
                else if (blk == BLK_LIST) fputs("  </list>\n", f);
                else if (blk == BLK_TABLE) fputs("  </table>\n", f);
                fputs("  <blockquote>\n", f);
                blk = BLK_QUOTE;
            }
            fputs("    <paragraph>", f);
            xml_write_spans(f, raw, &pl, 0, pl.span_count);
            fputs("</paragraph>\n", f);
        } else if (pl.is_list_item) {
            if (blk != BLK_LIST) {
                if (blk == BLK_PARA) fputs("</paragraph>\n", f);
                else if (blk == BLK_QUOTE) fputs("  </blockquote>\n", f);
                else if (blk == BLK_TABLE) fputs("  </table>\n", f);
                const char *ltype = pl.is_task ? "task" : (is_ordered_bullet(raw, len) ? "ordered" : "unordered");
                fprintf(f, "  <list type=\"%s\">\n", ltype);
                blk = BLK_LIST;
            }
            if (pl.is_task) {
                fprintf(f, "    <task checked=\"%s\">", pl.task_checked ? "true" : "false");
                xml_write_spans(f, raw, &pl, 0, pl.span_count);
                fputs("</task>\n", f);
            } else {
                fputs("    <item>", f);
                xml_write_spans(f, raw, &pl, 0, pl.span_count);
                fputs("</item>\n", f);
            }
        } else if (pl.is_table_row) {
            if (is_table_sep_row(raw, len)) {
                md_parsed_line_free(&pl);
                continue;
            }
            bool is_hdr = false;
            if (blk != BLK_TABLE) {
                if (blk == BLK_PARA) fputs("</paragraph>\n", f);
                else if (blk == BLK_LIST) fputs("  </list>\n", f);
                else if (blk == BLK_QUOTE) fputs("  </blockquote>\n", f);
                fputs("  <table>\n", f);
                blk = BLK_TABLE;
                if (r + 1 < nlines && is_table_sep_row(buffer_line(buf, r + 1), buffer_line_len(buf, r + 1))) {
                    is_hdr = true;
                }
            }
            fprintf(f, "    <row%s>", is_hdr ? " header=\"true\"" : "");
            size_t s = 0;
            while (s < pl.span_count) {
                if (pl.spans[s].type == MD_TABLE_BORDER) { s++; continue; }
                size_t cell_end = s;
                while (cell_end < pl.span_count && pl.spans[cell_end].type != MD_TABLE_BORDER) cell_end++;
                fputs("<cell>", f);
                xml_write_spans(f, raw, &pl, s, cell_end);
                fputs("</cell>", f);
                s = cell_end;
            }
            fputs("</row>\n", f);
        } else {
            if (blk != BLK_PARA) {
                if (blk == BLK_LIST) fputs("  </list>\n", f);
                else if (blk == BLK_QUOTE) fputs("  </blockquote>\n", f);
                else if (blk == BLK_TABLE) fputs("  </table>\n", f);
                fputs("  <paragraph>", f);
                blk = BLK_PARA;
            } else {
                fputc(' ', f);
            }
            xml_write_spans(f, raw, &pl, 0, pl.span_count);
        }

        md_parsed_line_free(&pl);
    }

    if (code_open) fputs("</codeblock>\n", f);
    if (blk == BLK_PARA) fputs("</paragraph>\n", f);
    else if (blk == BLK_LIST) fputs("  </list>\n", f);
    else if (blk == BLK_QUOTE) fputs("  </blockquote>\n", f);
    else if (blk == BLK_TABLE) fputs("  </table>\n", f);

    fputs("</document>\n", f);
    fclose(f);
    return true;
}

/* =========================================================================
 * Built-In LaTeX Exporter
 * ========================================================================= */

static void latex_escape(FILE *f, const char *s, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        switch (s[i]) {
            case '\\': fputs("\\textbackslash{}", f); break;
            case '{':  fputs("\\{", f);               break;
            case '}':  fputs("\\}", f);               break;
            case '$':  fputs("\\$", f);               break;
            case '&':  fputs("\\&", f);               break;
            case '#':  fputs("\\#", f);               break;
            case '^':  fputs("\\textasciicircum{}", f); break;
            case '_':  fputs("\\_", f);               break;
            case '%':  fputs("\\%", f);               break;
            case '~':  fputs("\\textasciitilde{}", f); break;
            default:   fputc(s[i], f);                break;
        }
    }
}

static void latex_write_spans(FILE *f, const char *raw, const MdParsedLine *pl, size_t start_idx, size_t end_idx) {
    size_t s = start_idx;
    while (s < end_idx) {
        MdSpan sp = pl->spans[s];

        if (md_is_header_marker(sp.type) ||
            sp.type == MD_BLOCKQUOTE_MARKER ||
            sp.type == MD_LIST_BULLET ||
            sp.type == MD_TASK_UNCHECKED ||
            sp.type == MD_TASK_CHECKED ||
            sp.type == MD_BOLD_MARKER ||
            sp.type == MD_ITALIC_MARKER ||
            sp.type == MD_BOLD_ITALIC_MARKER ||
            sp.type == MD_STRIKE_MARKER ||
            sp.type == MD_INLINE_CODE_MARKER ||
            sp.type == MD_TABLE_BORDER) {
            s++;
            continue;
        }

        if (sp.type == MD_IMAGE_MARKER) {
            const char *alt = ""; size_t alt_len = 0;
            const char *url = ""; size_t url_len = 0;
            size_t k = s + 1;
            while (k < end_idx &&
                   (pl->spans[k].type == MD_LINK_BRACKET ||
                    pl->spans[k].type == MD_LINK_TEXT ||
                    pl->spans[k].type == MD_LINK_URL_PAREN ||
                    pl->spans[k].type == MD_LINK_URL)) {
                if (pl->spans[k].type == MD_LINK_TEXT) {
                    alt = raw + pl->spans[k].start;
                    alt_len = pl->spans[k].end - pl->spans[k].start;
                } else if (pl->spans[k].type == MD_LINK_URL) {
                    url = raw + pl->spans[k].start;
                    url_len = pl->spans[k].end - pl->spans[k].start;
                }
                k++;
                if (pl->spans[k - 1].type == MD_LINK_URL_PAREN && raw[pl->spans[k - 1].start] == ')') break;
            }
            fputs("\\texttt{[Image: ", f);
            latex_escape(f, alt, alt_len);
            fputs(" (", f);
            latex_escape(f, url, url_len);
            fputs(")]}", f);
            s = k;
            continue;
        }

        if (sp.type == MD_LINK_BRACKET && raw[sp.start] == '[') {
            const char *ltext = ""; size_t ltext_len = 0;
            const char *url = ""; size_t url_len = 0;
            size_t k = s + 1;
            while (k < end_idx &&
                   (pl->spans[k].type == MD_LINK_BRACKET ||
                    pl->spans[k].type == MD_LINK_TEXT ||
                    pl->spans[k].type == MD_LINK_URL_PAREN ||
                    pl->spans[k].type == MD_LINK_URL)) {
                if (pl->spans[k].type == MD_LINK_TEXT) {
                    ltext = raw + pl->spans[k].start;
                    ltext_len = pl->spans[k].end - pl->spans[k].start;
                } else if (pl->spans[k].type == MD_LINK_URL) {
                    url = raw + pl->spans[k].start;
                    url_len = pl->spans[k].end - pl->spans[k].start;
                }
                k++;
                if (pl->spans[k - 1].type == MD_LINK_URL_PAREN && raw[pl->spans[k - 1].start] == ')') break;
            }
            fputs("\\href{", f);
            fwrite(url, 1, url_len, f);
            fputs("}{", f);
            latex_escape(f, ltext, ltext_len);
            fputs("}", f);
            s = k;
            continue;
        }

        const char *txt = raw + sp.start;
        size_t tlen = sp.end - sp.start;
        switch (sp.type) {
            case MD_BOLD_TEXT:
                fputs("\\textbf{", f); latex_escape(f, txt, tlen); fputs("}", f);
                break;
            case MD_ITALIC_TEXT:
                fputs("\\textit{", f); latex_escape(f, txt, tlen); fputs("}", f);
                break;
            case MD_BOLD_ITALIC_TEXT:
                fputs("\\textbf{\\textit{", f); latex_escape(f, txt, tlen); fputs("}}", f);
                break;
            case MD_STRIKE_TEXT:
                fputs("\\sout{", f); latex_escape(f, txt, tlen); fputs("}", f);
                break;
            case MD_INLINE_CODE_TEXT:
                fputs("\\texttt{", f); latex_escape(f, txt, tlen); fputs("}", f);
                break;
            default:
                latex_escape(f, txt, tlen);
                break;
        }
        s++;
    }
}

static bool export_builtin_latex(const Buffer *buf, const char *out_path, char *err_msg, size_t err_cap) {
    FILE *f = fopen(out_path, "wb");
    if (!f) {
        set_str(err_msg, err_cap, "Cannot open output LaTeX file for writing");
        return false;
    }

    char title[128];
    extract_doc_title(buf, title, sizeof(title));

    fputs("\\documentclass[a4paper,11pt]{article}\n", f);
    fputs("\\usepackage[utf8]{inputenc}\n", f);
    fputs("\\usepackage[T1]{fontenc}\n", f);
    fputs("\\usepackage{hyperref}\n", f);
    fputs("\\usepackage[normalem]{ulem}\n", f);
    fputs("\\usepackage{amssymb}\n", f);
    fputs("\\title{", f);
    latex_escape(f, title, strlen(title));
    fputs("}\n\\date{}\n\n\\begin{document}\n", f);

    enum { BLK_NONE, BLK_PARA, BLK_UL, BLK_OL, BLK_QUOTE, BLK_TABLE } blk = BLK_NONE;
    bool in_code = false;
    bool code_open = false;
    char lang[32] = {0};

    size_t nlines = buffer_line_count(buf);
    for (size_t r = 0; r < nlines; ++r) {
        const char *raw = buffer_line(buf, r);
        size_t len = buffer_line_len(buf, r);
        bool was_in_code = in_code;

        MdParsedLine pl;
        md_parse_line(raw, len, &in_code, lang, sizeof(lang), &pl);

        if (pl.is_code_block && pl.span_count > 0 && pl.spans[0].type == MD_CODE_FENCE) {
            if (blk != BLK_NONE) {
                if (blk == BLK_PARA) fputs("\n\n", f);
                else if (blk == BLK_UL) fputs("\\end{itemize}\n\n", f);
                else if (blk == BLK_OL) fputs("\\end{enumerate}\n\n", f);
                else if (blk == BLK_QUOTE) fputs("\\end{quote}\n\n", f);
                else if (blk == BLK_TABLE) fputs("\\hline\n\\end{tabular}\n\n", f);
                blk = BLK_NONE;
            }
            if (!was_in_code && in_code) {
                fputs("\\begin{verbatim}\n", f);
                code_open = true;
            } else if (was_in_code && !in_code) {
                fputs("\\end{verbatim}\n\n", f);
                code_open = false;
            }
            md_parsed_line_free(&pl);
            continue;
        }

        if (code_open) {
            fwrite(raw, 1, len, f);
            fputc('\n', f);
            md_parsed_line_free(&pl);
            continue;
        }

        if (len == 0) {
            if (blk == BLK_PARA) fputs("\n\n", f);
            else if (blk == BLK_UL) fputs("\\end{itemize}\n\n", f);
            else if (blk == BLK_OL) fputs("\\end{enumerate}\n\n", f);
            else if (blk == BLK_QUOTE) fputs("\\end{quote}\n\n", f);
            else if (blk == BLK_TABLE) fputs("\\hline\n\\end{tabular}\n\n", f);
            blk = BLK_NONE;
            md_parsed_line_free(&pl);
            continue;
        }

        if (pl.header_level > 0) {
            if (blk != BLK_NONE) {
                if (blk == BLK_PARA) fputs("\n\n", f);
                else if (blk == BLK_UL) fputs("\\end{itemize}\n\n", f);
                else if (blk == BLK_OL) fputs("\\end{enumerate}\n\n", f);
                else if (blk == BLK_QUOTE) fputs("\\end{quote}\n\n", f);
                else if (blk == BLK_TABLE) fputs("\\hline\n\\end{tabular}\n\n", f);
                blk = BLK_NONE;
            }
            const char *cmd = "\\section";
            if (pl.header_level == 2) cmd = "\\subsection";
            else if (pl.header_level == 3) cmd = "\\subsubsection";
            else if (pl.header_level == 4) cmd = "\\paragraph";
            else if (pl.header_level >= 5) cmd = "\\subparagraph";

            fprintf(f, "%s{", cmd);
            latex_write_spans(f, raw, &pl, 0, pl.span_count);
            fputs("}\n\n", f);
        } else if (pl.is_hrule) {
            if (blk != BLK_NONE) {
                if (blk == BLK_PARA) fputs("\n\n", f);
                else if (blk == BLK_UL) fputs("\\end{itemize}\n\n", f);
                else if (blk == BLK_OL) fputs("\\end{enumerate}\n\n", f);
                else if (blk == BLK_QUOTE) fputs("\\end{quote}\n\n", f);
                else if (blk == BLK_TABLE) fputs("\\hline\n\\end{tabular}\n\n", f);
                blk = BLK_NONE;
            }
            fputs("\\noindent\\rule{\\textwidth}{0.4pt}\n\n", f);
        } else if (pl.is_blockquote) {
            if (blk != BLK_QUOTE) {
                if (blk == BLK_PARA) fputs("\n\n", f);
                else if (blk == BLK_UL) fputs("\\end{itemize}\n\n", f);
                else if (blk == BLK_OL) fputs("\\end{enumerate}\n\n", f);
                else if (blk == BLK_TABLE) fputs("\\hline\n\\end{tabular}\n\n", f);
                fputs("\\begin{quote}\n", f);
                blk = BLK_QUOTE;
            }
            latex_write_spans(f, raw, &pl, 0, pl.span_count);
            fputs("\n", f);
        } else if (pl.is_list_item) {
            int want = (!pl.is_task && is_ordered_bullet(raw, len)) ? BLK_OL : BLK_UL;
            if ((int)blk != want) {
                if (blk == BLK_PARA) fputs("\n\n", f);
                else if (blk == BLK_UL) fputs("\\end{itemize}\n\n", f);
                else if (blk == BLK_OL) fputs("\\end{enumerate}\n\n", f);
                else if (blk == BLK_QUOTE) fputs("\\end{quote}\n\n", f);
                else if (blk == BLK_TABLE) fputs("\\hline\n\\end{tabular}\n\n", f);

                fputs(want == BLK_OL ? "\\begin{enumerate}\n" : "\\begin{itemize}\n", f);
                blk = want;
            }
            if (pl.is_task) {
                fprintf(f, "  \\item[$%s$] ", pl.task_checked ? "\\boxtimes" : "\\square");
            } else {
                fputs("  \\item ", f);
            }
            latex_write_spans(f, raw, &pl, 0, pl.span_count);
            fputc('\n', f);
        } else if (pl.is_table_row) {
            if (is_table_sep_row(raw, len)) {
                fputs("\\hline\n", f);
                md_parsed_line_free(&pl);
                continue;
            }
            if (blk != BLK_TABLE) {
                if (blk == BLK_PARA) fputs("\n\n", f);
                else if (blk == BLK_UL) fputs("\\end{itemize}\n\n", f);
                else if (blk == BLK_OL) fputs("\\end{enumerate}\n\n", f);
                else if (blk == BLK_QUOTE) fputs("\\end{quote}\n\n", f);
                size_t ncols = count_table_cols(raw, len);
                fputs("\\begin{tabular}{|", f);
                for (size_t c = 0; c < ncols; ++c) fputs("l|", f);
                fputs("}\n\\hline\n", f);
                blk = BLK_TABLE;
            }
            size_t s = 0;
            bool first_cell = true;
            while (s < pl.span_count) {
                if (pl.spans[s].type == MD_TABLE_BORDER) { s++; continue; }
                size_t cell_end = s;
                while (cell_end < pl.span_count && pl.spans[cell_end].type != MD_TABLE_BORDER) cell_end++;
                if (!first_cell) fputs(" & ", f);
                first_cell = false;
                latex_write_spans(f, raw, &pl, s, cell_end);
                s = cell_end;
            }
            fputs(" \\\\\n", f);
        } else {
            if (blk != BLK_PARA) {
                if (blk == BLK_UL) fputs("\\end{itemize}\n\n", f);
                else if (blk == BLK_OL) fputs("\\end{enumerate}\n\n", f);
                else if (blk == BLK_QUOTE) fputs("\\end{quote}\n\n", f);
                else if (blk == BLK_TABLE) fputs("\\hline\n\\end{tabular}\n\n", f);
                blk = BLK_PARA;
            } else {
                fputc(' ', f);
            }
            latex_write_spans(f, raw, &pl, 0, pl.span_count);
        }

        md_parsed_line_free(&pl);
    }

    if (code_open) fputs("\\end{verbatim}\n\n", f);
    if (blk == BLK_PARA) fputs("\n\n", f);
    else if (blk == BLK_UL) fputs("\\end{itemize}\n\n", f);
    else if (blk == BLK_OL) fputs("\\end{enumerate}\n\n", f);
    else if (blk == BLK_QUOTE) fputs("\\end{quote}\n\n", f);
    else if (blk == BLK_TABLE) fputs("\\hline\n\\end{tabular}\n\n", f);

    fputs("\\end{document}\n", f);
    fclose(f);
    return true;
}

/* =========================================================================
 * Master Export Dispatch
 * ========================================================================= */

bool export_document(const Buffer *buf, ExportFormat fmt, const char *out_path,
                     ExportEngine engine, char *out_engine_used, size_t engine_cap,
                     char *err_msg, size_t err_cap) {
    if (!buf || !out_path || !out_path[0]) {
        set_str(err_msg, err_cap, "Invalid buffer or empty output path");
        return false;
    }

    bool has_pandoc = export_has_pandoc();

    // EPUB always requires pandoc
    if (fmt == EXPORT_EPUB) {
        if (engine == EXPORT_ENGINE_BUILTIN) {
            set_str(err_msg, err_cap, "EPUB export requires pandoc (no built-in EPUB backend)");
            return false;
        }
        if (!has_pandoc) {
            set_str(err_msg, err_cap, "EPUB export requires pandoc (install: sudo apt install pandoc)");
            return false;
        }
        set_str(out_engine_used, engine_cap, "pandoc");
        return export_via_pandoc(buf, fmt, out_path, err_msg, err_cap);
    }

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

    if (use_pandoc) {
        set_str(out_engine_used, engine_cap, "pandoc");
        return export_via_pandoc(buf, fmt, out_path, err_msg, err_cap);
    }

    set_str(out_engine_used, engine_cap, "built-in");
    switch (fmt) {
        case EXPORT_HTML:  return export_builtin_html(buf, out_path, err_msg, err_cap);
        case EXPORT_XML:   return export_builtin_xml(buf, out_path, err_msg, err_cap);
        case EXPORT_LATEX: return export_builtin_latex(buf, out_path, err_msg, err_cap);
        default:
            set_str(err_msg, err_cap, "Unsupported export format");
            return false;
    }
}
