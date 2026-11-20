/**
 * @file renderer.c
 * @brief Implementation of Markdown preview generation and formatting.
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
#include <ctype.h>

void rendered_doc_init(RenderedDoc *doc) {
    if (!doc) return;
    doc->lines = NULL;
    doc->count = 0;
    doc->cap = 0;
}

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

static RenderedLine *doc_add_line(RenderedDoc *doc, size_t source_row) {
    if (doc->count >= doc->cap) {
        size_t new_cap = doc->cap == 0 ? 32 : doc->cap * 2;
        RenderedLine *new_lines = (RenderedLine*)realloc(doc->lines, new_cap * sizeof(RenderedLine));
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

static bool line_add_chunk(RenderedLine *rl, const char *text, size_t len, attr_t attr) {
    if (!rl || !text || len == 0) return true;

    if (rl->chunk_count >= rl->chunk_cap) {
        size_t new_cap = rl->chunk_cap == 0 ? 8 : rl->chunk_cap * 2;
        RenderChunk *new_chunks = (RenderChunk*)realloc(rl->chunks, new_cap * sizeof(RenderChunk));
        if (!new_chunks) return false;
        rl->chunks = new_chunks;
        rl->chunk_cap = new_cap;
    }

    RenderChunk *rc = &rl->chunks[rl->chunk_count++];
    rc->text = (char*)malloc(len + 1);
    if (!rc->text) return false;
    memcpy(rc->text, text, len);
    rc->text[len] = '\0';
    rc->text_len = len;
    rc->attr = attr;
    return true;
}

void render_document(const Buffer *buf, int width, RenderedDoc *out_doc) {
    if (!buf || !out_doc) return;
    rendered_doc_free(out_doc);
    rendered_doc_init(out_doc);

    if (width <= 4) width = 80;

    bool in_code_block = false;
    char code_lang[32] = {0};

    size_t line_count = buffer_line_count(buf);

    for (size_t row = 0; row < line_count; ++row) {
        const char *raw_line = buffer_line(buf, row);
        size_t raw_len = buffer_line_len(buf, row);

        MdParsedLine pl;
        md_parse_line(raw_line, raw_len, &in_code_block, code_lang, sizeof(code_lang), &pl);

        if (pl.is_code_block) {
            if (pl.span_count > 0 && pl.spans[0].type == MD_CODE_FENCE) {
                // Code fence header / footer scaled to preview width
                RenderedLine *rl = doc_add_line(out_doc, row);
                int max_cols = width > 4 ? (width - 3) : 4;
                if (max_cols > 300) max_cols = 300;
                if (in_code_block) {
                    // Opening fence: ┌── [ lang ] ───────────────────────
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
                    // Closing fence: └───────────────────────────────────
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
                // Interior code line: │ code
                RenderedLine *rl = doc_add_line(out_doc, row);
                line_add_chunk(rl, "│ ", 4, COLOR_PAIR(CP_MARKER) | A_DIM);
                if (raw_len > 0) {
                    line_add_chunk(rl, raw_line, raw_len, COLOR_PAIR(CP_CODE_BLOCK));
                }
            }
        } else if (pl.is_hrule) {
            // Horizontal rule: smooth full-width line
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
            // Rendered header: omit raw '#' markers, render title cleanly
            RenderedLine *rl = doc_add_line(out_doc, row);
            attr_t head_attr = theme_token_attr((MdTokenType)(MD_HEADER_TEXT_1 + (pl.header_level - 1) * 2), false);

            for (size_t i = 0; i < pl.span_count; ++i) {
                MdSpan *sp = &pl.spans[i];
                if (md_is_header_marker(sp->type)) {
                    continue; // Skip the raw '#' characters in preview
                }
                const char *chunk_txt = raw_line + sp->start;
                size_t chunk_len = sp->end - sp->start;

                // Strip bold/italic markers inside headers for clean look
                if (sp->type == MD_BOLD_MARKER || sp->type == MD_ITALIC_MARKER ||
                    sp->type == MD_BOLD_ITALIC_MARKER || sp->type == MD_INLINE_CODE_MARKER) {
                    continue;
                }
                line_add_chunk(rl, chunk_txt, chunk_len, head_attr);
            }

            // Decorative underline for H1 and H2
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
            // Rendered blockquote: replace > with solid bar ▎
            RenderedLine *rl = doc_add_line(out_doc, row);
            line_add_chunk(rl, "▎ ", 4, COLOR_PAIR(CP_HEADER1) | A_BOLD);

            for (size_t i = 0; i < pl.span_count; ++i) {
                MdSpan *sp = &pl.spans[i];
                if (sp->type == MD_BLOCKQUOTE_MARKER) continue;
                if (sp->type == MD_BOLD_MARKER || sp->type == MD_ITALIC_MARKER ||
                    sp->type == MD_BOLD_ITALIC_MARKER || sp->type == MD_INLINE_CODE_MARKER) {
                    continue;
                }
                attr_t a = theme_token_attr(sp->type, false);
                line_add_chunk(rl, raw_line + sp->start, sp->end - sp->start, a);
            }
        } else if (pl.is_list_item) {
            RenderedLine *rl = doc_add_line(out_doc, row);

            for (size_t i = 0; i < pl.span_count; ++i) {
                MdSpan *sp = &pl.spans[i];
                if (sp->type == MD_LIST_BULLET) {
                    if (pl.is_task) {
                        size_t ind = 0;
                        while (ind < sp->end && (raw_line[ind] == ' ' || raw_line[ind] == '\t')) ind++;
                        if (ind > 0) {
                            line_add_chunk(rl, raw_line, ind, COLOR_PAIR(CP_DEFAULT));
                        }
                    } else {
                        if (sp->end > sp->start && isdigit((unsigned char)raw_line[sp->start])) {
                            line_add_chunk(rl, raw_line + sp->start, sp->end - sp->start,
                                           COLOR_PAIR(CP_HEADER2) | A_BOLD);
                        } else {
                            line_add_chunk(rl, "  • ", 6, COLOR_PAIR(CP_HEADER2) | A_BOLD);
                        }
                    }
                    continue;
                }
                if (sp->type == MD_TASK_UNCHECKED) {
                    line_add_chunk(rl, "☐  ", 5, COLOR_PAIR(CP_TASK_UNCHECKED) | A_BOLD);
                    continue;
                }
                if (sp->type == MD_TASK_CHECKED) {
                    line_add_chunk(rl, "☑  ", 5, COLOR_PAIR(CP_TASK_CHECKED) | A_BOLD);
                    continue;
                }
                if (sp->type == MD_BOLD_MARKER || sp->type == MD_ITALIC_MARKER ||
                    sp->type == MD_BOLD_ITALIC_MARKER || sp->type == MD_INLINE_CODE_MARKER) {
                    continue;
                }
                attr_t a = theme_token_attr(sp->type, false);
                line_add_chunk(rl, raw_line + sp->start, sp->end - sp->start, a);
            }
        } else {
            // Regular paragraph line
            RenderedLine *rl = doc_add_line(out_doc, row);
            if (raw_len > 0) {
                for (size_t i = 0; i < pl.span_count; ++i) {
                    MdSpan *sp = &pl.spans[i];
                    if (sp->type == MD_BOLD_MARKER || sp->type == MD_ITALIC_MARKER ||
                        sp->type == MD_BOLD_ITALIC_MARKER || sp->type == MD_INLINE_CODE_MARKER) {
                        continue;
                    }
                    attr_t a = theme_token_attr(sp->type, false);
                    line_add_chunk(rl, raw_line + sp->start, sp->end - sp->start, a);
                }
            }
        }

        md_parsed_line_free(&pl);
    }
}

size_t rendered_doc_find_line(const RenderedDoc *doc, size_t buffer_row) {
    if (!doc || doc->count == 0) return 0;
    for (size_t i = 0; i < doc->count; ++i) {
        if (doc->lines[i].source_row >= buffer_row) {
            return i;
        }
    }
    return doc->count - 1;
}

int renderer_get_cursor_vcol(const Buffer *buf, const RenderedDoc *doc, size_t row, size_t col) {
    if (!buf || !doc || doc->count == 0 || row >= buffer_line_count(buf)) {
        return 0;
    }

    size_t rend_idx = rendered_doc_find_line(doc, row);
    if (rend_idx >= doc->count) return 0;

    const RenderedLine *rl = &doc->lines[rend_idx];
    const char *raw_line = buffer_line(buf, row);
    size_t raw_len = buffer_line_len(buf, row);

    // If cursor is at or past end of raw line, position cursor at the end of rendered line
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

    // Check for rendered prefixes that are prepended to the line
    if (pl.is_blockquote) {
        vcol += 2; // "▎ "
    } else if (pl.is_code_block) {
        if (!pl.span_count || pl.spans[0].type != MD_CODE_FENCE) {
            vcol += 2; // "│ "
        }
    } else if (pl.is_list_item) {
        if (pl.is_task) {
            // Task has leading whitespace + "☐  " or "☑  "
            size_t ind = 0;
            while (ind < raw_len && (raw_line[ind] == ' ' || raw_line[ind] == '\t')) ind++;
            vcol += (int)ind + 3; // checkbox is 3 visual columns
        } else {
            // Unordered list has "  • " (4 columns) or ordered number
            if (pl.span_count > 0 && pl.spans[0].type == MD_LIST_BULLET &&
                pl.spans[0].end > pl.spans[0].start && isdigit((unsigned char)raw_line[pl.spans[0].start])) {
                vcol += utf8_visual_width_range(raw_line, pl.spans[0].start, pl.spans[0].end);
            } else {
                vcol += 4; // "  • "
            }
        }
    }

    // Now walk the spans up to 'col'
    for (size_t s = 0; s < pl.span_count; ++s) {
        MdSpan *sp = &pl.spans[s];

        // Skip markers that the renderer strips
        if (md_is_header_marker(sp->type) ||
            sp->type == MD_BLOCKQUOTE_MARKER ||
            (sp->type == MD_LIST_BULLET && !isdigit((unsigned char)raw_line[sp->start])) ||
            sp->type == MD_TASK_UNCHECKED ||
            sp->type == MD_TASK_CHECKED ||
            sp->type == MD_BOLD_MARKER ||
            sp->type == MD_ITALIC_MARKER ||
            sp->type == MD_BOLD_ITALIC_MARKER ||
            sp->type == MD_INLINE_CODE_MARKER) {
            continue;
        }

        if (col < sp->start) {
            // Cursor is before this span (e.g. inside a stripped marker); place at start of this span
            break;
        } else if (col <= sp->end) {
            // Cursor is inside this span
            vcol += utf8_visual_width_range(raw_line, sp->start, col);
            break;
        } else {
            // Cursor is past this span
            vcol += utf8_visual_width_range(raw_line, sp->start, sp->end);
        }
    }

    md_parsed_line_free(&pl);
    return vcol;
}
