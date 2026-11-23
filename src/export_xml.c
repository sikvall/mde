/**
 * @file export_xml.c
 * @brief Built-in XML export backend for single and multi-file documents.
 */

#define _POSIX_C_SOURCE 200809L
#include "export_internal.h"
#include <stdio.h>
#include <string.h>

/** @brief Write XML-escaped text to the output stream. */
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

/** @brief Emit inline Markdown spans as structured XML elements, rewriting internal links. */
static void xml_write_spans(FILE *f, const char *raw, const MdParsedLine *pl,
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
            const char *fn = raw + sp.start + 2;
            size_t fn_len = (sp.end > sp.start + 3) ? (sp.end - sp.start - 3) : 0;
            fputs("<footnoteref linkend=\"fn-", f);
            xml_escape(f, fn, fn_len);
            fputs("\"/>", f);
            s++;
            continue;
        }

        if (sp.type == MD_AUTO_LINK) {
            const char *url = raw + sp.start;
            size_t ulen = sp.end - sp.start;
            fputs("<link url=\"", f);
            xml_escape(f, url, ulen);
            fputs("\">", f);
            xml_escape(f, url, ulen);
            fputs("</link>", f);
            s++;
            continue;
        }
        if (sp.type == MD_IMAGE_MARKER) {
            const char *alt, *url;
            size_t alt_len, url_len;
            s = extract_link_or_image_span(raw, pl, s, end_idx, &alt, &alt_len, &url, &url_len);
            fputs("<image src=\"", f);
            xml_escape(f, url, url_len);
            fputs("\" alt=\"", f);
            xml_escape(f, alt, alt_len);
            fputs("\"/>", f);
            continue;
        }

        if (sp.type == MD_LINK_BRACKET && raw[sp.start] == '[') {
            const char *ltext, *url;
            size_t ltext_len, url_len;
            s = extract_link_or_image_span(raw, pl, s, end_idx, &ltext, &ltext_len, &url, &url_len);

            char frag[128] = {0};
            ssize_t target_idx = find_linked_doc_index(docs, doc_idx, url, url_len, frag, sizeof(frag));
            fputs("<link url=\"", f);
            if (target_idx >= 0) {
                fprintf(f, "#%s", docs->nodes[target_idx].anchor_id);
            } else {
                xml_escape(f, url, url_len);
            }
            fputs("\">", f);
            xml_escape(f, ltext, ltext_len);
            fputs("</link>", f);
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

typedef enum {
    XML_BLK_NONE, XML_BLK_PARA, XML_BLK_LIST, XML_BLK_QUOTE, XML_BLK_TABLE
} XmlBlockState;

/** @brief Close the currently open XML block element and reset state to XML_BLK_NONE. */
static void xml_close_block(FILE *f, XmlBlockState *blk) {
    switch (*blk) {
        case XML_BLK_PARA:  fputs("</paragraph>\n", f);    break;
        case XML_BLK_LIST:  fputs("  </list>\n", f);       break;
        case XML_BLK_QUOTE: fputs("  </blockquote>\n", f); break;
        case XML_BLK_TABLE: fputs("  </table>\n", f);      break;
        case XML_BLK_NONE:
        default:            break;
    }
    *blk = XML_BLK_NONE;
}

/** @brief Render a single document node's body lines as structured XML elements. */
static void export_builtin_xml_body(FILE *f, const ExportDocList *docs, size_t doc_idx) {
    const Buffer *buf = docs->nodes[doc_idx].buf;

    XmlBlockState blk = XML_BLK_NONE;
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
            xml_close_block(f, &blk);
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
            xml_close_block(f, &blk);
            md_parsed_line_free(&pl);
            continue;
        }

        if (pl.header_level > 0) {
            xml_close_block(f, &blk);
            fprintf(f, "  <heading level=\"%d\">", pl.header_level);
            xml_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx);
            fputs("</heading>\n", f);
        } else if (pl.is_hrule) {
            xml_close_block(f, &blk);
            fputs("  <hrule/>\n", f);
        } else if (pl.is_blockquote) {
            if (blk != XML_BLK_QUOTE) {
                xml_close_block(f, &blk);
                fputs("  <blockquote>\n", f);
                blk = XML_BLK_QUOTE;
            }
            fputs("    <paragraph>", f);
            xml_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx);
            fputs("</paragraph>\n", f);
        } else if (pl.is_footnote_def) {
            xml_close_block(f, &blk);
            const char *raw_fn = "";
            size_t id_len = 0;
            if (pl.span_count > 0) {
                extract_footnote_def_id(raw, &pl.spans[0], &raw_fn, &id_len);
            }
            fprintf(f, "  <footnote id=\"fn-%.*s\">", (int)id_len, raw_fn);
            xml_write_spans(f, raw, &pl, 1, pl.span_count, docs, doc_idx);
            fputs("</footnote>\n", f);
        } else if (pl.is_list_item) {
            if (blk != XML_BLK_LIST) {
                xml_close_block(f, &blk);
                const char *ltype = pl.is_task ? "task"
                                               : (is_ordered_bullet(raw, len) ? "ordered" : "unordered");
                fprintf(f, "  <list type=\"%s\">\n", ltype);
                blk = XML_BLK_LIST;
            }
            if (pl.is_task) {
                fprintf(f, "    <task checked=\"%s\">", pl.task_checked ? "true" : "false");
                xml_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx);
                fputs("</task>\n", f);
            } else {
                fputs("    <item>", f);
                xml_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx);
                fputs("</item>\n", f);
            }
        } else if (pl.is_table_row) {
            if (is_table_sep_row(raw, len)) {
                md_parsed_line_free(&pl);
                continue;
            }
            bool is_hdr = false;
            if (blk != XML_BLK_TABLE) {
                xml_close_block(f, &blk);
                fputs("  <table>\n", f);
                blk = XML_BLK_TABLE;
                if (r + 1 < nlines &&
                    is_table_sep_row(buffer_line(buf, r + 1), buffer_line_len(buf, r + 1))) {
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
                xml_write_spans(f, raw, &pl, s, cell_end, docs, doc_idx);
                fputs("</cell>", f);
                s = cell_end;
            }
            fputs("</row>\n", f);
        } else {
            if (blk != XML_BLK_PARA) {
                xml_close_block(f, &blk);
                fputs("  <paragraph>", f);
                blk = XML_BLK_PARA;
            } else {
                fputc(' ', f);
            }
            xml_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx);
        }

        md_parsed_line_free(&pl);
    }

    if (code_open) fputs("</codeblock>\n", f);
    xml_close_block(f, &blk);
}

/** @brief Export all collected document nodes to a structured XML file. */
bool export_builtin_xml(const ExportDocList *docs, const char *out_path,
                        char *err_msg, size_t err_cap) {
    FILE *f = fopen(out_path, "wb");
    if (!f) {
        set_str(err_msg, err_cap, "Cannot open output XML file for writing");
        return false;
    }

    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", f);
    fputs("<document title=\"", f);
    xml_escape(f, docs->nodes[0].title, strlen(docs->nodes[0].title));
    fputs("\">\n", f);

    export_builtin_xml_body(f, docs, 0);

    for (size_t i = 1; i < docs->count; ++i) {
        fprintf(f, "  <linked-document id=\"%s\" title=\"", docs->nodes[i].anchor_id);
        xml_escape(f, docs->nodes[i].title, strlen(docs->nodes[i].title));
        fputs("\" source=\"", f);
        xml_escape(f, docs->nodes[i].display_name, strlen(docs->nodes[i].display_name));
        fputs("\">\n", f);
        export_builtin_xml_body(f, docs, i);
        fputs("  </linked-document>\n", f);
    }

    fputs("</document>\n", f);
    fclose(f);
    return true;
}
