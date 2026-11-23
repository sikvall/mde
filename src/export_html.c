/**
 * @file export_html.c
 * @brief Built-in HTML5 export backend supporting single- and multi-file Markdown documents.
 */

#define _POSIX_C_SOURCE 200809L
#include "export_internal.h"

/** @brief Write HTML5-escaped text to the output stream. */
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

/** @brief Emit inline Markdown spans as HTML5 elements, rewriting internal linked `.md` URLs. */
static void html_write_spans(FILE *f, const char *raw, const MdParsedLine *pl,
                             size_t start_idx, size_t end_idx,
                             const ExportDocList *docs, size_t doc_idx) {
    size_t s = start_idx;
    while (s < end_idx) {
        MdSpan sp = pl->spans[s];

        if (is_skipped_export_marker(sp.type)) {
            s++;
            continue;
        }

        if (sp.type == MD_FOOTNOTE_REF) {
            fputs("<sup><a href=\"#fn-", f);
            const char *fn = raw + sp.start + 2;
            size_t fn_len = (sp.end > sp.start + 3) ? (sp.end - sp.start - 3) : 0;
            html_escape(f, fn, fn_len);
            fputs("\">[", f);
            html_escape(f, fn, fn_len);
            fputs("]</a></sup>", f);
            s++;
            continue;
        }

        if (sp.type == MD_AUTO_LINK) {
            const char *url = raw + sp.start;
            size_t ulen = sp.end - sp.start;
            fputs("<a href=\"", f);
            html_escape(f, url, ulen);
            fputs("\">", f);
            html_escape(f, url, ulen);
            fputs("</a>", f);
            s++;
            continue;
        }
        if (sp.type == MD_IMAGE_MARKER) {
            const char *alt, *url;
            size_t alt_len, url_len;
            s = extract_link_or_image_span(raw, pl, s, end_idx, &alt, &alt_len, &url, &url_len);
            fputs("<img src=\"", f);
            html_escape(f, url, url_len);
            fputs("\" alt=\"", f);
            html_escape(f, alt, alt_len);
            fputs("\">", f);
            continue;
        }

        if (sp.type == MD_LINK_BRACKET && raw[sp.start] == '[') {
            const char *ltext, *url;
            size_t ltext_len, url_len;
            s = extract_link_or_image_span(raw, pl, s, end_idx, &ltext, &ltext_len, &url, &url_len);

            char frag[128] = {0};
            ssize_t target_idx = find_linked_doc_index(docs, doc_idx, url, url_len, frag, sizeof(frag));
            fputs("<a href=\"", f);
            if (target_idx >= 0) {
                if (frag[0]) {
                    char slug[128];
                    slugify_heading(frag, strlen(frag), slug, sizeof(slug));
                    fprintf(f, "#%s", slug[0] ? slug : docs->nodes[target_idx].anchor_id);
                } else {
                    fprintf(f, "#%s", docs->nodes[target_idx].anchor_id);
                }
            } else {
                html_escape(f, url, url_len);
            }
            fputs("\">", f);
            html_escape(f, ltext, ltext_len);
            fputs("</a>", f);
            continue;
        }

        const char *txt = raw + sp.start;
        size_t tlen = sp.end - sp.start;

        switch (sp.type) {
            case MD_BOLD_TEXT:
                fputs("<strong>", f); html_escape(f, txt, tlen); fputs("</strong>", f);
                break;
            case MD_ITALIC_TEXT:
                fputs("<em>", f); html_escape(f, txt, tlen); fputs("</em>", f);
                break;
            case MD_BOLD_ITALIC_TEXT:
                fputs("<strong><em>", f); html_escape(f, txt, tlen); fputs("</em></strong>", f);
                break;
            case MD_STRIKE_TEXT:
                fputs("<del>", f); html_escape(f, txt, tlen); fputs("</del>", f);
                break;
            case MD_INLINE_CODE_TEXT:
                fputs("<code>", f); html_escape(f, txt, tlen); fputs("</code>", f);
                break;
            default:
                html_escape(f, txt, tlen);
                break;
        }
        s++;
    }
}

typedef enum {
    HTML_BLK_NONE, HTML_BLK_PARA, HTML_BLK_UL, HTML_BLK_OL,
    HTML_BLK_TASK, HTML_BLK_QUOTE, HTML_BLK_TABLE
} HtmlBlockState;

/** @brief Close the currently open HTML block element and reset state to HTML_BLK_NONE. */
static void html_close_block(FILE *f, HtmlBlockState *blk) {
    switch (*blk) {
        case HTML_BLK_PARA:  fputs("</p>\n", f);              break;
        case HTML_BLK_UL:
        case HTML_BLK_TASK:  fputs("</ul>\n", f);             break;
        case HTML_BLK_OL:    fputs("</ol>\n", f);             break;
        case HTML_BLK_QUOTE: fputs("</blockquote>\n", f);     break;
        case HTML_BLK_TABLE: fputs("</tbody>\n</table>\n", f); break;
        case HTML_BLK_NONE:
        default:             break;
    }
    *blk = HTML_BLK_NONE;
}

/** @brief Render a single document node's body lines as HTML5 markup. */
static void export_builtin_html_body(FILE *f, const ExportDocList *docs, size_t doc_idx) {
    const Buffer *buf = docs->nodes[doc_idx].buf;

    HtmlBlockState blk = HTML_BLK_NONE;
    bool in_code = false;
    bool code_tag_open = false;
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
            html_close_block(f, &blk);
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

        if (len == 0) {
            html_close_block(f, &blk);
            md_parsed_line_free(&pl);
            continue;
        }

        if (pl.header_level > 0) {
            html_close_block(f, &blk);
            if (docs->count > 1) {
                char htxt[256];
                size_t hpos = 0;
                for (size_t s = 0; s < pl.span_count; ++s) {
                    if (md_is_header_marker(pl.spans[s].type)) continue;
                    size_t sl = pl.spans[s].end - pl.spans[s].start;
                    if (hpos + sl >= sizeof(htxt) - 1) sl = sizeof(htxt) - 1 - hpos;
                    if (sl > 0) { memcpy(htxt + hpos, raw + pl.spans[s].start, sl); hpos += sl; }
                }
                htxt[hpos] = '\0';
                char slug[128];
                slugify_heading(htxt, hpos, slug, sizeof(slug));
                if (slug[0]) fprintf(f, "<a id=\"%s\"></a>", slug);
            }
            fprintf(f, "<h%d>", pl.header_level);
            html_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx);
            fprintf(f, "</h%d>\n", pl.header_level);
        } else if (pl.is_hrule) {
            html_close_block(f, &blk);
            fputs("<hr>\n", f);
        } else if (pl.is_footnote_def) {
            html_close_block(f, &blk);
            const char *raw_fn = "";
            size_t id_len = 0;
            if (pl.span_count > 0) {
                extract_footnote_def_id(raw, &pl.spans[0], &raw_fn, &id_len);
            }
            fprintf(f, "<div class=\"footnote\" id=\"fn-%.*s\"><sup>[%.*s]</sup> ", (int)id_len, raw_fn, (int)id_len, raw_fn);
            html_write_spans(f, raw, &pl, 1, pl.span_count, docs, doc_idx);
            fputs("</div>\n", f);
        } else if (pl.is_blockquote) {
            if (blk != HTML_BLK_QUOTE) {
                html_close_block(f, &blk);
                fputs("<blockquote>\n", f);
                blk = HTML_BLK_QUOTE;
            }
            fputs("  <p>", f);
            html_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx);
            fputs("</p>\n", f);
        } else if (pl.is_list_item) {
            HtmlBlockState want = pl.is_task ? HTML_BLK_TASK
                                             : (is_ordered_bullet(raw, len) ? HTML_BLK_OL : HTML_BLK_UL);
            if (blk != want) {
                html_close_block(f, &blk);
                if (want == HTML_BLK_TASK) fputs("<ul class=\"task-list\">\n", f);
                else if (want == HTML_BLK_OL) fputs("<ol>\n", f);
                else fputs("<ul>\n", f);
                blk = want;
            }
            if (pl.is_task) {
                fprintf(f, "  <li><input type=\"checkbox\" disabled%s> ",
                        pl.task_checked ? " checked" : "");
            } else {
                fputs("  <li>", f);
            }
            html_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx);
            fputs("</li>\n", f);
        } else if (pl.is_table_row) {
            if (is_table_sep_row(raw, len)) {
                md_parsed_line_free(&pl);
                continue;
            }
            bool is_hdr = false;
            if (blk != HTML_BLK_TABLE) {
                html_close_block(f, &blk);
                fputs("<table>\n", f);
                blk = HTML_BLK_TABLE;
                if (r + 1 < nlines &&
                    is_table_sep_row(buffer_line(buf, r + 1), buffer_line_len(buf, r + 1))) {
                    is_hdr = true;
                }
                fputs(is_hdr ? "<thead>\n" : "<tbody>\n", f);
            }
            const char *cell_tag = is_hdr ? "th" : "td";
            fputs("  <tr>", f);
            size_t s = 0;
            while (s < pl.span_count) {
                if (pl.spans[s].type == MD_TABLE_BORDER) { s++; continue; }
                size_t cell_end = s;
                while (cell_end < pl.span_count && pl.spans[cell_end].type != MD_TABLE_BORDER) cell_end++;
                fprintf(f, "<%s>", cell_tag);
                html_write_spans(f, raw, &pl, s, cell_end, docs, doc_idx);
                fprintf(f, "</%s>", cell_tag);
                s = cell_end;
            }
            fputs("</tr>\n", f);
            if (is_hdr) fputs("</thead>\n<tbody>\n", f);
        } else {
            if (blk != HTML_BLK_PARA) {
                html_close_block(f, &blk);
                fputs("<p>", f);
                blk = HTML_BLK_PARA;
            } else {
                fputc(' ', f);
            }
            html_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx);
        }

        md_parsed_line_free(&pl);
    }

    if (code_tag_open) fputs("</code></pre>\n", f);
    html_close_block(f, &blk);
}

/** @brief Export all collected document nodes to a standalone HTML5 file. */
bool export_builtin_html(const ExportDocList *docs, const char *out_path,
                         char *err_msg, size_t err_cap) {
    FILE *f = fopen(out_path, "wb");
    if (!f) {
        set_str(err_msg, err_cap, "Cannot open output HTML file for writing");
        return false;
    }

    fputs("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n", f);
    fputs("  <meta charset=\"UTF-8\">\n", f);
    fputs("  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n", f);
    fputs("  <title>", f);
    html_escape(f, docs->nodes[0].title, strlen(docs->nodes[0].title));
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
          "    hr.mde-doc-divider { border: none; border-top: 2px solid #d0d7de; margin: 2.5rem 0; }\n"
          "  </style>\n", f);
    fputs("</head>\n<body>\n", f);

    for (size_t i = 0; i < docs->count; ++i) {
        if (docs->count > 1) {
            if (i > 0) fputs("<hr class=\"mde-doc-divider\">\n", f);
            fprintf(f, "<section id=\"%s\">\n", docs->nodes[i].anchor_id);
        }
        export_builtin_html_body(f, docs, i);
        if (docs->count > 1) {
            fputs("</section>\n", f);
        }
    }

    fputs("</body>\n</html>\n", f);
    fclose(f);
    return true;
}
