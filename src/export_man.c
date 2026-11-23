/**
 * @file export_man.c
 * @brief Built-in UNIX/Linux troff/groff man(7) manual page export backend.
 */

#define _POSIX_C_SOURCE 200809L
#include "export_internal.h"
#include "version.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/** @brief Escape plain text bytes for troff/groff body output. */
static void man_escape_text(FILE *f, const char *s, size_t len, bool *at_line_start) {
    if (!s || len == 0) return;
    if (at_line_start && *at_line_start && (s[0] == '.' || s[0] == '\'')) {
        fputs("\\&", f);
    }
    for (size_t i = 0; i < len; ++i) {
        char c = s[i];
        if (c == '\\') {
            fputs("\\e", f);
        } else if (c == '-') {
            fputs("\\-", f);
        } else if (c == '\n' || c == '\r' || c == '\t') {
            fputc(' ', f);
        } else {
            fputc(c, f);
        }
        if (at_line_start) *at_line_start = false;
    }
}

/** @brief Escape text for inclusion inside double-quoted roff macro arguments. */
static void man_escape_quoted_arg(FILE *f, const char *s, size_t len, bool upper) {
    if (!s || len == 0) return;
    for (size_t i = 0; i < len; ++i) {
        unsigned char uc = (unsigned char)s[i];
        char c = upper ? (char)toupper(uc) : (char)uc;
        if (c == '\\') {
            fputs("\\e", f);
        } else if (c == '-') {
            fputs("\\-", f);
        } else if (c == '"') {
            fputs("\"\"", f);
        } else if (c == '\n' || c == '\r' || c == '\t') {
            fputc(' ', f);
        } else {
            fputc(c, f);
        }
    }
}

/** @brief Return true if any document node in docs contains at least one Markdown table row. */
static bool man_docs_have_tables(const ExportDocList *docs) {
    for (size_t d = 0; d < docs->count; ++d) {
        const Buffer *buf = docs->nodes[d].buf;
        size_t nlines = buffer_line_count(buf);
        bool in_code = false;
        char lang[32] = {0};
        for (size_t r = 0; r < nlines; ++r) {
            MdParsedLine pl;
            md_parse_line(buffer_line(buf, r), buffer_line_len(buf, r),
                          &in_code, lang, sizeof(lang), &pl);
            bool has_tbl = (!pl.is_code_block && pl.is_table_row);
            md_parsed_line_free(&pl);
            if (has_tbl) return true;
        }
    }
    return false;
}

/** @brief Extract a man section string ("1".."9") from out_path suffix, defaulting to "1". */
static void extract_man_section(const char *out_path, char *out_sec, size_t cap) {
    snprintf(out_sec, cap, "1");
    if (!out_path || !out_path[0]) return;
    const char *slash = strrchr(out_path, '/');
    const char *base = slash ? slash + 1 : out_path;
    const char *dot = strrchr(base, '.');
    if (dot && dot[1] >= '1' && dot[1] <= '9') {
        size_t elen = strlen(dot + 1);
        if (elen >= 1 && elen <= 3) {
            bool valid = true;
            for (size_t i = 0; i < elen; ++i) {
                if (!isalnum((unsigned char)dot[1 + i])) {
                    valid = false;
                    break;
                }
            }
            if (valid) snprintf(out_sec, cap, "%s", dot + 1);
        }
    }
}

/** @brief Emit inline Markdown spans as roff formatting sequences. */
static void man_write_spans(FILE *f, const char *raw, const MdParsedLine *pl,
                            size_t start_idx, size_t end_idx,
                            const ExportDocList *docs, size_t doc_idx,
                            bool *at_line_start) {
    size_t s = start_idx;
    while (s < end_idx) {
        MdSpan sp = pl->spans[s];

        if (is_skipped_export_marker(sp.type)) {
            s++;
            continue;
        }

        if (sp.type == MD_FOOTNOTE_REF) {
            const char *fn = raw + sp.start + 2;
            size_t fn_len = (sp.end > sp.start + 3) ? (sp.end - sp.start - 3) : 0;
            bool inside = false;
            man_escape_text(f, "[note ", 6, at_line_start);
            fputs("\\fI", f);
            man_escape_text(f, fn, fn_len, &inside);
            fputs("\\fR]", f);
            s++;
            continue;
        }

        if (sp.type == MD_AUTO_LINK) {
            const char *url = raw + sp.start;
            size_t ulen = sp.end - sp.start;
            bool inside = false;
            fputs("\\fI", f);
            man_escape_text(f, url, ulen, &inside);
            fputs("\\fR", f);
            if (at_line_start) *at_line_start = false;
            s++;
            continue;
        }

        if (sp.type == MD_IMAGE_MARKER) {
            const char *alt, *url;
            size_t alt_len, url_len;
            s = extract_link_or_image_span(raw, pl, s, end_idx, &alt, &alt_len, &url, &url_len);
            bool inside = false;
            man_escape_text(f, "[Image: ", 8, at_line_start);
            if (alt_len > 0) {
                fputs("\\fI", f);
                man_escape_text(f, alt, alt_len, &inside);
                fputs("\\fR", f);
            }
            if (url_len > 0) {
                fputs(" (\\fI", f);
                man_escape_text(f, url, url_len, &inside);
                fputs("\\fR)", f);
            }
            fputc(']', f);
            continue;
        }

        if (sp.type == MD_LINK_BRACKET && raw[sp.start] == '[') {
            const char *ltext, *url;
            size_t ltext_len, url_len;
            s = extract_link_or_image_span(raw, pl, s, end_idx, &ltext, &ltext_len, &url, &url_len);

            char frag[128] = {0};
            ssize_t target_idx = find_linked_doc_index(docs, doc_idx, url, url_len, frag, sizeof(frag));
            bool inside = false;
            fputs("\\fB", f);
            man_escape_text(f, ltext, ltext_len, &inside);
            fputs("\\fR", f);
            if (at_line_start) *at_line_start = false;

            if (target_idx >= 0) {
                const char *ref_label = frag[0] ? frag : docs->nodes[target_idx].title;
                fputs(" (see \\fI", f);
                man_escape_text(f, ref_label, strlen(ref_label), &inside);
                fputs("\\fR)", f);
            } else if (url_len > 0) {
                fputs(" <\\fI", f);
                man_escape_text(f, url, url_len, &inside);
                fputs("\\fR>", f);
            }
            continue;
        }

        const char *txt = raw + sp.start;
        size_t tlen = sp.end - sp.start;
        bool inside = false;
        switch (sp.type) {
            case MD_BOLD_TEXT:
            case MD_INLINE_CODE_TEXT:
                fputs("\\fB", f);
                man_escape_text(f, txt, tlen, &inside);
                fputs("\\fR", f);
                if (at_line_start) *at_line_start = false;
                break;
            case MD_ITALIC_TEXT:
                fputs("\\fI", f);
                man_escape_text(f, txt, tlen, &inside);
                fputs("\\fR", f);
                if (at_line_start) *at_line_start = false;
                break;
            case MD_BOLD_ITALIC_TEXT:
                fputs("\\f(BI", f);
                man_escape_text(f, txt, tlen, &inside);
                fputs("\\fR", f);
                if (at_line_start) *at_line_start = false;
                break;
            default:
                man_escape_text(f, txt, tlen, at_line_start);
                break;
        }
        s++;
    }
}

/** @brief Emit plain heading text (stripped of markers) inside a quoted `.SH` or `.SS` line. */
static void man_write_heading_arg(FILE *f, const char *raw, const MdParsedLine *pl, bool upper) {
    size_t s = 0;
    while (s < pl->span_count) {
        MdSpan sp = pl->spans[s];
        if (is_skipped_export_marker(sp.type)) {
            s++;
            continue;
        }
        if (sp.type == MD_LINK_BRACKET && raw[sp.start] == '[') {
            const char *ltext, *url;
            size_t ltext_len, url_len;
            s = extract_link_or_image_span(raw, pl, s, pl->span_count, &ltext, &ltext_len, &url, &url_len);
            man_escape_quoted_arg(f, ltext, ltext_len, upper);
            continue;
        }
        man_escape_quoted_arg(f, raw + sp.start, sp.end - sp.start, upper);
        s++;
    }
}

typedef enum {
    MAN_BLK_NONE, MAN_BLK_PARA, MAN_BLK_UL, MAN_BLK_OL, MAN_BLK_QUOTE, MAN_BLK_TABLE
} ManBlockState;

/** @brief Close the active man block state cleanly before transitioning. */
static void man_close_block(FILE *f, ManBlockState *blk, size_t *ol_num) {
    switch (*blk) {
        case MAN_BLK_PARA:
            fputc('\n', f);
            break;
        case MAN_BLK_QUOTE:
            fputs(".RE\n", f);
            break;
        case MAN_BLK_TABLE:
            fputs(".TE\n", f);
            break;
        case MAN_BLK_UL:
        case MAN_BLK_OL:
        case MAN_BLK_NONE:
        default:
            break;
    }
    *blk = MAN_BLK_NONE;
    if (ol_num) *ol_num = 0;
}

/** @brief Render a single document node as groff `man(7)` source. */
static void export_builtin_man_body(FILE *f, const ExportDocList *docs, size_t doc_idx) {
    const Buffer *buf = docs->nodes[doc_idx].buf;

    EpubMetadata doc_meta;
    memset(&doc_meta, 0, sizeof(doc_meta));
    size_t body_start = 0;
    bool has_fm = epub_parse_front_matter(buf, &doc_meta, &body_start);

    ManBlockState blk = MAN_BLK_NONE;
    size_t ol_num = 0;
    bool in_code = false;
    bool code_open = false;
    char lang[32] = {0};

    size_t nlines = buffer_line_count(buf);
    for (size_t r = (has_fm ? body_start : 0); r < nlines; ++r) {
        const char *raw = buffer_line(buf, r);
        size_t len = buffer_line_len(buf, r);
        bool was_in_code = in_code;

        MdParsedLine pl;
        md_parse_line(raw, len, &in_code, lang, sizeof(lang), &pl);

        if (pl.is_code_block && pl.span_count > 0 && pl.spans[0].type == MD_CODE_FENCE) {
            man_close_block(f, &blk, &ol_num);
            if (!was_in_code && in_code) {
                fputs(".PP\n.RS 4\n.nf\n", f);
                code_open = true;
            } else if (was_in_code && !in_code) {
                fputs(".fi\n.RE\n", f);
                code_open = false;
            }
            md_parsed_line_free(&pl);
            continue;
        }

        if (code_open) {
            bool line_start = true;
            man_escape_text(f, raw, len, &line_start);
            fputc('\n', f);
            md_parsed_line_free(&pl);
            continue;
        }

        if (len == 0) {
            man_close_block(f, &blk, &ol_num);
            md_parsed_line_free(&pl);
            continue;
        }

        if (pl.header_level > 0) {
            man_close_block(f, &blk, &ol_num);
            if (pl.header_level == 1) {
                fputs(".SH \"", f);
                man_write_heading_arg(f, raw, &pl, true);
                fputs("\"\n", f);
            } else if (pl.header_level == 2) {
                fputs(".SS \"", f);
                man_write_heading_arg(f, raw, &pl, false);
                fputs("\"\n", f);
            } else {
                bool inside = false;
                fputs(".PP\n\\fB", f);
                man_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx, &inside);
                fputs("\\fR\n", f);
            }
        } else if (pl.is_hrule) {
            man_close_block(f, &blk, &ol_num);
            fputs(".PP\n.sp 0.5\n", f);
        } else if (pl.is_footnote_def) {
            man_close_block(f, &blk, &ol_num);
            const char *raw_fn = "";
            size_t id_len = 0;
            if (pl.span_count > 0) {
                extract_footnote_def_id(raw, &pl.spans[0], &raw_fn, &id_len);
            }
            bool inside = false;
            fputs(".TP\n\\fB[", f);
            man_escape_text(f, raw_fn, id_len, &inside);
            fputs("]\\fR\n", f);
            bool line_start = true;
            man_write_spans(f, raw, &pl, 1, pl.span_count, docs, doc_idx, &line_start);
            fputc('\n', f);
        } else if (pl.is_blockquote) {
            if (blk != MAN_BLK_QUOTE) {
                man_close_block(f, &blk, &ol_num);
                fputs(".PP\n.RS 4\n", f);
                blk = MAN_BLK_QUOTE;
            }
            bool line_start = true;
            man_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx, &line_start);
            fputc('\n', f);
        } else if (pl.is_list_item) {
            ManBlockState want = (!pl.is_task && is_ordered_bullet(raw, len))
                                     ? MAN_BLK_OL : MAN_BLK_UL;
            if (blk != want) {
                man_close_block(f, &blk, &ol_num);
                blk = want;
            }
            if (want == MAN_BLK_OL) {
                ol_num++;
                fprintf(f, ".IP \"%zu.\" 4\n", ol_num);
            } else {
                fputs(".IP \\(bu 2\n", f);
                if (pl.is_task) {
                    fputs(pl.task_checked ? "[x]" : "[ ]", f);
                }
            }
            bool line_start = !pl.is_task;
            man_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx, &line_start);
            fputc('\n', f);
        } else if (pl.is_table_row) {
            if (is_table_sep_row(raw, len)) {
                md_parsed_line_free(&pl);
                continue;
            }
            if (blk != MAN_BLK_TABLE) {
                man_close_block(f, &blk, &ol_num);
                size_t ncols = count_table_cols(raw, len);
                bool has_header = (r + 1 < nlines &&
                                   is_table_sep_row(buffer_line(buf, r + 1), buffer_line_len(buf, r + 1)));
                fputs(".PP\n.TS\nallbox;\n", f);
                if (has_header) {
                    for (size_t c = 0; c < ncols; ++c) {
                        fputs(c + 1 < ncols ? "lb " : "lb\n", f);
                    }
                }
                for (size_t c = 0; c < ncols; ++c) {
                    fputs(c + 1 < ncols ? "l " : "l.\n", f);
                }
                blk = MAN_BLK_TABLE;
            }

            size_t s = 0;
            bool first_cell = true;
            while (s < pl.span_count) {
                if (pl.spans[s].type == MD_TABLE_BORDER) { s++; continue; }
                size_t cell_end = s;
                while (cell_end < pl.span_count && pl.spans[cell_end].type != MD_TABLE_BORDER) cell_end++;
                if (!first_cell) fputc('\t', f);
                first_cell = false;
                fputs("T{\n", f);
                bool cell_start = true;
                man_write_spans(f, raw, &pl, s, cell_end, docs, doc_idx, &cell_start);
                fputs("\nT}", f);
                s = cell_end;
            }
            fputc('\n', f);
        } else {
            bool line_start = false;
            if (blk != MAN_BLK_PARA) {
                man_close_block(f, &blk, &ol_num);
                fputs(".PP\n", f);
                blk = MAN_BLK_PARA;
                line_start = true;
            } else {
                fputc(' ', f);
            }
            man_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx, &line_start);
        }

        md_parsed_line_free(&pl);
    }

    if (code_open) fputs(".fi\n.RE\n", f);
    man_close_block(f, &blk, &ol_num);
}

/** @brief Export all collected document nodes as a standalone UNIX/Linux man(7) manual page. */
bool export_builtin_man(const ExportDocList *docs, const char *out_path,
                        char *err_msg, size_t err_cap) {
    if (!docs || docs->count == 0 || !out_path || !out_path[0]) {
        set_str(err_msg, err_cap, "Invalid document list or output path");
        return false;
    }

    FILE *f = fopen(out_path, "wb");
    if (!f) {
        set_str(err_msg, err_cap, "Cannot open output man page file for writing");
        return false;
    }

    EpubMetadata meta;
    memset(&meta, 0, sizeof(meta));
    size_t dummy_start = 0;
    epub_parse_front_matter(docs->nodes[0].buf, &meta, &dummy_start);

    const char *raw_title = meta.title[0] ? meta.title : docs->nodes[0].title;
    char section[8];
    extract_man_section(out_path, section, sizeof(section));

    char date_str[64] = {0};
    if (meta.date[0]) {
        snprintf(date_str, sizeof(date_str), "%s", meta.date);
    } else {
        time_t now = time(NULL);
        struct tm tm_buf;
        if (localtime_r(&now, &tm_buf)) {
            strftime(date_str, sizeof(date_str), "%Y-%m-%d", &tm_buf);
        } else {
            snprintf(date_str, sizeof(date_str), "2026");
        }
    }

    char default_source[64];
    snprintf(default_source, sizeof(default_source), "%s %s", MDE_APP_NAME, MDE_VERSION_STRING);
    const char *source_str = default_source;
    if (meta.publisher[0]) {
        source_str = meta.publisher;
    } else if (meta.author[0]) {
        source_str = meta.author;
    }

    if (man_docs_have_tables(docs)) {
        fputs("'\\\" t\n", f);
    }
    fputs(".TH \"", f);
    man_escape_quoted_arg(f, raw_title, strlen(raw_title), true);
    fputs("\" \"", f);
    man_escape_quoted_arg(f, section, strlen(section), false);
    fputs("\" \"", f);
    man_escape_quoted_arg(f, date_str, strlen(date_str), false);
    fputs("\" \"", f);
    man_escape_quoted_arg(f, source_str, strlen(source_str), false);
    fputs("\" \"User Commands\"\n", f);

    for (size_t i = 0; i < docs->count; ++i) {
        if (i > 0) {
            fprintf(f, ".\\\" --- Linked source: %s ---\n", docs->nodes[i].display_name);
            fputs(".SH \"", f);
            man_escape_quoted_arg(f, docs->nodes[i].title, strlen(docs->nodes[i].title), true);
            fputs("\"\n", f);
        }
        export_builtin_man_body(f, docs, i);
    }

    bool ok = (ferror(f) == 0);
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        set_str(err_msg, err_cap, "I/O error while writing man page");
    }
    return ok;
}
