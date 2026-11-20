/**
 * @file markdown.c
 * @brief Implementation of Markdown syntax lexing and token span extraction.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "markdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void md_parsed_line_init(MdParsedLine *pl) {
    if (!pl) return;
    pl->spans = NULL;
    pl->span_count = 0;
    pl->span_cap = 0;
    pl->is_code_block = false;
    pl->is_hrule = false;
    pl->header_level = 0;
    pl->is_list_item = false;
    pl->is_task = false;
    pl->task_checked = false;
    pl->task_bracket_start = 0;
    pl->is_blockquote = false;
    pl->is_table_row = false;
    pl->code_lang[0] = '\0';
}

void md_parsed_line_free(MdParsedLine *pl) {
    if (!pl) return;
    free(pl->spans);
    pl->spans = NULL;
    pl->span_count = 0;
    pl->span_cap = 0;
}

bool md_parsed_line_add_span(MdParsedLine *pl, size_t start, size_t end, MdTokenType type) {
    if (!pl || start >= end) return false;

    if (pl->span_count >= pl->span_cap) {
        size_t new_cap = pl->span_cap == 0 ? 16 : pl->span_cap * 2;
        MdSpan *new_spans = (MdSpan*)realloc(pl->spans, new_cap * sizeof(MdSpan));
        if (!new_spans) return false;
        pl->spans = new_spans;
        pl->span_cap = new_cap;
    }

    pl->spans[pl->span_count].start = start;
    pl->spans[pl->span_count].end = end;
    pl->spans[pl->span_count].type = type;
    pl->span_count++;
    return true;
}

bool md_is_hrule(const char *line, size_t len) {
    if (!line || len < 3) return false;
    size_t i = 0;
    while (i < len && isspace((unsigned char)line[i])) i++;
    if (i >= len) return false;

    char c = line[i];
    if (c != '-' && c != '*' && c != '_') return false;

    size_t count = 0;
    while (i < len) {
        if (line[i] == c) {
            count++;
        } else if (!isspace((unsigned char)line[i])) {
            return false;
        }
        i++;
    }
    return count >= 3;
}

bool md_detect_list(const char *line, size_t len,
                    size_t *out_prefix_len,
                    char *out_bullet, size_t bullet_cap,
                    bool *out_is_task, bool *out_task_checked) {
    if (!line || len == 0) return false;

    size_t indent = 0;
    while (indent < len && (line[indent] == ' ' || line[indent] == '\t')) indent++;
    if (indent >= len) return false;

    // Check unordered bullet: '-', '*', '+'
    if ((line[indent] == '-' || line[indent] == '*' || line[indent] == '+') &&
        (indent + 1 < len && isspace((unsigned char)line[indent + 1]))) {
        
        size_t bullet_end = indent + 1;
        while (bullet_end < len && line[bullet_end] == ' ') bullet_end++;

        // Check for task checkbox: [ ] or [x]
        if (bullet_end + 3 <= len && line[bullet_end] == '[' && line[bullet_end + 2] == ']') {
            char check = line[bullet_end + 1];
            if (check == ' ' || check == 'x' || check == 'X') {
                if (out_is_task) *out_is_task = true;
                if (out_task_checked) *out_task_checked = (check == 'x' || check == 'X');

                size_t task_end = bullet_end + 3;
                if (task_end < len && line[task_end] == ' ') task_end++;
                if (out_prefix_len) *out_prefix_len = task_end;
                if (out_bullet && bullet_cap > 0) {
                    snprintf(out_bullet, bullet_cap, "%.*s", (int)(bullet_end - indent), line + indent);
                }
                return true;
            }
        }

        if (out_is_task) *out_is_task = false;
        if (out_task_checked) *out_task_checked = false;
        if (out_prefix_len) *out_prefix_len = bullet_end;
        if (out_bullet && bullet_cap > 0) {
            snprintf(out_bullet, bullet_cap, "%c ", line[indent]);
        }
        return true;
    }

    // Check ordered bullet: '1. ', '12. '
    if (isdigit((unsigned char)line[indent])) {
        size_t num_end = indent;
        while (num_end < len && isdigit((unsigned char)line[num_end])) num_end++;
        if (num_end < len && (line[num_end] == '.' || line[num_end] == ')') &&
            num_end + 1 < len && isspace((unsigned char)line[num_end + 1])) {
            
            size_t prefix_end = num_end + 1;
            while (prefix_end < len && line[prefix_end] == ' ') prefix_end++;

            if (out_is_task) *out_is_task = false;
            if (out_task_checked) *out_task_checked = false;
            if (out_prefix_len) *out_prefix_len = prefix_end;
            if (out_bullet && bullet_cap > 0) {
                snprintf(out_bullet, bullet_cap, "%.*s", (int)(prefix_end - indent), line + indent);
            }
            return true;
        }
    }

    return false;
}

/**
 * @brief Parse inline formatting (bold, italic, code, links, images) within a text range.
 */
static void parse_inlines(const char *text, size_t text_len, size_t base_offset,
                          MdParsedLine *pl, MdTokenType default_type) {
    if (!text || text_len == 0) return;

    size_t i = 0;
    size_t plain_start = 0;

    while (i < text_len) {
        // Escaped character (\*)
        if (text[i] == '\\' && i + 1 < text_len) {
            i += 2;
            continue;
        }

        // Inline code: `code`
        if (text[i] == '`') {
            size_t code_start = i;
            size_t j = i + 1;
            while (j < text_len && text[j] != '`') j++;
            if (j < text_len) {
                if (code_start > plain_start) {
                    md_parsed_line_add_span(pl, base_offset + plain_start, base_offset + code_start, default_type);
                }
                md_parsed_line_add_span(pl, base_offset + code_start, base_offset + code_start + 1, MD_INLINE_CODE_MARKER);
                if (j > code_start + 1) {
                    md_parsed_line_add_span(pl, base_offset + code_start + 1, base_offset + j, MD_INLINE_CODE_TEXT);
                }
                md_parsed_line_add_span(pl, base_offset + j, base_offset + j + 1, MD_INLINE_CODE_MARKER);
                i = j + 1;
                plain_start = i;
                continue;
            }
        }

        // Image: ![alt](url)
        if (text[i] == '!' && i + 1 < text_len && text[i + 1] == '[') {
            size_t close_bracket = i + 2;
            while (close_bracket < text_len && text[close_bracket] != ']') close_bracket++;
            if (close_bracket < text_len && close_bracket + 1 < text_len && text[close_bracket + 1] == '(') {
                size_t close_paren = close_bracket + 2;
                while (close_paren < text_len && text[close_paren] != ')') close_paren++;
                if (close_paren < text_len) {
                    if (i > plain_start) {
                        md_parsed_line_add_span(pl, base_offset + plain_start, base_offset + i, default_type);
                    }
                    md_parsed_line_add_span(pl, base_offset + i, base_offset + i + 1, MD_IMAGE_MARKER);
                    md_parsed_line_add_span(pl, base_offset + i + 1, base_offset + i + 2, MD_LINK_BRACKET);
                    if (close_bracket > i + 2) {
                        md_parsed_line_add_span(pl, base_offset + i + 2, base_offset + close_bracket, MD_LINK_TEXT);
                    }
                    md_parsed_line_add_span(pl, base_offset + close_bracket, base_offset + close_bracket + 1, MD_LINK_BRACKET);
                    md_parsed_line_add_span(pl, base_offset + close_bracket + 1, base_offset + close_bracket + 2, MD_LINK_URL_PAREN);
                    if (close_paren > close_bracket + 2) {
                        md_parsed_line_add_span(pl, base_offset + close_bracket + 2, base_offset + close_paren, MD_LINK_URL);
                    }
                    md_parsed_line_add_span(pl, base_offset + close_paren, base_offset + close_paren + 1, MD_LINK_URL_PAREN);
                    i = close_paren + 1;
                    plain_start = i;
                    continue;
                }
            }
        }

        // Link: [text](url)
        if (text[i] == '[') {
            size_t close_bracket = i + 1;
            while (close_bracket < text_len && text[close_bracket] != ']') close_bracket++;
            if (close_bracket < text_len && close_bracket + 1 < text_len && text[close_bracket + 1] == '(') {
                size_t close_paren = close_bracket + 2;
                while (close_paren < text_len && text[close_paren] != ')') close_paren++;
                if (close_paren < text_len) {
                    if (i > plain_start) {
                        md_parsed_line_add_span(pl, base_offset + plain_start, base_offset + i, default_type);
                    }
                    md_parsed_line_add_span(pl, base_offset + i, base_offset + i + 1, MD_LINK_BRACKET);
                    if (close_bracket > i + 1) {
                        md_parsed_line_add_span(pl, base_offset + i + 1, base_offset + close_bracket, MD_LINK_TEXT);
                    }
                    md_parsed_line_add_span(pl, base_offset + close_bracket, base_offset + close_bracket + 1, MD_LINK_BRACKET);
                    md_parsed_line_add_span(pl, base_offset + close_bracket + 1, base_offset + close_bracket + 2, MD_LINK_URL_PAREN);
                    if (close_paren > close_bracket + 2) {
                        md_parsed_line_add_span(pl, base_offset + close_bracket + 2, base_offset + close_paren, MD_LINK_URL);
                    }
                    md_parsed_line_add_span(pl, base_offset + close_paren, base_offset + close_paren + 1, MD_LINK_URL_PAREN);
                    i = close_paren + 1;
                    plain_start = i;
                    continue;
                }
            }
        }

        // Bold + Italic: ***text***
        if (i + 2 < text_len && text[i] == '*' && text[i + 1] == '*' && text[i + 2] == '*') {
            size_t match = i + 3;
            while (match + 2 < text_len && !(text[match] == '*' && text[match + 1] == '*' && text[match + 2] == '*')) match++;
            if (match + 2 < text_len) {
                if (i > plain_start) {
                    md_parsed_line_add_span(pl, base_offset + plain_start, base_offset + i, default_type);
                }
                md_parsed_line_add_span(pl, base_offset + i, base_offset + i + 3, MD_BOLD_ITALIC_MARKER);
                if (match > i + 3) {
                    md_parsed_line_add_span(pl, base_offset + i + 3, base_offset + match, MD_BOLD_ITALIC_TEXT);
                }
                md_parsed_line_add_span(pl, base_offset + match, base_offset + match + 3, MD_BOLD_ITALIC_MARKER);
                i = match + 3;
                plain_start = i;
                continue;
            }
        }

        // Bold: **text** or __text__
        if (i + 1 < text_len && ((text[i] == '*' && text[i + 1] == '*') || (text[i] == '_' && text[i + 1] == '_'))) {
            char marker = text[i];
            size_t match = i + 2;
            while (match + 1 < text_len && !(text[match] == marker && text[match + 1] == marker)) match++;
            if (match + 1 < text_len) {
                if (i > plain_start) {
                    md_parsed_line_add_span(pl, base_offset + plain_start, base_offset + i, default_type);
                }
                md_parsed_line_add_span(pl, base_offset + i, base_offset + i + 2, MD_BOLD_MARKER);
                if (match > i + 2) {
                    md_parsed_line_add_span(pl, base_offset + i + 2, base_offset + match, MD_BOLD_TEXT);
                }
                md_parsed_line_add_span(pl, base_offset + match, base_offset + match + 2, MD_BOLD_MARKER);
                i = match + 2;
                plain_start = i;
                continue;
            }
        }

        // Strikethrough: ~~text~~
        if (i + 1 < text_len && text[i] == '~' && text[i + 1] == '~') {
            size_t match = i + 2;
            while (match + 1 < text_len && !(text[match] == '~' && text[match + 1] == '~')) match++;
            if (match + 1 < text_len) {
                if (i > plain_start) {
                    md_parsed_line_add_span(pl, base_offset + plain_start, base_offset + i, default_type);
                }
                md_parsed_line_add_span(pl, base_offset + i, base_offset + i + 2, MD_STRIKE_MARKER);
                if (match > i + 2) {
                    md_parsed_line_add_span(pl, base_offset + i + 2, base_offset + match, MD_STRIKE_TEXT);
                }
                md_parsed_line_add_span(pl, base_offset + match, base_offset + match + 2, MD_STRIKE_MARKER);
                i = match + 2;
                plain_start = i;
                continue;
            }
        }

        // Italic: *text* or _text_
        if ((text[i] == '*' || text[i] == '_') && (i == 0 || text[i - 1] != text[i])) {
            char marker = text[i];
            size_t match = i + 1;
            while (match < text_len && text[match] != marker) match++;
            if (match < text_len && match > i + 1) {
                if (i > plain_start) {
                    md_parsed_line_add_span(pl, base_offset + plain_start, base_offset + i, default_type);
                }
                md_parsed_line_add_span(pl, base_offset + i, base_offset + i + 1, MD_ITALIC_MARKER);
                md_parsed_line_add_span(pl, base_offset + i + 1, base_offset + match, MD_ITALIC_TEXT);
                md_parsed_line_add_span(pl, base_offset + match, base_offset + match + 1, MD_ITALIC_MARKER);
                i = match + 1;
                plain_start = i;
                continue;
            }
        }

        i++;
    }

    if (plain_start < text_len) {
        md_parsed_line_add_span(pl, base_offset + plain_start, base_offset + text_len, default_type);
    }
}

void md_parse_line(const char *line, size_t len,
                   bool *in_code_block, char *code_lang, size_t lang_cap,
                   MdParsedLine *out_line) {
    if (!out_line) return;
    md_parsed_line_init(out_line);
    if (!line || len == 0) return;

    size_t indent = 0;
    while (indent < len && (line[indent] == ' ' || line[indent] == '\t')) indent++;

    // Code fence delimiter: ``` or ~~~
    if (indent + 3 <= len &&
        ((line[indent] == '`' && line[indent + 1] == '`' && line[indent + 2] == '`') ||
         (line[indent] == '~' && line[indent + 1] == '~' && line[indent + 2] == '~'))) {
        
        char fence_char = line[indent];
        size_t fence_end = indent;
        while (fence_end < len && line[fence_end] == fence_char) fence_end++;

        if (in_code_block && *in_code_block) {
            // Closing code block fence
            *in_code_block = false;
            if (code_lang && lang_cap > 0) code_lang[0] = '\0';
            md_parsed_line_add_span(out_line, 0, len, MD_CODE_FENCE);
            out_line->is_code_block = true;
            return;
        } else {
            // Opening code block fence
            if (in_code_block) *in_code_block = true;
            out_line->is_code_block = true;

            md_parsed_line_add_span(out_line, 0, fence_end, MD_CODE_FENCE);

            // Extract language identifier
            size_t lang_start = fence_end;
            while (lang_start < len && isspace((unsigned char)line[lang_start])) lang_start++;
            size_t lang_end = lang_start;
            while (lang_end < len && !isspace((unsigned char)line[lang_end])) lang_end++;

            if (lang_end > lang_start) {
                md_parsed_line_add_span(out_line, lang_start, lang_end, MD_CODE_FENCE_LANG);
                if (code_lang && lang_cap > 0) {
                    size_t l = lang_end - lang_start;
                    if (l >= lang_cap) l = lang_cap - 1;
                    memcpy(code_lang, line + lang_start, l);
                    code_lang[l] = '\0';
                    strncpy(out_line->code_lang, code_lang, sizeof(out_line->code_lang) - 1);
                }
            }
            if (lang_end < len) {
                md_parsed_line_add_span(out_line, lang_end, len, MD_TEXT);
            }
            return;
        }
    }

    // Inside an active fenced code block
    if (in_code_block && *in_code_block) {
        out_line->is_code_block = true;
        if (code_lang && code_lang[0]) {
            strncpy(out_line->code_lang, code_lang, sizeof(out_line->code_lang) - 1);
        }
        md_parsed_line_add_span(out_line, 0, len, MD_CODE_BLOCK_LINE);
        return;
    }

    // Horizontal rule
    if (md_is_hrule(line, len)) {
        out_line->is_hrule = true;
        md_parsed_line_add_span(out_line, 0, len, MD_HRULE);
        return;
    }

    // Headings: # through ######
    if (indent < len && line[indent] == '#') {
        size_t h = indent;
        while (h < len && line[h] == '#') h++;
        int level = (int)(h - indent);

        if (level >= 1 && level <= 6 && (h == len || isspace((unsigned char)line[h]))) {
            out_line->header_level = level;
            MdTokenType marker_type = (MdTokenType)(MD_HEADER_MARKER_1 + (level - 1) * 2);
            MdTokenType text_type = (MdTokenType)(MD_HEADER_TEXT_1 + (level - 1) * 2);

            size_t marker_end = h;
            while (marker_end < len && line[marker_end] == ' ') marker_end++;

            if (marker_end > 0) {
                md_parsed_line_add_span(out_line, 0, marker_end, marker_type);
            }
            if (marker_end < len) {
                parse_inlines(line + marker_end, len - marker_end, marker_end, out_line, text_type);
            }
            return;
        }
    }

    // Blockquote: >
    if (indent < len && line[indent] == '>') {
        out_line->is_blockquote = true;
        size_t b_end = indent + 1;
        while (b_end < len && (line[b_end] == '>' || line[b_end] == ' ')) b_end++;

        md_parsed_line_add_span(out_line, 0, b_end, MD_BLOCKQUOTE_MARKER);
        if (b_end < len) {
            parse_inlines(line + b_end, len - b_end, b_end, out_line, MD_BLOCKQUOTE_TEXT);
        }
        return;
    }

    // List and Task item
    size_t prefix_len = 0;
    char bullet[16] = {0};
    bool is_task = false;
    bool task_checked = false;
    if (md_detect_list(line, len, &prefix_len, bullet, sizeof(bullet), &is_task, &task_checked)) {
        out_line->is_list_item = true;
        out_line->is_task = is_task;
        out_line->task_checked = task_checked;

        if (is_task) {
            size_t bracket_pos = indent;
            while (bracket_pos < prefix_len && line[bracket_pos] != '[') bracket_pos++;
            out_line->task_bracket_start = bracket_pos;

            if (bracket_pos > 0) {
                md_parsed_line_add_span(out_line, 0, bracket_pos, MD_LIST_BULLET);
            }
            size_t box_end = bracket_pos + 3;
            if (box_end <= prefix_len) {
                md_parsed_line_add_span(out_line, bracket_pos, box_end,
                                        task_checked ? MD_TASK_CHECKED : MD_TASK_UNCHECKED);
            }
            if (box_end < prefix_len) {
                md_parsed_line_add_span(out_line, box_end, prefix_len, MD_TEXT);
            }
        } else {
            md_parsed_line_add_span(out_line, 0, prefix_len, MD_LIST_BULLET);
        }

        if (prefix_len < len) {
            parse_inlines(line + prefix_len, len - prefix_len, prefix_len, out_line, MD_TEXT);
        }
        return;
    }

    // Table row detection (| header | header |)
    if (indent < len && line[indent] == '|') {
        out_line->is_table_row = true;
        size_t cur = 0;
        while (cur < len) {
            if (line[cur] == '|') {
                md_parsed_line_add_span(out_line, cur, cur + 1, MD_TABLE_BORDER);
                cur++;
            } else {
                size_t cell_start = cur;
                while (cur < len && line[cur] != '|') cur++;
                parse_inlines(line + cell_start, cur - cell_start, cell_start, out_line, MD_TABLE_CELL);
            }
        }
        return;
    }

    // Regular text paragraph
    parse_inlines(line, len, 0, out_line, MD_TEXT);
}
