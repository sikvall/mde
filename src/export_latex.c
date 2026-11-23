/**
 * @file export_latex.c
 * @brief Built-in LaTeX export backend for single and multi-file documents.
 */

#define _POSIX_C_SOURCE 200809L
#include "export_internal.h"
#include <stdio.h>
#include <string.h>

/** @brief Write LaTeX-escaped text to the output stream. */
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

/** @brief Emit inline Markdown spans as LaTeX commands, converting internal links to `\hyperlink`. */
static void latex_write_spans(FILE *f, const char *raw, const MdParsedLine *pl,
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
            fputs("\\footnotemark[", f);
            fwrite(fn, 1, fn_len, f);
            fputs("]", f);
            s++;
            continue;
        }

        if (sp.type == MD_AUTO_LINK) {
            const char *url = raw + sp.start;
            size_t ulen = sp.end - sp.start;
            fputs("\\url{", f);
            fwrite(url, 1, ulen, f);
            fputs("}", f);
            s++;
            continue;
        }
        if (sp.type == MD_IMAGE_MARKER) {
            const char *alt, *url;
            size_t alt_len, url_len;
            s = extract_link_or_image_span(raw, pl, s, end_idx, &alt, &alt_len, &url, &url_len);
            fputs("\\texttt{[Image: ", f);
            latex_escape(f, alt, alt_len);
            fputs(" (", f);
            latex_escape(f, url, url_len);
            fputs(")]}", f);
            continue;
        }

        if (sp.type == MD_LINK_BRACKET && raw[sp.start] == '[') {
            const char *ltext, *url;
            size_t ltext_len, url_len;
            s = extract_link_or_image_span(raw, pl, s, end_idx, &ltext, &ltext_len, &url, &url_len);

            char frag[128] = {0};
            ssize_t target_idx = find_linked_doc_index(docs, doc_idx, url, url_len, frag, sizeof(frag));
            if (target_idx >= 0) {
                fprintf(f, "\\hyperlink{%s}{", docs->nodes[target_idx].anchor_id);
                latex_escape(f, ltext, ltext_len);
                fputs("}", f);
            } else {
                fputs("\\href{", f);
                fwrite(url, 1, url_len, f);
                fputs("}{", f);
                latex_escape(f, ltext, ltext_len);
                fputs("}", f);
            }
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

typedef enum {
    LATEX_BLK_NONE, LATEX_BLK_PARA, LATEX_BLK_UL,
    LATEX_BLK_OL, LATEX_BLK_QUOTE, LATEX_BLK_TABLE
} LatexBlockState;

/** @brief Close the currently open LaTeX environment and reset state to LATEX_BLK_NONE. */
static void latex_close_block(FILE *f, LatexBlockState *blk) {
    switch (*blk) {
        case LATEX_BLK_PARA:  fputs("\n\n", f);                     break;
        case LATEX_BLK_UL:    fputs("\\end{itemize}\n\n", f);       break;
        case LATEX_BLK_OL:    fputs("\\end{enumerate}\n\n", f);     break;
        case LATEX_BLK_QUOTE: fputs("\\end{quote}\n\n", f);         break;
        case LATEX_BLK_TABLE: fputs("\\hline\n\\end{tabular}\n\n", f); break;
        case LATEX_BLK_NONE:
        default:              break;
    }
    *blk = LATEX_BLK_NONE;
}

/** @brief Render a single document node's body lines inside a unified LaTeX document. */
static void export_builtin_latex_body(FILE *f, const ExportDocList *docs, size_t doc_idx) {
    const Buffer *buf = docs->nodes[doc_idx].buf;

    LatexBlockState blk = LATEX_BLK_NONE;
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
            latex_close_block(f, &blk);
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
            latex_close_block(f, &blk);
            md_parsed_line_free(&pl);
            continue;
        }

        if (pl.header_level > 0) {
            latex_close_block(f, &blk);
            const char *cmd = "\\section";
            if (pl.header_level == 2) cmd = "\\subsection";
            else if (pl.header_level == 3) cmd = "\\subsubsection";
            else if (pl.header_level == 4) cmd = "\\paragraph";
            else if (pl.header_level >= 5) cmd = "\\subparagraph";

            fprintf(f, "%s{", cmd);
            latex_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx);
            fputs("}\n\n", f);
        } else if (pl.is_hrule) {
            latex_close_block(f, &blk);
            fputs("\\noindent\\rule{\\textwidth}{0.4pt}\n\n", f);
        } else if (pl.is_footnote_def) {
            latex_close_block(f, &blk);
            const char *raw_fn = "";
            size_t id_len = 0;
            if (pl.span_count > 0) {
                extract_footnote_def_id(raw, &pl.spans[0], &raw_fn, &id_len);
            }
            fprintf(f, "\\footnotetext[%.*s]{", (int)id_len, raw_fn);
            latex_write_spans(f, raw, &pl, 1, pl.span_count, docs, doc_idx);
            fputs("}\n\n", f);
        } else if (pl.is_blockquote) {
            if (blk != LATEX_BLK_QUOTE) {
                latex_close_block(f, &blk);
                fputs("\\begin{quote}\n", f);
                blk = LATEX_BLK_QUOTE;
            }
            latex_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx);
            fputs("\n", f);
        } else if (pl.is_list_item) {
            LatexBlockState want = (!pl.is_task && is_ordered_bullet(raw, len))
                                       ? LATEX_BLK_OL : LATEX_BLK_UL;
            if (blk != want) {
                latex_close_block(f, &blk);
                fputs(want == LATEX_BLK_OL ? "\\begin{enumerate}\n" : "\\begin{itemize}\n", f);
                blk = want;
            }
            if (pl.is_task) {
                fprintf(f, "  \\item[$%s$] ", pl.task_checked ? "\\boxtimes" : "\\square");
            } else {
                fputs("  \\item ", f);
            }
            latex_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx);
            fputc('\n', f);
        } else if (pl.is_table_row) {
            if (is_table_sep_row(raw, len)) {
                fputs("\\hline\n", f);
                md_parsed_line_free(&pl);
                continue;
            }
            if (blk != LATEX_BLK_TABLE) {
                latex_close_block(f, &blk);
                size_t ncols = count_table_cols(raw, len);
                fputs("\\begin{tabular}{|", f);
                for (size_t c = 0; c < ncols; ++c) fputs("l|", f);
                fputs("}\n\\hline\n", f);
                blk = LATEX_BLK_TABLE;
            }
            size_t s = 0;
            bool first_cell = true;
            while (s < pl.span_count) {
                if (pl.spans[s].type == MD_TABLE_BORDER) { s++; continue; }
                size_t cell_end = s;
                while (cell_end < pl.span_count && pl.spans[cell_end].type != MD_TABLE_BORDER) cell_end++;
                if (!first_cell) fputs(" & ", f);
                first_cell = false;
                latex_write_spans(f, raw, &pl, s, cell_end, docs, doc_idx);
                s = cell_end;
            }
            fputs(" \\\\\n", f);
        } else {
            if (blk != LATEX_BLK_PARA) {
                latex_close_block(f, &blk);
                blk = LATEX_BLK_PARA;
            } else {
                fputc(' ', f);
            }
            latex_write_spans(f, raw, &pl, 0, pl.span_count, docs, doc_idx);
        }

        md_parsed_line_free(&pl);
    }

    if (code_open) fputs("\\end{verbatim}\n\n", f);
    latex_close_block(f, &blk);
}

/** @brief Export all collected document nodes into one unified standalone LaTeX document. */
bool export_builtin_latex(const ExportDocList *docs, const char *out_path,
                          char *err_msg, size_t err_cap) {
    FILE *f = fopen(out_path, "wb");
    if (!f) {
        set_str(err_msg, err_cap, "Cannot open output LaTeX file for writing");
        return false;
    }

    fputs("\\documentclass[a4paper,11pt]{article}\n", f);
    fputs("\\usepackage[utf8]{inputenc}\n", f);
    fputs("\\usepackage[T1]{fontenc}\n", f);
    fputs("\\usepackage{hyperref}\n", f);
    fputs("\\usepackage[normalem]{ulem}\n", f);
    fputs("\\usepackage{amssymb}\n", f);
    fputs("\\title{", f);
    latex_escape(f, docs->nodes[0].title, strlen(docs->nodes[0].title));
    fputs("}\n\\date{}\n\n\\begin{document}\n", f);

    // Emit root and all linked documents inside ONE unified LaTeX document
    for (size_t i = 0; i < docs->count; ++i) {
        if (docs->count > 1) {
            if (i > 0) {
                fprintf(f, "%% --- Linked source: %s ---\n", docs->nodes[i].display_name);
            }
            fprintf(f, "\\hypertarget{%s}{}\n", docs->nodes[i].anchor_id);
        }
        export_builtin_latex_body(f, docs, i);
    }

    fputs("\\end{document}\n", f);
    fclose(f);
    return true;
}
