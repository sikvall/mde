/**
 * @file renderer.c
 * @brief Implementation of Markdown preview generation, aligned box tables,
 *        directional link indicators, and cursor column mapping.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _XOPEN_SOURCE_EXTENDED 1
#include "renderer.h"
#include "markdown.h"
#include "theme.h"
#include "utf8.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define MDE_TABLE_MAX_COLS 16

/** @brief Initialize an empty RenderedDoc container. */
void rendered_doc_init(RenderedDoc *doc) {
    if (!doc) return;
    doc->lines = NULL;
    doc->count = 0;
    doc->cap = 0;
}

/** @brief Return true if a token is a raw inline delimiter stripped in preview mode. */
static bool is_stripped_inline_marker(MdTokenType type) {
    return type == MD_BOLD_MARKER ||
           type == MD_ITALIC_MARKER ||
           type == MD_BOLD_ITALIC_MARKER ||
           type == MD_STRIKE_MARKER ||
           type == MD_INLINE_CODE_MARKER;
}

/** @brief Classify a link URL as external (↗), same-doc anchor (↓), or local file (→). */
static const char *link_direction_indicator(const char *url, size_t url_len) {
    while (url_len > 0 && isspace((unsigned char)url[0])) { url++; url_len--; }
    if (url_len == 0) return "";
    if (url[0] == '#') return " ↓";
    if ((url_len >= 7 && strncasecmp(url, "http://", 7) == 0) ||
        (url_len >= 8 && strncasecmp(url, "https://", 8) == 0) ||
        (url_len >= 7 && strncasecmp(url, "mailto:", 7) == 0) ||
        (url_len >= 6 && strncasecmp(url, "ftp://", 6) == 0) ||
        (url_len >= 4 && strncasecmp(url, "tel:", 4) == 0) ||
        (url_len >= 2 && strncmp(url, "//", 2) == 0)) {
        return " ↗";
    }
    return " →";
}

/** @brief Release all styled chunks owned by a single RenderedLine. */
static void rendered_line_free(RenderedLine *rl) {
    if (!rl) return;
    for (size_t i = 0; i < rl->chunk_count; ++i) {
        free(rl->chunks[i].text);
    }
    free(rl->chunks);
    rl->chunks = NULL;
    rl->chunk_count = 0;
    rl->chunk_cap = 0;
}

/** @brief Release all rendered lines and text chunks owned by a RenderedDoc. */
void rendered_doc_free(RenderedDoc *doc) {
    if (!doc) return;
    for (size_t i = 0; i < doc->count; ++i) {
        rendered_line_free(&doc->lines[i]);
    }
    free(doc->lines);
    doc->lines = NULL;
    doc->count = 0;
    doc->cap = 0;
}

/** @brief Append a new RenderedLine mapped back to source_row. */
static RenderedLine *doc_add_line(RenderedDoc *doc, size_t source_row) {
    if (doc->count >= doc->cap) {
        size_t new_cap = doc->cap == 0 ? 32 : doc->cap * 2;
        RenderedLine *new_lines = (RenderedLine *)realloc(doc->lines, new_cap * sizeof(RenderedLine));
        if (!new_lines) return NULL;
        doc->lines = new_lines;
        doc->cap = new_cap;
    }
    RenderedLine *rl = &doc->lines[doc->count++];
    rl->chunks = NULL;
    rl->chunk_count = 0;
    rl->chunk_cap = 0;
    rl->source_row = source_row;
    return rl;
}

/** @brief Append a styled UTF-8 text chunk to a RenderedLine. */
static bool line_add_chunk(RenderedLine *rl, const char *text, size_t len, attr_t attr) {
    if (!rl || !text || len == 0) return true;

    if (rl->chunk_count >= rl->chunk_cap) {
        size_t new_cap = rl->chunk_cap == 0 ? 8 : rl->chunk_cap * 2;
        RenderChunk *new_chunks = (RenderChunk *)realloc(rl->chunks, new_cap * sizeof(RenderChunk));
        if (!new_chunks) return false;
        rl->chunks = new_chunks;
        rl->chunk_cap = new_cap;
    }

    RenderChunk *rc = &rl->chunks[rl->chunk_count++];
    rc->text = (char *)malloc(len + 1);
    if (!rc->text) return false;
    memcpy(rc->text, text, len);
    rc->text[len] = '\0';
    rc->text_len = len;
    rc->attr = attr;
    return true;
}

/**
 * @brief Extract label and URL from a link or image span sequence, returning the next span index.
 */
static size_t parse_link_span_seq(const char *raw_line, const MdParsedLine *pl,
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
            *out_label = raw_line + pl->spans[k].start;
            *out_label_len = pl->spans[k].end - pl->spans[k].start;
        } else if (pl->spans[k].type == MD_LINK_URL) {
            *out_url = raw_line + pl->spans[k].start;
            *out_url_len = pl->spans[k].end - pl->spans[k].start;
        }
        bool done = (pl->spans[k].type == MD_LINK_URL_PAREN &&
                     (raw_line[pl->spans[k].start] == ')' || raw_line[pl->spans[k].start] == ']'));
        k++;
        if (done) break;
    }
    return k;
}

/**
 * @brief Emit formatted inline spans into rl, rendering links with directional indicators.
 */
static void render_inline_spans(RenderedLine *rl, const char *raw_line, const MdParsedLine *pl,
                                size_t start_idx, size_t end_idx,
                                bool use_override_attr, attr_t override_attr) {
    size_t i = start_idx;
    while (i < end_idx) {
        MdSpan *sp = &pl->spans[i];

        if (md_is_header_marker(sp->type) ||
            sp->type == MD_BLOCKQUOTE_MARKER ||
            is_stripped_inline_marker(sp->type)) {
            i++;
            continue;
        }

        // Embedded image: ![alt](url) -> [Img: alt]
        if (sp->type == MD_IMAGE_MARKER) {
            const char *alt, *url;
            size_t alt_len, url_len;
            i = parse_link_span_seq(raw_line, pl, i, end_idx, &alt, &alt_len, &url, &url_len);
            attr_t img_attr = use_override_attr ? override_attr : (COLOR_PAIR(CP_HEADER4) | A_BOLD);
            line_add_chunk(rl, "[Img: ", 6, img_attr);
            if (alt_len > 0) line_add_chunk(rl, alt, alt_len, img_attr);
            else line_add_chunk(rl, "image", 5, img_attr);
            line_add_chunk(rl, "]", 1, img_attr);
            continue;
        }

        // Hyperlink: [label](url) -> underlined label + directional arrow (↗ / → / ↓)
        if (sp->type == MD_LINK_BRACKET && raw_line[sp->start] == '[') {
            const char *label, *url;
            size_t label_len, url_len;
            i = parse_link_span_seq(raw_line, pl, i, end_idx, &label, &label_len, &url, &url_len);
            attr_t link_attr = use_override_attr ? (override_attr | A_UNDERLINE)
                                                 : (COLOR_PAIR(CP_LINK) | A_UNDERLINE);
            attr_t arrow_attr = use_override_attr ? override_attr : (COLOR_PAIR(CP_LINK) | A_BOLD);
            if (label_len > 0) {
                line_add_chunk(rl, label, label_len, link_attr);
            } else if (url_len > 0) {
                line_add_chunk(rl, url, url_len, link_attr);
            }
            const char *ind = link_direction_indicator(url, url_len);
            if (ind[0]) {
                line_add_chunk(rl, ind, strlen(ind), arrow_attr);
            }
            continue;
        }

        if (sp->type == MD_AUTO_LINK) {
            attr_t link_a = use_override_attr ? (override_attr | A_UNDERLINE) : (COLOR_PAIR(CP_LINK) | A_UNDERLINE);
            attr_t arrow_a = use_override_attr ? override_attr : (COLOR_PAIR(CP_LINK) | A_BOLD);
            const char *url_txt = raw_line + sp->start;
            size_t url_l = sp->end - sp->start;
            line_add_chunk(rl, url_txt, url_l, link_a);
            const char *ind = link_direction_indicator(url_txt, url_l);
            if (ind[0]) {
                line_add_chunk(rl, ind, strlen(ind), arrow_a);
            }
            i++;
            continue;
        }

        if (sp->type == MD_FOOTNOTE_REF) {
            attr_t fn_a = use_override_attr ? override_attr : (COLOR_PAIR(CP_HEADER3) | A_BOLD);
            const char *fn_txt = raw_line + sp->start;
            size_t fn_l = sp->end - sp->start;
            line_add_chunk(rl, fn_txt, fn_l, fn_a);
            i++;
            continue;
        }

                attr_t a = use_override_attr ? override_attr : theme_token_attr(sp->type, false);
        line_add_chunk(rl, raw_line + sp->start, sp->end - sp->start, a);
        i++;
    }
}

/**
 * @brief Render inline spans into lines with responsive soft word-wrapping.
 *
 * Words that exceed max_width wrap onto newly allocated RenderedLines,
 * prepending cont_prefix to maintain visual indentation for lists and blockquotes.
 *
 * @param doc Pointer to RenderedDoc destination.
 * @param current_rl Pointer to currently active RenderedLine pointer.
 * @param source_row Line index in source buffer.
 * @param max_width Maximum allowable columns before wrapping.
 * @param cont_prefix Continuation indentation prefix string.
 * @param prefix_len Byte length of continuation prefix.
 * @param prefix_attr Ncurses visual attribute for prefix.
 * @param initial_col Starting column position.
 * @param raw_line Pointer to original raw Markdown line.
 * @param pl Pointer to parsed Markdown spans.
 * @param start_idx Index of first span to render.
 * @param end_idx Index after last span to render.
 * @param use_override_attr If true, override theme styling with override_attr.
 * @param override_attr Explicit attribute override.
 */
static void render_inline_spans_wrapped(RenderedDoc *doc, RenderedLine **current_rl,
                                       size_t source_row, int max_width,
                                       const char *cont_prefix, size_t prefix_len, attr_t prefix_attr,
                                       int initial_col,
                                       const char *raw_line, const MdParsedLine *pl,
                                       size_t start_idx, size_t end_idx,
                                       bool use_override_attr, attr_t override_attr) {
    int cur_col = initial_col;
    size_t i = start_idx;

    while (i < end_idx) {
        MdSpan *sp = &pl->spans[i];
        if (md_is_header_marker(sp->type) ||
            sp->type == MD_BLOCKQUOTE_MARKER ||
            is_stripped_inline_marker(sp->type)) {
            i++;
            continue;
        }

        if (sp->type == MD_IMAGE_MARKER) {
            const char *alt, *url;
            size_t alt_len, url_len;
            i = parse_link_span_seq(raw_line, pl, i, end_idx, &alt, &alt_len, &url, &url_len);
            attr_t img_attr = use_override_attr ? override_attr : (COLOR_PAIR(CP_HEADER4) | A_BOLD);
            line_add_chunk(*current_rl, "[Img: ", 6, img_attr);
            cur_col += 6;
            if (alt_len > 0) {
                line_add_chunk(*current_rl, alt, alt_len, img_attr);
                cur_col += utf8_visual_width_range(alt, 0, alt_len);
            } else {
                line_add_chunk(*current_rl, "image", 5, img_attr);
                cur_col += 5;
            }
            line_add_chunk(*current_rl, "]", 1, img_attr);
            cur_col += 1;
            continue;
        }

        if (sp->type == MD_LINK_BRACKET && raw_line[sp->start] == '[') {
            const char *label, *url;
            size_t label_len, url_len;
            i = parse_link_span_seq(raw_line, pl, i, end_idx, &label, &label_len, &url, &url_len);
            attr_t link_attr = use_override_attr ? (override_attr | A_UNDERLINE) : (COLOR_PAIR(CP_LINK) | A_UNDERLINE);
            attr_t arrow_attr = use_override_attr ? override_attr : (COLOR_PAIR(CP_LINK) | A_BOLD);
            const char *txt = (label_len > 0) ? label : url;
            size_t txt_len = (label_len > 0) ? label_len : url_len;
            if (txt_len > 0) {
                line_add_chunk(*current_rl, txt, txt_len, link_attr);
                cur_col += utf8_visual_width_range(txt, 0, txt_len);
            }
            const char *ind = link_direction_indicator(url, url_len);
            if (ind[0]) {
                size_t ind_l = strlen(ind);
                line_add_chunk(*current_rl, ind, ind_l, arrow_attr);
                cur_col += utf8_visual_width_range(ind, 0, ind_l);
            }
            continue;
        }

        if (sp->type == MD_AUTO_LINK) {
            attr_t link_attr = use_override_attr ? (override_attr | A_UNDERLINE) : (COLOR_PAIR(CP_LINK) | A_UNDERLINE);
            attr_t arrow_attr = use_override_attr ? override_attr : (COLOR_PAIR(CP_LINK) | A_BOLD);
            const char *url_txt = raw_line + sp->start;
            size_t url_l = sp->end - sp->start;
            line_add_chunk(*current_rl, url_txt, url_l, link_attr);
            cur_col += utf8_visual_width_range(url_txt, 0, url_l);
            const char *ind = link_direction_indicator(url_txt, url_l);
            if (ind[0]) {
                size_t ind_l = strlen(ind);
                line_add_chunk(*current_rl, ind, ind_l, arrow_attr);
                cur_col += utf8_visual_width_range(ind, 0, ind_l);
            }
            i++;
            continue;
        }

        attr_t a = use_override_attr ? override_attr : theme_token_attr(sp->type, false);
        const char *span_text = raw_line + sp->start;
        size_t span_len = sp->end - sp->start;

        if (max_width > 12) {
            size_t pos = 0;
            while (pos < span_len) {
                size_t next_pos = pos;
                while (next_pos < span_len && isspace((unsigned char)span_text[next_pos])) next_pos++;
                size_t word_end = next_pos;
                while (word_end < span_len && !isspace((unsigned char)span_text[word_end])) word_end++;

                size_t token_end = (word_end > next_pos) ? word_end : (next_pos < span_len ? next_pos + 1 : span_len);
                size_t token_bytes = token_end - pos;
                int token_w = utf8_visual_width_range(span_text, pos, token_end);

                if (cur_col + token_w > max_width && cur_col > (int)prefix_len) {
                    *current_rl = doc_add_line(doc, source_row);
                    if (cont_prefix && prefix_len > 0) {
                        line_add_chunk(*current_rl, cont_prefix, prefix_len, prefix_attr);
                        cur_col = (int)prefix_len;
                    } else {
                        cur_col = 0;
                    }
                    while (pos < token_end && isspace((unsigned char)span_text[pos])) pos++;
                    if (pos >= token_end) continue;
                    token_bytes = token_end - pos;
                    token_w = utf8_visual_width_range(span_text, pos, token_end);
                }

                line_add_chunk(*current_rl, span_text + pos, token_bytes, a);
                cur_col += token_w;
                pos = token_end;
            }
        } else {
            line_add_chunk(*current_rl, span_text, span_len, a);
            cur_col += utf8_visual_width_range(span_text, 0, span_len);
        }

        i++;
    }
}

static bool is_table_separator(const char *line, size_t len) {
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

/** @brief Extract trimmed plain-text cell strings (markers stripped) from a table row. */
static size_t extract_table_cells(const char *raw_line, size_t raw_len,
                                  char cells[MDE_TABLE_MAX_COLS][256]) {
    size_t col = 0;
    size_t pos = 0;
    while (pos < raw_len && isspace((unsigned char)raw_line[pos])) pos++;
    if (pos < raw_len && raw_line[pos] == '|') pos++;

    size_t end = raw_len;
    while (end > pos && isspace((unsigned char)raw_line[end - 1])) end--;
    if (end > pos && raw_line[end - 1] == '|') end--;

    while (pos <= end && col < MDE_TABLE_MAX_COLS) {
        size_t cstart = pos;
        while (pos < end && raw_line[pos] != '|') pos++;
        size_t cend = pos;

        while (cstart < cend && isspace((unsigned char)raw_line[cstart])) cstart++;
        while (cend > cstart && isspace((unsigned char)raw_line[cend - 1])) cend--;

        // Strip inline formatting markers for accurate cell width and clean table rendering
        bool in_code = false;
        char lang[32] = {0};
        MdParsedLine cpl;
        md_parse_line(raw_line + cstart, cend - cstart, &in_code, lang, sizeof(lang), &cpl);

        size_t out_pos = 0;
        cells[col][0] = '\0';
        for (size_t s = 0; s < cpl.span_count && out_pos + 1 < 256; ) {
            MdSpan *sp = &cpl.spans[s];
            if (is_stripped_inline_marker(sp->type)) { s++; continue; }
            if (sp->type == MD_LINK_BRACKET && raw_line[cstart + sp->start] == '[') {
                const char *lbl, *url;
                size_t ll, ul;
                s = parse_link_span_seq(raw_line + cstart, &cpl, s, cpl.span_count, &lbl, &ll, &url, &ul);
                const char *ind = link_direction_indicator(url, ul);
                if (ll > 0) {
                    size_t n = ll < 255 - out_pos ? ll : 255 - out_pos;
                    memcpy(cells[col] + out_pos, lbl, n);
                    out_pos += n;
                }
                if (ind[0]) {
                    size_t il = strlen(ind);
                    if (out_pos + il < 255) {
                        memcpy(cells[col] + out_pos, ind, il);
                        out_pos += il;
                    }
                }
                continue;
            }
            size_t slen = sp->end - sp->start;
            if (slen > 255 - out_pos) slen = 255 - out_pos;
            memcpy(cells[col] + out_pos, raw_line + cstart + sp->start, slen);
            out_pos += slen;
            s++;
        }
        cells[col][out_pos] = '\0';
        md_parsed_line_free(&cpl);

        col++;
        if (pos >= end) break;
        pos++; // Skip '|'
    }
    return col;
}

/** @brief Emit a box-drawing horizontal border line (`┌─┬─┐`, `├─┼─┤`, or `└─┴─┘`) for a table. */
static void emit_table_border(RenderedDoc *out_doc, size_t source_row,
                              size_t num_cols, const int *col_widths,
                              const char *left, const char *mid, const char *right) {
    RenderedLine *rl = doc_add_line(out_doc, source_row);
    if (!rl) return;
    attr_t border_attr = COLOR_PAIR(CP_HRULE);

    char bar[2048];
    size_t b = 0;
    size_t ll = strlen(left);
    if (ll < sizeof(bar)) {
        memcpy(bar + b, left, ll);
        b += ll;
    }

    for (size_t c = 0; c < num_cols; ++c) {
        int seg = col_widths[c] + 2; // 1 space padding on each side
        for (int k = 0; k < seg && b + 4 < sizeof(bar); ++k) {
            memcpy(bar + b, "─", 3);
            b += 3;
        }
        const char *sep = (c + 1 < num_cols) ? mid : right;
        size_t sl = strlen(sep);
        if (b + sl < sizeof(bar)) {
            memcpy(bar + b, sep, sl);
            b += sl;
        }
    }
    bar[b] = '\0';
    line_add_chunk(rl, bar, b, border_attr);
}

/**
 * @brief Parse a Markdown table delimiter row to detect per-column alignments.
 *
 * Checks cells for leading and trailing colons (`:---:` for center, `---:` for right,
 * and `:---` or `---` for left).
 *
 * @param line Pointer to raw separator row string.
 * @param len Length of separator line in bytes.
 * @param aligns Output array of TableAlign specifiers.
 * @param out_count Pointer to receive detected column count.
 */
static void extract_table_alignments(const char *line, size_t len,
                                    TableAlign aligns[MDE_TABLE_MAX_COLS], size_t *out_count) {
    size_t col = 0;
    size_t pos = 0;
    while (pos < len && isspace((unsigned char)line[pos])) pos++;
    if (pos < len && line[pos] == '|') pos++;

    while (pos < len && col < MDE_TABLE_MAX_COLS) {
        size_t cstart = pos;
        while (pos < len && line[pos] != '|') pos++;
        size_t cend = pos;

        while (cstart < cend && isspace((unsigned char)line[cstart])) cstart++;
        while (cend > cstart && isspace((unsigned char)line[cend - 1])) cend--;

        if (cstart < cend) {
            bool left_colon = (line[cstart] == ':');
            bool right_colon = (line[cend - 1] == ':');
            if (left_colon && right_colon) {
                aligns[col] = ALIGN_CENTER;
            } else if (right_colon) {
                aligns[col] = ALIGN_RIGHT;
            } else {
                aligns[col] = ALIGN_LEFT;
            }
        } else {
            aligns[col] = ALIGN_LEFT;
        }
        col++;
        if (pos >= len) break;
        pos++;
    }
    if (out_count) *out_count = col;
}

/** @brief Truncate/pad a UTF-8 cell string with alignment into dst so it occupies target_cols screen cells. */
static void format_table_cell_aligned(const char *src, int target_cols, TableAlign align,
                                      char *dst, size_t dst_cap) {
    if (!dst || dst_cap == 0) return;
    if (target_cols <= 0) {
        dst[0] = '\0';
        return;
    }

    size_t src_len = strlen(src);
    size_t pos = 0;
    int content_w = 0;

    while (pos < src_len) {
        size_t cl = utf8_char_len((unsigned char)src[pos]);
        if (pos + cl > src_len) cl = src_len - pos;
        int cw = utf8_visual_width_range(src, pos, pos + cl);
        if (content_w + cw > target_cols) break;
        content_w += cw;
        pos += cl;
    }
    size_t content_bytes = pos;

    int total_pad = target_cols - content_w;
    if (total_pad < 0) total_pad = 0;

    int left_pad = 0;
    int right_pad = 0;
    if (align == ALIGN_RIGHT) {
        left_pad = total_pad;
        right_pad = 0;
    } else if (align == ALIGN_CENTER) {
        left_pad = total_pad / 2;
        right_pad = total_pad - left_pad;
    } else {
        left_pad = 0;
        right_pad = total_pad;
    }

    size_t out = 0;
    while (left_pad > 0 && out + 1 < dst_cap) {
        dst[out++] = ' ';
        left_pad--;
    }
    size_t to_copy = content_bytes;
    if (out + to_copy >= dst_cap) {
        to_copy = dst_cap - 1 - out;
    }
    if (to_copy > 0) {
        memcpy(dst + out, src, to_copy);
        out += to_copy;
    }
    while (right_pad > 0 && out + 1 < dst_cap) {
        dst[out++] = ' ';
        right_pad--;
    }
    dst[out] = '\0';
}

/**
 * @brief Render a contiguous block of Markdown table rows [t_start, t_end) with box-drawing borders.
 */
static void render_table_block(const Buffer *buf, size_t t_start, size_t t_end,
                               int width, RenderedDoc *out_doc) {
    int col_widths[MDE_TABLE_MAX_COLS] = {0};
    TableAlign aligns[MDE_TABLE_MAX_COLS];
    for (size_t c = 0; c < MDE_TABLE_MAX_COLS; ++c) aligns[c] = ALIGN_LEFT;
    size_t num_cols = 0;

    // Pass 1: Measure column count, natural visual widths, and column alignments
    for (size_t r = t_start; r < t_end; ++r) {
        const char *l = buffer_line(buf, r);
        size_t len = buffer_line_len(buf, r);
        if (is_table_separator(l, len)) {
            size_t count = 0;
            extract_table_alignments(l, len, aligns, &count);
            continue;
        }

        char cells[MDE_TABLE_MAX_COLS][256];
        size_t nc = extract_table_cells(l, len, cells);
        if (nc > num_cols) num_cols = nc;
        for (size_t c = 0; c < nc; ++c) {
            int w = utf8_visual_width(cells[c], strlen(cells[c]));
            if (w > col_widths[c]) col_widths[c] = w;
        }
    }
    if (num_cols == 0) return;
    for (size_t c = 0; c < num_cols; ++c) {
        if (col_widths[c] < 1) col_widths[c] = 1;
    }

    // Clamp column widths proportionally to fit preview width
    int max_table_w = width > 6 ? (width - 4) : 10;
    int overhead = (int)(num_cols * 3 + 1); // "│ " + " " per col + trailing "│"
    int avail = max_table_w - overhead;
    if (avail < (int)num_cols) avail = (int)num_cols;
    int sum_w = 0;
    for (size_t c = 0; c < num_cols; ++c) sum_w += col_widths[c];
    while (sum_w > avail) {
        size_t widest = 0;
        for (size_t c = 1; c < num_cols; ++c) {
            if (col_widths[c] > col_widths[widest]) widest = c;
        }
        if (col_widths[widest] <= 1) break;
        col_widths[widest]--;
        sum_w--;
    }

    // Pass 2: Emit top border, rows, header divider, and bottom border
    emit_table_border(out_doc, t_start, num_cols, col_widths, "┌", "┬", "┐");

    attr_t border_attr = COLOR_PAIR(CP_HRULE);
    for (size_t r = t_start; r < t_end; ++r) {
        const char *l = buffer_line(buf, r);
        size_t len = buffer_line_len(buf, r);
        if (is_table_separator(l, len)) {
            emit_table_border(out_doc, r, num_cols, col_widths, "├", "┼", "┤");
            continue;
        }

        bool is_header = (r + 1 < t_end &&
                          is_table_separator(buffer_line(buf, r + 1), buffer_line_len(buf, r + 1)));
        attr_t cell_attr = is_header ? (COLOR_PAIR(CP_HEADER1) | A_BOLD) : COLOR_PAIR(CP_DEFAULT);

        char cells[MDE_TABLE_MAX_COLS][256];
        size_t nc = extract_table_cells(l, len, cells);
        RenderedLine *rl = doc_add_line(out_doc, r);
        if (!rl) continue;

        for (size_t c = 0; c < num_cols; ++c) {
            line_add_chunk(rl, "│ ", 4, border_attr);
            char padded[512];
            format_table_cell_aligned(c < nc ? cells[c] : "", col_widths[c], aligns[c], padded, sizeof(padded));
            line_add_chunk(rl, padded, strlen(padded), cell_attr);
            line_add_chunk(rl, " ", 1, cell_attr);
        }
        line_add_chunk(rl, "│", 3, border_attr);
    }

    emit_table_border(out_doc, t_end - 1, num_cols, col_widths, "└", "┴", "┘");
}

/** @brief Render a slice of buffer lines [start_row, end_row) into out_doc. */
void render_document_range(const Buffer *buf, int width, size_t start_row, size_t end_row, RenderedDoc *out_doc) {
    if (!buf || !out_doc) return;
    rendered_doc_free(out_doc);
    rendered_doc_init(out_doc);

    if (width <= 4) width = 80;

    size_t line_count = buffer_line_count(buf);
    if (start_row >= line_count) return;
    if (end_row > line_count) end_row = line_count;
    if (end_row <= start_row) return;

    // Expand start_row if it lands in the middle of a Markdown table block
    while (start_row > 0) {
        bool in_c = false;
        char l_buf[32] = {0};
        MdParsedLine probe;
        md_parse_line(buffer_line(buf, start_row), buffer_line_len(buf, start_row),
                      &in_c, l_buf, sizeof(l_buf), &probe);
        bool is_tbl = probe.is_table_row;
        md_parsed_line_free(&probe);
        if (!is_tbl) break;
        start_row--;
    }

    // Determine initial fenced code block state at start_row
    bool in_code_block = false;
    char code_lang[32] = {0};
    for (size_t r = 0; r < start_row; ++r) {
        const char *l = buffer_line(buf, r);
        size_t len = buffer_line_len(buf, r);
        size_t indent = 0;
        while (indent < len && (l[indent] == ' ' || l[indent] == '\t')) indent++;
        if (indent + 3 <= len &&
            ((l[indent] == '`' && l[indent+1] == '`' && l[indent+2] == '`') ||
             (l[indent] == '~' && l[indent+1] == '~' && l[indent+2] == '~'))) {
            char fence_char = l[indent];
            if (in_code_block) {
                in_code_block = false;
                code_lang[0] = '\0';
            } else {
                in_code_block = true;
                size_t fe = indent;
                while (fe < len && l[fe] == fence_char) fe++;
                while (fe < len && isspace((unsigned char)l[fe])) fe++;
                size_t le = fe;
                while (le < len && !isspace((unsigned char)l[le])) le++;
                size_t n = le > fe ? (le - fe) : 0;
                if (n >= sizeof(code_lang)) n = sizeof(code_lang) - 1;
                memcpy(code_lang, l + fe, n);
                code_lang[n] = '\0';
            }
        }
    }

    for (size_t row = start_row; row < end_row; ++row) {
        const char *raw_line = buffer_line(buf, row);
        size_t raw_len = buffer_line_len(buf, row);

        MdParsedLine pl;
        md_parse_line(raw_line, raw_len, &in_code_block, code_lang, sizeof(code_lang), &pl);

        if (pl.is_code_block) {
            if (pl.span_count > 0 && pl.spans[0].type == MD_CODE_FENCE) {
                RenderedLine *rl = doc_add_line(out_doc, row);
                int max_cols = width > 4 ? (width - 3) : 4;
                if (max_cols > 300) max_cols = 300;
                if (in_code_block) {
                    char fence_bar[1024];
                    if (code_lang[0]) {
                        snprintf(fence_bar, sizeof(fence_bar), "┌── [ %s ] ", code_lang);
                    } else {
                        snprintf(fence_bar, sizeof(fence_bar), "┌── [ code ] ");
                    }
                    int cur_vcols = utf8_visual_width(fence_bar, strlen(fence_bar));
                    size_t bar_bytes = strlen(fence_bar);
                    while (cur_vcols < max_cols && bar_bytes + 4 < sizeof(fence_bar)) {
                        memcpy(fence_bar + bar_bytes, "─", 3);
                        bar_bytes += 3;
                        fence_bar[bar_bytes] = '\0';
                        cur_vcols++;
                    }
                    line_add_chunk(rl, fence_bar, bar_bytes, COLOR_PAIR(CP_MARKER) | A_DIM);
                } else {
                    char fence_bar[1024] = "└──";
                    int cur_vcols = 3;
                    size_t bar_bytes = strlen(fence_bar);
                    while (cur_vcols < max_cols && bar_bytes + 4 < sizeof(fence_bar)) {
                        memcpy(fence_bar + bar_bytes, "─", 3);
                        bar_bytes += 3;
                        fence_bar[bar_bytes] = '\0';
                        cur_vcols++;
                    }
                    line_add_chunk(rl, fence_bar, bar_bytes, COLOR_PAIR(CP_MARKER) | A_DIM);
                }
            } else {
                RenderedLine *rl = doc_add_line(out_doc, row);
                line_add_chunk(rl, "│ ", 4, COLOR_PAIR(CP_MARKER) | A_DIM);
                if (raw_len > 0) {
                    line_add_chunk(rl, raw_line, raw_len, COLOR_PAIR(CP_CODE_BLOCK));
                }
            }
        } else if (pl.is_table_row) {
            // Collect full contiguous table block and render with aligned box borders
            md_parsed_line_free(&pl);
            size_t t_end = row + 1;
            while (t_end < line_count) {
                bool probe_code = false;
                char probe_lang[32] = {0};
                MdParsedLine tpl;
                md_parse_line(buffer_line(buf, t_end), buffer_line_len(buf, t_end),
                              &probe_code, probe_lang, sizeof(probe_lang), &tpl);
                bool is_tbl = tpl.is_table_row;
                md_parsed_line_free(&tpl);
                if (!is_tbl) break;
                t_end++;
            }
            render_table_block(buf, row, t_end, width, out_doc);
            row = t_end - 1;
            continue;
        } else if (pl.is_hrule) {
            RenderedLine *rl = doc_add_line(out_doc, row);
            char hr[1024];
            size_t w = width > 4 ? (size_t)(width - 3) : 4;
            if (w >= sizeof(hr) / 3) w = sizeof(hr) / 3 - 1;
            size_t hr_bytes = 0;
            for (size_t i = 0; i < w; ++i) {
                memcpy(hr + hr_bytes, "─", 3);
                hr_bytes += 3;
            }
            hr[hr_bytes] = '\0';
            line_add_chunk(rl, hr, hr_bytes, COLOR_PAIR(CP_HRULE) | A_BOLD);
        } else if (pl.header_level > 0) {
            RenderedLine *rl = doc_add_line(out_doc, row);
            attr_t head_attr = theme_token_attr((MdTokenType)(MD_HEADER_TEXT_1 + (pl.header_level - 1) * 2), false);
            render_inline_spans(rl, raw_line, &pl, 0, pl.span_count, true, head_attr);

            if (pl.header_level == 1) {
                RenderedLine *rl_bar = doc_add_line(out_doc, row);
                char bar[256];
                size_t bar_w = width > 4 ? (size_t)(width - 4) : 4;
                if (bar_w > 40) bar_w = 40;
                size_t b = 0;
                for (size_t k = 0; k < bar_w; ++k) {
                    memcpy(bar + b, "═", 3);
                    b += 3;
                }
                bar[b] = '\0';
                line_add_chunk(rl_bar, bar, b, COLOR_PAIR(CP_HEADER1) | A_BOLD);
            } else if (pl.header_level == 2) {
                RenderedLine *rl_bar = doc_add_line(out_doc, row);
                char bar[256];
                size_t bar_w = width > 4 ? (size_t)(width - 4) : 4;
                if (bar_w > 30) bar_w = 30;
                size_t b = 0;
                for (size_t k = 0; k < bar_w; ++k) {
                    memcpy(bar + b, "─", 3);
                    b += 3;
                }
                bar[b] = '\0';
                line_add_chunk(rl_bar, bar, b, COLOR_PAIR(CP_HEADER2));
            }
        } else if (pl.is_blockquote) {
            RenderedLine *rl = doc_add_line(out_doc, row);
            line_add_chunk(rl, "▎ ", 4, COLOR_PAIR(CP_HEADER1) | A_BOLD);
            int max_w = width > 4 ? (width - 4) : 80;
            render_inline_spans_wrapped(out_doc, &rl, row, max_w,
                                        "▎ ", 4, COLOR_PAIR(CP_HEADER1) | A_BOLD,
                                        2, raw_line, &pl, 0, pl.span_count, false, 0);
        } else if (pl.is_footnote_def) {
            RenderedLine *rl = doc_add_line(out_doc, row);
            line_add_chunk(rl, "  ⚓ ", 6, COLOR_PAIR(CP_HEADER3) | A_BOLD);
            int max_w = width > 4 ? (width - 4) : 80;
            render_inline_spans_wrapped(out_doc, &rl, row, max_w,
                                        "    ", 4, COLOR_PAIR(CP_DEFAULT),
                                        4, raw_line, &pl, 0, pl.span_count, false, 0);
        } else if (pl.is_list_item) {
            RenderedLine *rl = doc_add_line(out_doc, row);
            size_t body_start = 0;
            int initial_indent = 4;
            char cont_indent[32] = "    ";

            for (size_t i = 0; i < pl.span_count; ++i) {
                MdSpan *sp = &pl.spans[i];
                if (sp->type == MD_LIST_BULLET) {
                    if (pl.is_task) {
                        size_t ind = 0;
                        while (ind < sp->end && (raw_line[ind] == ' ' || raw_line[ind] == '\t')) ind++;
                        if (ind > 0) line_add_chunk(rl, raw_line, ind, COLOR_PAIR(CP_DEFAULT));
                        initial_indent = (int)ind;
                    } else {
                        size_t bpos = sp->start;
                        while (bpos < sp->end && (raw_line[bpos] == ' ' || raw_line[bpos] == '\t')) bpos++;
                        if (bpos < sp->end && isdigit((unsigned char)raw_line[bpos])) {
                            line_add_chunk(rl, raw_line + sp->start, sp->end - sp->start,
                                           COLOR_PAIR(CP_HEADER2) | A_BOLD);
                            initial_indent = (int)(sp->end - sp->start);
                        } else {
                            if (pl.list_indent >= 4) {
                                line_add_chunk(rl, "      ▪ ", strlen("      ▪ "), COLOR_PAIR(CP_HEADER3) | A_BOLD);
                                initial_indent = 8;
                            } else if (pl.list_indent >= 2) {
                                line_add_chunk(rl, "    ◦ ", strlen("    ◦ "), COLOR_PAIR(CP_HEADER2) | A_BOLD);
                                initial_indent = 6;
                            } else {
                                line_add_chunk(rl, "  • ", strlen("  • "), COLOR_PAIR(CP_HEADER2) | A_BOLD);
                                initial_indent = 4;
                            }
                        }
                    }
                    body_start = i + 1;
                } else if (sp->type == MD_TASK_UNCHECKED) {
                    line_add_chunk(rl, "☐  ", 5, COLOR_PAIR(CP_TASK_UNCHECKED) | A_BOLD);
                    initial_indent += 3;
                    body_start = i + 1;
                } else if (sp->type == MD_TASK_CHECKED) {
                    line_add_chunk(rl, "☑  ", 5, COLOR_PAIR(CP_TASK_CHECKED) | A_BOLD);
                    initial_indent += 3;
                    body_start = i + 1;
                } else {
                    break;
                }
            }
            if (initial_indent < 0) initial_indent = 0;
            if (initial_indent > 24) initial_indent = 24;
            memset(cont_indent, ' ', (size_t)initial_indent);
            cont_indent[initial_indent] = 0;
            int max_w = width > 4 ? (width - 4) : 80;
            render_inline_spans_wrapped(out_doc, &rl, row, max_w,
                                        cont_indent, (size_t)initial_indent, COLOR_PAIR(CP_DEFAULT),
                                        initial_indent, raw_line, &pl, body_start, pl.span_count, false, 0);
        } else {
            RenderedLine *rl = doc_add_line(out_doc, row);
            if (raw_len > 0) {
                int max_w = width > 4 ? (width - 4) : 80;
                render_inline_spans_wrapped(out_doc, &rl, row, max_w,
                                            "", 0, COLOR_PAIR(CP_DEFAULT),
                                            0, raw_line, &pl, 0, pl.span_count, false, 0);
            }
        }

        md_parsed_line_free(&pl);
    }
}

/** @brief Render an entire Buffer into styled RenderedDoc lines sized to width. */
void render_document(const Buffer *buf, int width, RenderedDoc *out_doc) {
    if (!buf || !out_doc) return;
    render_document_range(buf, width, 0, buffer_line_count(buf), out_doc);
}

/** @brief Map a 0-indexed buffer row to the first corresponding RenderedDoc line index. */
size_t rendered_doc_find_line(const RenderedDoc *doc, size_t buffer_row) {
    if (!doc || doc->count == 0) return 0;
    for (size_t i = 0; i < doc->count; ++i) {
        if (doc->lines[i].source_row >= buffer_row) {
            return i;
        }
    }
    return doc->count - 1;
}

/** @brief Compute the visual column offset in preview mode matching raw buffer (row, col). */
int renderer_get_cursor_vcol(const Buffer *buf, const RenderedDoc *doc, size_t row, size_t col) {
    if (!buf || !doc || doc->count == 0 || row >= buffer_line_count(buf)) {
        return 0;
    }

    size_t rend_idx = rendered_doc_find_line(doc, row);
    if (rend_idx >= doc->count) return 0;

    const RenderedLine *rl = &doc->lines[rend_idx];
    const char *raw_line = buffer_line(buf, row);
    size_t raw_len = buffer_line_len(buf, row);

    if (col >= raw_len) {
        int total_w = 0;
        for (size_t c = 0; c < rl->chunk_count; ++c) {
            total_w += utf8_visual_width_range(rl->chunks[c].text, 0, rl->chunks[c].text_len);
        }
        return total_w;
    }

    bool in_code = false;
    char code_lang[32] = {0};
    MdParsedLine pl;
    md_parse_line(raw_line, raw_len, &in_code, code_lang, sizeof(code_lang), &pl);

    int vcol = 0;

    if (pl.is_blockquote) {
        vcol += 2; // "▎ "
    } else if (pl.is_code_block) {
        if (!pl.span_count || pl.spans[0].type != MD_CODE_FENCE) {
            vcol += 2; // "│ "
        }
    } else if (pl.is_list_item) {
        if (pl.is_task) {
            size_t ind = 0;
            while (ind < raw_len && (raw_line[ind] == ' ' || raw_line[ind] == '\t')) ind++;
            vcol += (int)ind + 3; // checkbox + spaces
        } else if (pl.span_count > 0 && pl.spans[0].type == MD_LIST_BULLET &&
                   pl.spans[0].end > pl.spans[0].start && isdigit((unsigned char)raw_line[pl.spans[0].start])) {
            vcol += utf8_visual_width_range(raw_line, pl.spans[0].start, pl.spans[0].end);
        } else {
            vcol += 4; // "  • "
        }
    }

    for (size_t s = 0; s < pl.span_count; ) {
        MdSpan *sp = &pl.spans[s];

        if (md_is_header_marker(sp->type) ||
            sp->type == MD_BLOCKQUOTE_MARKER ||
            (sp->type == MD_LIST_BULLET && !isdigit((unsigned char)raw_line[sp->start])) ||
            sp->type == MD_TASK_UNCHECKED ||
            sp->type == MD_TASK_CHECKED ||
            is_stripped_inline_marker(sp->type)) {
            s++;
            continue;
        }

        // Handle rendered link sequence: [label](url) -> label + " ↗"/" →"/" ↓"
        if (sp->type == MD_LINK_BRACKET && raw_line[sp->start] == '[') {
            const char *lbl, *url;
            size_t ll, ul;
            size_t next_s = parse_link_span_seq(raw_line, &pl, s, pl.span_count, &lbl, &ll, &url, &ul);
            size_t seq_end = pl.spans[next_s - 1].end;
            if (col < sp->start) break;
            if (col <= seq_end) {
                // Inside label text: map proportionally; inside URL: clamp to end of rendered label
                if (ll > 0) {
                    size_t lbl_start = (size_t)(lbl - raw_line);
                    if (col >= lbl_start && col <= lbl_start + ll) {
                        vcol += utf8_visual_width_range(raw_line, lbl_start, col);
                    } else {
                        vcol += utf8_visual_width(lbl, ll);
                    }
                }
                break;
            }
            vcol += (ll > 0 ? utf8_visual_width(lbl, ll) : utf8_visual_width(url, ul));
            const char *ind = link_direction_indicator(url, ul);
            if (ind[0]) vcol += utf8_visual_width(ind, strlen(ind));
            s = next_s;
            continue;
        }

        if (col < sp->start) {
            break;
        } else if (col <= sp->end) {
            vcol += utf8_visual_width_range(raw_line, sp->start, col);
            break;
        } else {
            vcol += utf8_visual_width_range(raw_line, sp->start, sp->end);
        }
        s++;
    }

    md_parsed_line_free(&pl);
    return vcol;
}
