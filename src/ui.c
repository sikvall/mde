/**
 * @file ui.c
 * @brief Implementation of user interface, layout, column ruler, and dialogs.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _XOPEN_SOURCE_EXTENDED 1
#include "ui.h"
#include "theme.h"
#include "utf8.h"
#include "markdown.h"
#include "version.h"
#include <locale.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

void ui_init(void) {
    setlocale(LC_ALL, "");
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(1);
    fputs("\033[2 q", stdout);
    fflush(stdout);
    theme_init();
    mousemask(ALL_MOUSE_EVENTS, NULL);
}

void ui_shutdown(void) {
    fputs("\033[0 q", stdout);
    fflush(stdout);
    curs_set(1);
    endwin();
}

/**
 * @brief Compute display width required for line numbers based on total line count.
 */
static int get_line_num_width(size_t max_lines) {
    int digits = 1;
    while (max_lines >= 10) {
        digits++;
        max_lines /= 10;
    }
    return digits < 3 ? 3 : digits;
}

/**
 * @brief Draw the horizontal column ruler at the top of the edit window.
 *
 * Aligns precisely with text columns, displaying ticks every column, pluses (+)
 * every 5 columns, tens digits (1, 2, 3...) every 10 columns, a wrap guide at
 * column 80, and a cursor position marker (▼) above the active column.
 */
static void draw_column_ruler(Editor *ed, int start_col, int width, int screen_y, int gutter_w) {
    // Gutter header: " COL │ "
    attron(COLOR_PAIR(CP_RULER));
    for (int x = start_col; x < start_col + gutter_w - 2; ++x) {
        mvaddch(screen_y, x, ' ');
    }
    mvaddstr(screen_y, start_col + 1, "COL");
    attroff(COLOR_PAIR(CP_RULER));

    attron(COLOR_PAIR(CP_DIVIDER));
    mvaddstr(screen_y, start_col + gutter_w - 2, "│ ");
    attroff(COLOR_PAIR(CP_DIVIDER));

    const char *cur_line = buffer_line(&ed->buffer, ed->cursor_row);
    int cursor_vcol = utf8_byte_to_col(cur_line, ed->cursor_col);

    int content_w = width - gutter_w;
    int render_x = start_col + gutter_w;

    for (int i = 0; i < content_w; ++i) {
        int vcol = (int)ed->scroll_col + i; // 0-indexed column
        int col_num = vcol + 1;             // 1-indexed column

        if (vcol == cursor_vcol) {
            // Active cursor column indicator on ruler
            attron(COLOR_PAIR(CP_RULER_CURSOR) | A_BOLD);
            mvaddstr(screen_y, render_x + i, "▼");
            attroff(COLOR_PAIR(CP_RULER_CURSOR) | A_BOLD);
        } else if (col_num == 80) {
            // Column 80 guide
            attron(COLOR_PAIR(CP_RULER_LIMIT) | A_BOLD);
            mvaddstr(screen_y, render_x + i, "┆");
            attroff(COLOR_PAIR(CP_RULER_LIMIT) | A_BOLD);
        } else if (col_num % 10 == 0) {
            // Tens mark: show tens digit
            attron(COLOR_PAIR(CP_RULER) | A_BOLD);
            int tens = (col_num / 10) % 10;
            mvprintw(screen_y, render_x + i, "%d", tens);
            attroff(COLOR_PAIR(CP_RULER) | A_BOLD);
        } else if (col_num % 5 == 0) {
            // Five mark (+)
            attron(COLOR_PAIR(CP_RULER));
            mvaddstr(screen_y, render_x + i, "+");
            attroff(COLOR_PAIR(CP_RULER));
        } else {
            // Subtle tick (·)
            attron(COLOR_PAIR(CP_RULER) | A_DIM);
            mvaddstr(screen_y, render_x + i, "·");
            attroff(COLOR_PAIR(CP_RULER) | A_DIM);
        }
    }
}

/**
 * @brief Render the text editing pane with line numbers, active line marker, column ruler, and cursor.
 */
static void draw_editor_pane(Editor *ed, int start_col, int width, int start_row, int height) {
    if (width <= 0 || height <= 0) return;

    size_t total_lines = buffer_line_count(&ed->buffer);
    int num_w = get_line_num_width(total_lines);
    if (num_w < 3) num_w = 3;
    int gutter_w = num_w + 4; // " ▶ 12 │ "
    int content_w = width - gutter_w;
    if (content_w <= 0) content_w = 1;

    // Draw Column Ruler on the first row of the edit window
    draw_column_ruler(ed, start_col, width, start_row, gutter_w);

    int text_start_y = start_row + 1;
    int text_height = height - 1;

    // Track multi-line code block state up to scroll_row
    bool in_code_block = false;
    char code_lang[32] = {0};
    for (size_t r = 0; r < ed->scroll_row; ++r) {
        const char *l = buffer_line(&ed->buffer, r);
        size_t len = buffer_line_len(&ed->buffer, r);
        MdParsedLine pl;
        md_parse_line(l, len, &in_code_block, code_lang, sizeof(code_lang), &pl);
        md_parsed_line_free(&pl);
    }

    for (int y = 0; y < text_height; ++y) {
        size_t row_idx = ed->scroll_row + (size_t)y;
        int screen_y = text_start_y + y;

        if (row_idx < total_lines) {
            bool is_cur_row = (row_idx == ed->cursor_row);

            // Draw gutter & line number with active line marker
            if (is_cur_row) {
                attron(COLOR_PAIR(CP_GUTTER_MARKER) | A_BOLD);
                mvaddstr(screen_y, start_col, "▶");
                mvprintw(screen_y, start_col + 1, " %*zu ", num_w, row_idx + 1);
                attroff(COLOR_PAIR(CP_GUTTER_MARKER) | A_BOLD);
            } else {
                attron(COLOR_PAIR(CP_LINE_NUM));
                mvprintw(screen_y, start_col, "  %*zu ", num_w, row_idx + 1);
                attroff(COLOR_PAIR(CP_LINE_NUM));
            }

            attron(COLOR_PAIR(CP_DIVIDER));
            mvaddstr(screen_y, start_col + gutter_w - 2, "│ ");
            attroff(COLOR_PAIR(CP_DIVIDER));

            // Parse markdown for this line
            const char *raw = buffer_line(&ed->buffer, row_idx);
            size_t raw_len = buffer_line_len(&ed->buffer, row_idx);

            MdParsedLine pl;
            md_parse_line(raw, raw_len, &in_code_block, code_lang, sizeof(code_lang), &pl);

            int cur_vcol = 0;
            int render_x = start_col + gutter_w;

            // Render spans (pass is_cur_row so markers stand out vividly when active)
            for (size_t s = 0; s < pl.span_count; ++s) {
                MdSpan *sp = &pl.spans[s];
                attr_t a = theme_token_attr(sp->type, is_cur_row);
                attron(a);

                size_t byte_pos = sp->start;
                while (byte_pos < sp->end && render_x < start_col + width) {
                    size_t ch_len = utf8_char_len((unsigned char)raw[byte_pos]);
                    int ch_w = utf8_visual_width_range(raw, byte_pos, byte_pos + ch_len);

                    if (cur_vcol >= (int)ed->scroll_col && render_x + ch_w <= start_col + width) {
                        if (ed->search_active && row_idx == ed->search_match_row &&
                            byte_pos >= ed->search_match_col &&
                            byte_pos < ed->search_match_col + ed->search_match_len) {
                            // Active search match highlight
                            attron(COLOR_PAIR(CP_SEARCH_CUR) | A_BOLD);
                            mvaddnstr(screen_y, render_x, raw + byte_pos, ch_len);
                            attroff(COLOR_PAIR(CP_SEARCH_CUR) | A_BOLD);
                            attron(a);
                        } else {
                            mvaddnstr(screen_y, render_x, raw + byte_pos, ch_len);
                        }
                        render_x += ch_w;
                    }

                    cur_vcol += ch_w;
                    byte_pos += ch_len;
                }

                attroff(a);
            }

            // Clear rest of line in editor pane
            while (render_x < start_col + width) {
                mvaddch(screen_y, render_x++, ' ');
            }

            md_parsed_line_free(&pl);
        } else {
            // Beyond end of file (tilde ~)
            attron(COLOR_PAIR(CP_MARKER) | A_DIM);
            mvprintw(screen_y, start_col, "  %*s ~", num_w, "");
            attroff(COLOR_PAIR(CP_MARKER) | A_DIM);

            for (int x = start_col + num_w + 4; x < start_col + width; ++x) {
                mvaddch(screen_y, x, ' ');
            }
        }
    }
}

/**
 * @brief Render the formatted Markdown preview pane.
 */
static void draw_preview_pane(Editor *ed, int start_col, int width, int start_row, int height) {
    if (width <= 0 || height <= 0) return;

    // Header bar for preview pane
    attron(COLOR_PAIR(CP_RULER));
    mvaddstr(start_row, start_col, "  PREVIEW");
    int rx = start_col + 9;
    while (rx < start_col + width) {
        mvaddch(start_row, rx++, ' ');
    }
    attroff(COLOR_PAIR(CP_RULER));

    int text_start_y = start_row + 1;
    int text_height = height - 1;
    size_t total_rendered = ed->rendered_doc.count;

    for (int y = 0; y < text_height; ++y) {
        size_t rend_idx = ed->preview_scroll_row + (size_t)y;
        int screen_y = text_start_y + y;
        int render_x = start_col;

        // Left margin padding
        mvaddstr(screen_y, render_x, "  ");
        render_x += 2;

        if (rend_idx < total_rendered) {
            RenderedLine *rl = &ed->rendered_doc.lines[rend_idx];

            for (size_t c = 0; c < rl->chunk_count; ++c) {
                RenderChunk *rc = &rl->chunks[c];
                if (render_x >= start_col + width) break;

                attron(rc->attr);
                size_t byte_pos = 0;
                while (byte_pos < rc->text_len && render_x < start_col + width) {
                    size_t ch_len = utf8_char_len((unsigned char)rc->text[byte_pos]);
                    int ch_w = utf8_visual_width_range(rc->text, byte_pos, byte_pos + ch_len);

                    if (render_x + ch_w <= start_col + width) {
                        mvaddnstr(screen_y, render_x, rc->text + byte_pos, ch_len);
                        render_x += ch_w;
                    }
                    byte_pos += ch_len;
                }
                attroff(rc->attr);
            }
        }

        // Clear rest of line in preview pane
        while (render_x < start_col + width) {
            mvaddch(screen_y, render_x++, ' ');
        }
    }
}

/**
 * @brief Render the bottom status bar with file, position, word count, and mode.
 */
static void draw_status_bar(Editor *ed, int screen_rows, int screen_cols) {
    int y = screen_rows - 2;

    attron(COLOR_PAIR(CP_STATUS_BAR));
    for (int x = 0; x < screen_cols; ++x) {
        mvaddch(y, x, ' ');
    }

    // View Mode badge
    const char *mode_str = "[ SPLIT ]";
    if (ed->view_mode == VIEW_INPLACE) mode_str = "[ IN-PLACE ]";
    else if (ed->view_mode == VIEW_PREVIEW) mode_str = "[ PREVIEW ]";

    attron(COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_BOLD);
    mvaddstr(y, 1, mode_str);
    attroff(COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_BOLD);

    // File name and dirty marker
    attron(COLOR_PAIR(CP_STATUS_BAR));
    const char *fname = ed->buffer.file_path ? ed->buffer.file_path : "[No Name]";
    mvprintw(y, 14, " %s ", fname);

    if (ed->buffer.is_dirty) {
        attron(COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD);
        mvaddstr(y, 14 + strlen(fname) + 2, "[*]");
        attroff(COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD);
    }

    // Right-aligned stats
    char right_buf[128];
    const char *cur_line = buffer_line(&ed->buffer, ed->cursor_row);
    int visual_col = utf8_byte_to_col(cur_line, ed->cursor_col) + 1;
    snprintf(right_buf, sizeof(right_buf), "%s | Ln %zu, Col %d | %zu words | UTF-8",
             ed->insert_mode ? "INS" : "NORM",
             ed->cursor_row + 1, visual_col, buffer_word_count(&ed->buffer));

    int right_len = (int)strlen(right_buf);
    int right_x = screen_cols - right_len - 2;
    if (right_x > 25) {
        attron(COLOR_PAIR(CP_STATUS_BAR));
        mvaddstr(y, right_x, right_buf);
    }
    attroff(COLOR_PAIR(CP_STATUS_BAR));
}

/**
 * @brief Render the bottom message line (status notification or key shortcuts).
 */
static void draw_message_bar(Editor *ed, int screen_rows, int screen_cols) {
    int y = screen_rows - 1;

    move(y, 0);
    clrtoeol();

    if (ed->status_msg[0] && time(NULL) - ed->status_time < 5) {
        attron(COLOR_PAIR(CP_DEFAULT) | A_BOLD);
        mvaddnstr(y, 1, ed->status_msg, screen_cols - 2);
        attroff(COLOR_PAIR(CP_DEFAULT) | A_BOLD);
    } else {
        attron(COLOR_PAIR(CP_MARKER) | A_DIM);
        mvaddstr(y, 1, "Ctrl+S: Save | Ctrl+Q: Quit | Ctrl+P: View | Ctrl+T: Checkbox | Ctrl+F: Find | Ctrl+H: Help");
        attroff(COLOR_PAIR(CP_MARKER) | A_DIM);
    }
}

static void update_cursor_shape(bool insert_mode) {
    static int s_last_cursor_mode = -1;
    if (s_last_cursor_mode != (int)insert_mode) {
        s_last_cursor_mode = (int)insert_mode;
        if (insert_mode) {
            fputs("\033[4 q", stdout); // DECSCUSR 4: steady underline cursor
        } else {
            fputs("\033[2 q", stdout); // DECSCUSR 2: steady block cursor
        }
        fflush(stdout);
    }
}

void ui_draw(Editor *ed) {
    if (!ed) return;

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    ed->screen_rows = rows;
    ed->screen_cols = cols;

    int body_height = rows - 2;
    if (body_height < 2) body_height = 2;

    // Body height minus 1 row for ruler
    editor_update_scroll(ed, body_height - 1);
    editor_refresh_rendered(ed);

    switch (ed->view_mode) {
        case VIEW_SPLIT: {
            int split_col = cols / 2;
            ed->editor_width = split_col;
            ed->preview_width = cols - split_col - 1;

            draw_editor_pane(ed, 0, split_col, 0, body_height);

            // Center vertical divider
            attron(COLOR_PAIR(CP_DIVIDER));
            for (int y = 0; y < body_height; ++y) {
                mvaddstr(y, split_col, "│");
            }
            attroff(COLOR_PAIR(CP_DIVIDER));

            draw_preview_pane(ed, split_col + 1, cols - split_col - 1, 0, body_height);
            break;
        }
        case VIEW_INPLACE: {
            ed->editor_width = cols;
            ed->preview_width = 0;
            draw_editor_pane(ed, 0, cols, 0, body_height);
            break;
        }
        case VIEW_PREVIEW: {
            ed->editor_width = 0;
            ed->preview_width = cols;
            draw_preview_pane(ed, 0, cols, 0, body_height);
            break;
        }
    }

    draw_status_bar(ed, rows, cols);
    draw_message_bar(ed, rows, cols);

    // Position terminal hardware cursor directly on screen
    int text_start_y = 1; // Row 0 is column ruler or preview header
    int text_height = body_height - 1;

    if (ed->view_mode == VIEW_PREVIEW) {
        // Fullscreen rendered editor: place cursor in rendered preview pane
        size_t rend_idx = rendered_doc_find_line(&ed->rendered_doc, ed->cursor_row);

        if (rend_idx >= ed->preview_scroll_row &&
            rend_idx < ed->preview_scroll_row + (size_t)text_height) {
            
            int vcol = renderer_get_cursor_vcol(&ed->buffer, &ed->rendered_doc, ed->cursor_row, ed->cursor_col);
            int cur_x = 2 + vcol; // 2 spaces left margin in draw_preview_pane
            int cur_y = text_start_y + (int)(rend_idx - ed->preview_scroll_row);

            if (cur_x >= 2 && cur_x < cols &&
                cur_y >= text_start_y && cur_y < text_start_y + text_height) {
                curs_set(1);
                update_cursor_shape(ed->insert_mode);
                wmove(stdscr, cur_y, cur_x);
            } else {
                curs_set(0);
            }
        } else {
            curs_set(0);
        }
    } else {
        // VIEW_SPLIT or VIEW_INPLACE: place cursor in editing pane
        size_t total_lines = buffer_line_count(&ed->buffer);
        int num_w = get_line_num_width(total_lines);
        if (num_w < 3) num_w = 3;
        int gutter_w = num_w + 4; // " ▶ 12 │ "

        if (ed->cursor_row >= ed->scroll_row &&
            ed->cursor_row < ed->scroll_row + (size_t)text_height) {
            
            const char *cur_line = buffer_line(&ed->buffer, ed->cursor_row);
            int cursor_vcol = utf8_byte_to_col(cur_line, ed->cursor_col);
            int cur_x = gutter_w + (cursor_vcol - (int)ed->scroll_col);
            int cur_y = text_start_y + (int)(ed->cursor_row - ed->scroll_row);

            int editor_max_x = ed->editor_width;
            if (cur_x >= gutter_w && cur_x < editor_max_x &&
                cur_y >= text_start_y && cur_y < text_start_y + text_height) {
                curs_set(1);
                update_cursor_shape(ed->insert_mode);
                wmove(stdscr, cur_y, cur_x);
            } else {
                curs_set(0);
            }
        } else {
            curs_set(0);
        }
    }

    refresh();
}

bool ui_prompt(Editor *ed, const char *prompt, char *out_buf, size_t buf_size) {
    if (!ed || !prompt || !out_buf || buf_size == 0) return false;

    out_buf[0] = '\0';
    size_t len = 0;
    int y = ed->screen_rows - 1;

    curs_set(1);

    while (1) {
        move(y, 0);
        clrtoeol();
        attron(COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_BOLD);
        mvprintw(y, 1, "%s: ", prompt);
        attroff(COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_BOLD);

        attron(COLOR_PAIR(CP_DEFAULT));
        mvaddstr(y, (int)strlen(prompt) + 3, out_buf);
        attroff(COLOR_PAIR(CP_DEFAULT));

        refresh();

        int ch = getch();
        if (ch == ERR) continue;
        if (ch == 27) { // Escape
            out_buf[0] = '\0';
            return false;
        } else if (ch == '\n' || ch == '\r') {
            return len > 0;
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (len > 0) {
                size_t prev = utf8_prev_char(out_buf, len);
                out_buf[prev] = '\0';
                len = prev;
            }
        } else if (ch >= 32 && ch <= 126) {
            if (len + 1 < buf_size) {
                out_buf[len++] = (char)ch;
                out_buf[len] = '\0';
            }
        }
    }
}

int ui_confirm_quit(Editor *ed) {
    if (!ed || !ed->buffer.is_dirty) return 0;

    int y = ed->screen_rows - 1;
    move(y, 0);
    clrtoeol();

    attron(COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD);
    mvaddstr(y, 1, "Unsaved changes! Save before quit? (y = Save & Quit, n = Discard, Esc = Cancel): ");
    attroff(COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD);
    refresh();

    while (1) {
        int ch = getch();
        if (ch == ERR) continue;
        if (ch == 'y' || ch == 'Y') {
            return 1;
        } else if (ch == 'n' || ch == 'N') {
            return 0;
        } else if (ch == 27 || ch == 3) {
            return -1;
        }
    }
}

typedef enum {
    HELP_LINE_SECTION,
    HELP_LINE_ITEM,
    HELP_LINE_NOTE,
    HELP_LINE_BLANK
} HelpLineType;

typedef struct {
    HelpLineType type;
    const char *col1;
    const char *col2;
} HelpEntry;

static const HelpEntry s_editor_help[] = {
    { HELP_LINE_SECTION, "FILE & SESSION", NULL },
    { HELP_LINE_ITEM, "Ctrl+S", "Save current document to disk" },
    { HELP_LINE_ITEM, "Ctrl+O", "Open / load file into buffer" },
    { HELP_LINE_ITEM, "Ctrl+Q", "Quit editor (prompts if unsaved changes)" },
    { HELP_LINE_ITEM, "Ctrl+H / F1", "Open / close this Help & Reference overlay" },
    { HELP_LINE_BLANK, NULL, NULL },
    { HELP_LINE_SECTION, "VIEW MODES & CURSOR", NULL },
    { HELP_LINE_ITEM, "Ctrl+P / F2", "Cycle view: Split -> In-Place -> Preview" },
    { HELP_LINE_ITEM, "Ins", "Toggle mode: Insert (steady _) / Normal (steady █)" },
    { HELP_LINE_BLANK, NULL, NULL },
    { HELP_LINE_SECTION, "EDITING & FORMATTING", NULL },
    { HELP_LINE_ITEM, "Ctrl+Z", "Undo last editing operation" },
    { HELP_LINE_ITEM, "Ctrl+Y", "Redo undone editing operation" },
    { HELP_LINE_ITEM, "Ctrl+T", "Toggle task checkbox: - [ ] <-> - [x]" },
    { HELP_LINE_ITEM, "Ctrl+B", "Insert Markdown bold markers (****)" },
    { HELP_LINE_ITEM, "Ctrl+K", "Insert Markdown inline code backticks (``)" },
    { HELP_LINE_ITEM, "Enter", "Smart Enter: auto-continues lists, tasks & indents" },
    { HELP_LINE_ITEM, "Tab", "Indent current line or block by 2 spaces" },
    { HELP_LINE_ITEM, "Shift+Tab", "Unindent current line or block by 2 spaces" },
    { HELP_LINE_BLANK, NULL, NULL },
    { HELP_LINE_SECTION, "SEARCH & NAVIGATION", NULL },
    { HELP_LINE_ITEM, "Ctrl+F", "Find / search text pattern in document" },
    { HELP_LINE_ITEM, "F3", "Repeat search: jump to next occurrence" },
    { HELP_LINE_ITEM, "Arrow Keys", "Navigate cursor by character / line" },
    { HELP_LINE_ITEM, "Home / End", "Jump to beginning / end of current line" },
    { HELP_LINE_ITEM, "PgUp / PgDn", "Scroll viewport up / down by page" }
};

static const HelpEntry s_markdown_help[] = {
    { HELP_LINE_SECTION, "HEADINGS & THEMATIC BREAKS", NULL },
    { HELP_LINE_ITEM, "# H1 Heading", "Top-level title (bold, cyan accent)" },
    { HELP_LINE_ITEM, "## H2 Heading", "Section header (yellow accent)" },
    { HELP_LINE_ITEM, "### H3 Heading", "Subsection header (green accent)" },
    { HELP_LINE_ITEM, "####..######", "Sub-levels 4 to 6 (magenta, blue, white)" },
    { HELP_LINE_ITEM, "--- or ***", "Thematic break / horizontal divider line" },
    { HELP_LINE_BLANK, NULL, NULL },
    { HELP_LINE_SECTION, "TEXT EMPHASIS & STYLING", NULL },
    { HELP_LINE_ITEM, "**text** / __text__", "Bold / strong emphasis" },
    { HELP_LINE_ITEM, "*text* / _text_", "Italic emphasis" },
    { HELP_LINE_ITEM, "***text***", "Combined bold and italic emphasis" },
    { HELP_LINE_ITEM, "~~text~~", "Strikethrough text" },
    { HELP_LINE_ITEM, "`code`", "Inline monospaced code span" },
    { HELP_LINE_BLANK, NULL, NULL },
    { HELP_LINE_SECTION, "LISTS & TASK CHECKBOXES", NULL },
    { HELP_LINE_ITEM, "- item / * item", "Unordered bullet list (nest with 2 spaces)" },
    { HELP_LINE_ITEM, "1. item", "Ordered numbered list (auto-numbers on Enter)" },
    { HELP_LINE_ITEM, "- [ ] Task", "Unchecked to-do item" },
    { HELP_LINE_ITEM, "- [x] Done", "Completed task (press Ctrl+T to toggle)" },
    { HELP_LINE_BLANK, NULL, NULL },
    { HELP_LINE_SECTION, "BLOCKS & CODE FENCES", NULL },
    { HELP_LINE_ITEM, "> Quote text", "Blockquote with visual vertical bar" },
    { HELP_LINE_ITEM, "```lang", "Fenced code block opening (c, py, js, sh, etc.)" },
    { HELP_LINE_ITEM, "```", "Closing code block fence" },
    { HELP_LINE_BLANK, NULL, NULL },
    { HELP_LINE_SECTION, "LINKS, IMAGES & TABLES", NULL },
    { HELP_LINE_ITEM, "[Label](url)", "Hyperlink to web URL or local anchor" },
    { HELP_LINE_ITEM, "![Alt text](path)", "Embedded image reference" },
    { HELP_LINE_ITEM, "| Col 1 | Col 2 |", "Table header column delimiters" },
    { HELP_LINE_ITEM, "|-------|-------|", "Table header separator row" },
    { HELP_LINE_ITEM, "| Val 1 | Val 2 |", "Table content cell row" },
    { HELP_LINE_BLANK, NULL, NULL },
    { HELP_LINE_SECTION, "SMART SYNTAX & RENDERING", NULL },
    { HELP_LINE_NOTE, "Active Markers", "Syntax markers (#, **) glow when editing line" },
    { HELP_LINE_NOTE, "Live Preview", "Press Ctrl+P to toggle live rendered preview" },
    { HELP_LINE_NOTE, "Smart Lists", "Enter on empty list item automatically exits list" }
};

void ui_show_help(Editor *ed) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int box_h = 26;
    int box_w = 76;
    if (rows >= 32) box_h = 28;
    else if (box_h > rows - 2) box_h = rows - 2;
    if (box_w > cols - 2) box_w = cols - 2;
    if (box_h < 12) box_h = 12;
    if (box_w < 50) box_w = 50;

    int start_y = (rows - box_h) / 2;
    int start_x = (cols - box_w) / 2;

    WINDOW *win = newwin(box_h, box_w, start_y, start_x);
    if (!win) return;
    keypad(win, TRUE);
    wbkgd(win, COLOR_PAIR(CP_HELP_BOX));
    curs_set(0);

    int active_tab = 0; // 0 = Editor Cheat Sheet, 1 = Markdown Guide
    int scroll_y[2] = {0, 0};

    while (1) {
        werase(win);
        box(win, 0, 0);

        // Header Title
        wattron(win, COLOR_PAIR(CP_HEADER1) | A_BOLD);
        char title_buf[80];
        snprintf(title_buf, sizeof(title_buf), "%s v%s - Help & Reference", MDE_APP_NAME, MDE_VERSION_STRING);
        int title_x = (box_w - (int)strlen(title_buf)) / 2;
        if (title_x < 2) title_x = 2;
        mvwaddstr(win, 1, title_x, title_buf);
        wattroff(win, COLOR_PAIR(CP_HEADER1) | A_BOLD);

        // Tab Bar on row 2
        int tab1_x = 4;
        int tab2_x = 32;
        if (box_w < 70) {
            tab1_x = 2;
            tab2_x = (box_w / 2);
        }

        if (active_tab == 0) {
            wattron(win, COLOR_PAIR(CP_STATUS_BAR) | A_BOLD);
            mvwaddstr(win, 2, tab1_x, "  1: Editor Cheat Sheet  ");
            wattroff(win, COLOR_PAIR(CP_STATUS_BAR) | A_BOLD);

            wattron(win, COLOR_PAIR(CP_HELP_BOX) | A_DIM);
            mvwaddstr(win, 2, tab2_x, "  2: Markdown Guide  ");
            wattroff(win, COLOR_PAIR(CP_HELP_BOX) | A_DIM);
        } else {
            wattron(win, COLOR_PAIR(CP_HELP_BOX) | A_DIM);
            mvwaddstr(win, 2, tab1_x, "  1: Editor Cheat Sheet  ");
            wattroff(win, COLOR_PAIR(CP_HELP_BOX) | A_DIM);

            wattron(win, COLOR_PAIR(CP_STATUS_BAR) | A_BOLD);
            mvwaddstr(win, 2, tab2_x, "  2: Markdown Guide  ");
            wattroff(win, COLOR_PAIR(CP_STATUS_BAR) | A_BOLD);
        }

        // Horizontal divider under tab bar on row 3
        wmove(win, 3, 0);
        waddch(win, ACS_LTEE);
        for (int x = 1; x < box_w - 1; ++x) {
            waddch(win, ACS_HLINE);
        }
        waddch(win, ACS_RTEE);

        // Content area between row 4 and row box_h - 4
        int visible_lines = box_h - 7;
        if (visible_lines < 1) visible_lines = 1;

        const HelpEntry *entries = (active_tab == 0) ? s_editor_help : s_markdown_help;
        size_t total_entries = (active_tab == 0) ?
            (sizeof(s_editor_help) / sizeof(s_editor_help[0])) :
            (sizeof(s_markdown_help) / sizeof(s_markdown_help[0]));

        int max_scroll = (int)total_entries - visible_lines;
        if (max_scroll < 0) max_scroll = 0;
        if (scroll_y[active_tab] > max_scroll) scroll_y[active_tab] = max_scroll;

        for (int i = 0; i < visible_lines; ++i) {
            int cur_y = 4 + i;
            int entry_idx = scroll_y[active_tab] + i;

            if (entry_idx < (int)total_entries) {
                const HelpEntry *entry = &entries[entry_idx];
                switch (entry->type) {
                    case HELP_LINE_SECTION:
                        wattron(win, COLOR_PAIR(CP_HEADER2) | A_BOLD);
                        mvwprintw(win, cur_y, 3, "▸ %s", entry->col1);
                        wattroff(win, COLOR_PAIR(CP_HEADER2) | A_BOLD);
                        break;
                    case HELP_LINE_ITEM:
                        wattron(win, COLOR_PAIR(CP_HELP_KEY) | A_BOLD);
                        mvwprintw(win, cur_y, 5, "%-20s", entry->col1);
                        wattroff(win, COLOR_PAIR(CP_HELP_KEY) | A_BOLD);

                        wattron(win, COLOR_PAIR(CP_HELP_BOX));
                        if (box_w > 30) {
                            int max_col2 = (box_w - 2) - 26;
                            if (max_col2 > 0) {
                                mvwprintw(win, cur_y, 26, "%.*s", max_col2, entry->col2);
                            }
                        }
                        wattroff(win, COLOR_PAIR(CP_HELP_BOX));
                        break;
                    case HELP_LINE_NOTE:
                        wattron(win, COLOR_PAIR(CP_HEADER3) | A_BOLD);
                        mvwprintw(win, cur_y, 5, "%-20s", entry->col1);
                        wattroff(win, COLOR_PAIR(CP_HEADER3) | A_BOLD);

                        wattron(win, COLOR_PAIR(CP_HELP_BOX));
                        if (box_w > 30) {
                            int max_col2 = (box_w - 2) - 26;
                            if (max_col2 > 0) {
                                mvwprintw(win, cur_y, 26, "%.*s", max_col2, entry->col2);
                            }
                        }
                        wattroff(win, COLOR_PAIR(CP_HELP_BOX));
                        break;
                    case HELP_LINE_BLANK:
                    default:
                        break;
                }
            }
        }

        // Horizontal divider above footer on row box_h - 3
        wmove(win, box_h - 3, 0);
        waddch(win, ACS_LTEE);
        for (int x = 1; x < box_w - 1; ++x) {
            waddch(win, ACS_HLINE);
        }
        waddch(win, ACS_RTEE);

        // Scroll indicator if content overflows
        if (total_entries > (size_t)visible_lines) {
            char scroll_info[64];
            int end_line = scroll_y[active_tab] + visible_lines;
            if (end_line > (int)total_entries) end_line = (int)total_entries;
            snprintf(scroll_info, sizeof(scroll_info), " [ %sLine %d-%d of %zu%s ] ",
                     scroll_y[active_tab] > 0 ? "▲ " : "",
                     scroll_y[active_tab] + 1,
                     end_line,
                     total_entries,
                     (scroll_y[active_tab] + visible_lines < (int)total_entries) ? " ▼" : "");
            int info_len = (int)strlen(scroll_info);
            int info_x = (box_w - info_len) / 2;
            if (info_x > 1) {
                wattron(win, COLOR_PAIR(CP_MARKER) | A_BOLD);
                mvwaddstr(win, box_h - 3, info_x, scroll_info);
                wattroff(win, COLOR_PAIR(CP_MARKER) | A_BOLD);
            }
        }

        // Footer hint on row box_h - 2
        wattron(win, COLOR_PAIR(CP_MARKER) | A_DIM);
        const char *footer = "Tab/1-2/←→: Switch Tab │ ↑/↓/PgUp/PgDn: Scroll │ Esc/q: Close";
        int footer_len = (int)strlen(footer);
        int footer_x = (box_w - footer_len) / 2;
        if (footer_x < 2) footer_x = 2;
        mvwaddstr(win, box_h - 2, footer_x, footer);
        wattroff(win, COLOR_PAIR(CP_MARKER) | A_DIM);

        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;

        if (ch == 27 || ch == 'q' || ch == 'Q' || ch == KEY_F(1) || ch == 8 || ch == KEY_RESIZE) {
            break;
        } else if (ch == '\n' || ch == '\r') {
            break;
        } else if (ch == '\t' || ch == KEY_BTAB || ch == KEY_RIGHT) {
            active_tab = (active_tab + 1) % 2;
        } else if (ch == KEY_LEFT) {
            active_tab = (active_tab + 1) % 2;
        } else if (ch == '1') {
            active_tab = 0;
        } else if (ch == '2') {
            active_tab = 1;
        } else if (ch == KEY_UP || ch == 'k' || ch == 'K') {
            if (scroll_y[active_tab] > 0) {
                scroll_y[active_tab]--;
            }
        } else if (ch == KEY_DOWN || ch == 'j' || ch == 'J') {
            if (scroll_y[active_tab] < max_scroll) {
                scroll_y[active_tab]++;
            }
        } else if (ch == KEY_PPAGE) {
            scroll_y[active_tab] -= visible_lines;
            if (scroll_y[active_tab] < 0) scroll_y[active_tab] = 0;
        } else if (ch == KEY_NPAGE) {
            scroll_y[active_tab] += visible_lines;
            if (scroll_y[active_tab] > max_scroll) scroll_y[active_tab] = max_scroll;
        } else if (ch == KEY_HOME) {
            scroll_y[active_tab] = 0;
        } else if (ch == KEY_END) {
            scroll_y[active_tab] = max_scroll;
        }
    }

    delwin(win);
    touchwin(stdscr);
    ui_draw(ed);
}
