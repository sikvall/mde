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
#include "export.h"
#include "version.h"
#include <locale.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>

void ui_init(void) {
    setlocale(LC_ALL, "");
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    set_escdelay(25);
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
    if (width <= 0) return;

    int gutter_text_end = start_col + gutter_w - 2;
    if (gutter_text_end > start_col + width) gutter_text_end = start_col + width;

    // Gutter header: " COL │ "
    attron(COLOR_PAIR(CP_RULER));
    for (int x = start_col; x < gutter_text_end; ++x) {
        mvaddch(screen_y, x, ' ');
    }
    if (gutter_text_end - (start_col + 1) >= 3) {
        mvaddstr(screen_y, start_col + 1, "COL");
    }
    attroff(COLOR_PAIR(CP_RULER));

    if (start_col + gutter_w <= start_col + width) {
        attron(COLOR_PAIR(CP_DIVIDER));
        mvaddstr(screen_y, start_col + gutter_w - 2, "│ ");
        attroff(COLOR_PAIR(CP_DIVIDER));
    } else {
        return;
    }

    const char *cur_line = buffer_line(&ed->buffer, ed->cursor_row);
    int cursor_vcol = utf8_byte_to_col(cur_line, ed->cursor_col);

    int content_w = width - gutter_w;
    if (content_w <= 0) return;
    int render_x = start_col + gutter_w;

    for (int i = 0; i < content_w; ++i) {
        int vcol = (int)ed->scroll_col + i; // 0-indexed column
        int col_num = vcol + 1;             // 1-indexed column

        if (vcol == cursor_vcol) {
            // Active cursor column indicator on ruler
            attron(COLOR_PAIR(CP_RULER_CURSOR) | A_BOLD);
            mvaddstr(screen_y, render_x + i, "▼");
            attroff(COLOR_PAIR(CP_RULER_CURSOR) | A_BOLD);
        } else if (ed->wrap_col > 0 && col_num == ed->wrap_col) {
            // Active automatic line break guide
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
    if (text_height <= 0) return;

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

    size_t sel_s_row = 0, sel_s_col = 0, sel_e_row = 0, sel_e_col = 0;
    bool has_selection = editor_get_selection(ed, &sel_s_row, &sel_s_col, &sel_e_row, &sel_e_col);

    for (int y = 0; y < text_height; ++y) {
        size_t row_idx = ed->scroll_row + (size_t)y;
        int screen_y = text_start_y + y;

        if (width < gutter_w) {
            for (int x = start_col; x < start_col + width; ++x) {
                mvaddch(screen_y, x, ' ');
            }
            continue;
        }

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
                        bool is_sel = false;
                        if (has_selection) {
                            if (row_idx > sel_s_row && row_idx < sel_e_row) {
                                is_sel = true;
                            } else if (sel_s_row == sel_e_row && row_idx == sel_s_row) {
                                is_sel = (byte_pos >= sel_s_col && byte_pos < sel_e_col);
                            } else if (row_idx == sel_s_row) {
                                is_sel = (byte_pos >= sel_s_col);
                            } else if (row_idx == sel_e_row) {
                                is_sel = (byte_pos < sel_e_col);
                            }
                        }

                        if (is_sel) {
                            attron(A_REVERSE);
                            mvaddnstr(screen_y, render_x, raw + byte_pos, ch_len);
                            attroff(A_REVERSE);
                        } else if (ed->search_active && row_idx == ed->search_match_row &&
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
            bool newline_selected = (has_selection && row_idx >= sel_s_row && row_idx < sel_e_row);
            if (newline_selected && render_x < start_col + width) {
                attron(A_REVERSE);
                mvaddch(screen_y, render_x++, ' ');
                attroff(A_REVERSE);
            }
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

    // Header bar for preview pane (clipped to pane width)
    attron(COLOR_PAIR(CP_RULER));
    for (int x = start_col; x < start_col + width; ++x) {
        mvaddch(start_row, x, ' ');
    }
    const char *hdr = "  PREVIEW";
    int hdr_len = (int)strlen(hdr);
    if (hdr_len > width) hdr_len = width;
    mvaddnstr(start_row, start_col, hdr, hdr_len);
    attroff(COLOR_PAIR(CP_RULER));

    int text_start_y = start_row + 1;
    int text_height = height - 1;
    if (text_height <= 0) return;
    size_t total_rendered = ed->rendered_doc.count;

    for (int y = 0; y < text_height; ++y) {
        size_t rend_idx = ed->preview_scroll_row + (size_t)y;
        int screen_y = text_start_y + y;
        int render_x = start_col;

        // Left margin padding (respecting narrow panes)
        int pad = width >= 2 ? 2 : width;
        for (int p = 0; p < pad; ++p) {
            mvaddch(screen_y, render_x++, ' ');
        }

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
static void draw_status_bar(Editor *ed, int screen_rows, int screen_cols, int y) {
    if (y < 0 || y >= screen_rows || screen_cols <= 0) return;
    attron(COLOR_PAIR(CP_STATUS_BAR));
    for (int x = 0; x < screen_cols; ++x) {
        mvaddch(y, x, ' ');
    }

    // View Mode badge
    const char *mode_str = "[ SPLIT ]";
    if (ed->view_mode == VIEW_INPLACE) mode_str = "[ IN-PLACE ]";
    else if (ed->view_mode == VIEW_PREVIEW) mode_str = "[ PREVIEW ]";

    int mode_len = (int)strlen(mode_str);
    if (screen_cols > 2) {
        int draw_len = mode_len < screen_cols - 1 ? mode_len : screen_cols - 1;
        attron(COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_BOLD);
        mvaddnstr(y, 1, mode_str, draw_len);
        attroff(COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_BOLD);
    }

    // Right-aligned stats
    char right_buf[128];
    const char *cur_line = buffer_line(&ed->buffer, ed->cursor_row);
    int visual_col = utf8_byte_to_col(cur_line, ed->cursor_col) + 1;
    char mode_buf[16];
    if (ed->mark_active) {
        snprintf(mode_buf, sizeof(mode_buf), "%s [SEL]", ed->insert_mode ? "INS" : "OVR");
    } else {
        snprintf(mode_buf, sizeof(mode_buf), "%s", ed->insert_mode ? "INS" : "OVR");
    }
    char wrap_buf[16];
    if (ed->wrap_col > 0) {
        snprintf(wrap_buf, sizeof(wrap_buf), "W:%d", ed->wrap_col);
    } else {
        snprintf(wrap_buf, sizeof(wrap_buf), "W:OFF");
    }
    snprintf(right_buf, sizeof(right_buf), "%s | %s | Ln %zu, Col %d | %zu words | UTF-8",
             mode_buf, wrap_buf,
             ed->cursor_row + 1, visual_col, buffer_word_count(&ed->buffer));

    int right_len = (int)strlen(right_buf);
    int right_x = screen_cols - right_len - 2;
    bool show_right = (right_x > mode_len + 4);
    int left_limit = show_right ? (right_x - 1) : (screen_cols - 1);

    // File name and dirty marker (strictly clipped to left_limit)
    attron(COLOR_PAIR(CP_STATUS_BAR));
    const char *fname = ed->buffer.file_path ? ed->buffer.file_path : "[No Name]";
    int fname_x = mode_len + 3;
    int fname_end_x = fname_x;
    if (fname_x < left_limit) {
        int max_fname = left_limit - fname_x;
        if (ed->buffer.is_dirty && max_fname > 4) max_fname -= 4;
        int fn_len = (int)strlen(fname);
        if (fn_len > max_fname) fn_len = max_fname;
        if (fn_len > 0) {
            mvaddnstr(y, fname_x, fname, fn_len);
            fname_end_x = fname_x + fn_len + 1;
        }
    }

    if (ed->buffer.is_dirty && fname_end_x + 3 <= left_limit) {
        attron(COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD);
        mvaddstr(y, fname_end_x, "[*]");
        attroff(COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD);
        fname_end_x += 4;
    }

    // Ephemeral status notification (if active within 5 seconds)
    if (ed->status_msg[0] && time(NULL) - ed->status_time < 5) {
        int avail_w = left_limit - fname_end_x - 3;
        if (avail_w > 6) {
            attron(COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_BOLD);
            mvprintw(y, fname_end_x + 1, " %.*s ", avail_w - 2, ed->status_msg);
            attroff(COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_BOLD);
        }
    }

    if (show_right) {
        attron(COLOR_PAIR(CP_STATUS_BAR));
        mvaddnstr(y, right_x, right_buf, screen_cols - right_x);
    }
    attroff(COLOR_PAIR(CP_STATUS_BAR));
}

typedef struct {
    const char *key;
    const char *desc;
} ShortcutHint;

static const ShortcutHint s_hints_r1[] = {
    { "^G", "Help" },
    { "^O", "Write Out" },
    { "^W", "Where Is" },
    { "^K", "Cut" },
    { "^T", "Task" },
    { "^C", "Location" }
};

static const ShortcutHint s_hints_r2[] = {
    { "^X", "Exit" },
    { "^R", "Read File" },
    { "^S", "Save" },
    { "^U", "Paste" },
    { "^/", "Go Line" },
    { "F2", "View" }
};

static const ShortcutHint s_hints_r3[] = {
    { "M-U", "Undo" },
    { "M-E", "Redo" },
    { "M-6", "Copy" },
    { "M-A", "Mark" },
    { "M-B", "Bold" },
    { "F5", "Export" }
};

int ui_get_shortcut_hint_rows(int screen_rows) {
    if (screen_rows >= 28) return 3;
    if (screen_rows >= 16) return 2;
    if (screen_rows >= 6) return 1;
    return 0;
}

static void draw_shortcut_hints(int screen_cols, int start_y, int num_rows) {
    if (num_rows <= 0 || screen_cols <= 2) return;
    const ShortcutHint *all_rows[3] = { s_hints_r1, s_hints_r2, s_hints_r3 };

    for (int r = 0; r < num_rows; ++r) {
        int y = start_y + r;
        move(y, 0);
        clrtoeol();

        int num_items = 6;
        int col_w = screen_cols / num_items;
        if (col_w < 10) col_w = 10;

        for (int i = 0; i < num_items; ++i) {
            int x = i * col_w;
            if (x + 2 >= screen_cols) break;

            const ShortcutHint *h = &all_rows[r][i];
            int kw = (int)strlen(h->key);
            if (x + 1 + kw >= screen_cols) break;

            // Render key badge (inverted bold)
            attron(COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_REVERSE | A_BOLD);
            mvaddnstr(y, x + 1, h->key, screen_cols - (x + 1));
            attroff(COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_REVERSE | A_BOLD);

            // Render description (strictly clipped so bottom row never wraps)
            int desc_x = x + 1 + kw + 1;
            int max_desc = (x + col_w < screen_cols ? x + col_w : screen_cols - 1) - desc_x;
            if (max_desc > 0) {
                attron(COLOR_PAIR(CP_DEFAULT));
                mvaddnstr(y, desc_x, h->desc, max_desc);
                attroff(COLOR_PAIR(CP_DEFAULT));
            }
        }
    }
}

static void update_cursor_shape(bool insert_mode) {
    static int s_last_cursor_mode = -1;
    if (s_last_cursor_mode != (int)insert_mode) {
        s_last_cursor_mode = (int)insert_mode;
        if (insert_mode) {
            fputs("\033[2 q", stdout); // DECSCUSR 2: steady block cursor
        } else {
            fputs("\033[4 q", stdout); // DECSCUSR 4: steady underline cursor
        }
        fflush(stdout);
    }
}

void ui_draw(Editor *ed) {
    if (!ed) return;

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    bool resized = (rows != ed->screen_rows || cols != ed->screen_cols);
    ed->screen_rows = rows;
    ed->screen_cols = cols;

    if (resized) {
        ed->rendered_dirty = true;
        erase();
    }

    // Graceful fallback for ultra-tiny terminals
    if (rows < 3 || cols < 10) {
        erase();
        curs_set(0);
        const char *msg = "Resize terminal";
        int mlen = (int)strlen(msg);
        if (mlen > cols) mlen = cols;
        mvaddnstr(rows / 2, (cols - mlen) / 2, msg, mlen);
        refresh();
        return;
    }

    int num_hint_rows = ui_get_shortcut_hint_rows(rows);
    int status_bar_y = rows - num_hint_rows - 1;
    int body_height = status_bar_y;
    if (body_height < 1) body_height = 1;

    // Compute pane widths BEFORE updating scroll and re-rendering preview
    int new_editor_width = 0;
    int new_preview_width = 0;
    int split_col = cols / 2;

    switch (ed->view_mode) {
        case VIEW_SPLIT:
            new_editor_width = split_col;
            new_preview_width = cols - split_col - 1;
            if (new_preview_width < 0) new_preview_width = 0;
            break;
        case VIEW_INPLACE:
            new_editor_width = cols;
            new_preview_width = 0;
            break;
        case VIEW_PREVIEW:
            new_editor_width = 0;
            new_preview_width = cols;
            break;
    }

    if (new_preview_width != ed->preview_width) {
        ed->rendered_dirty = true;
    }
    ed->editor_width = new_editor_width;
    ed->preview_width = new_preview_width;

    // Refresh preview and scroll offsets using the new geometry
    editor_refresh_rendered(ed);
    int view_rows = body_height > 1 ? (body_height - 1) : 1;
    editor_update_scroll(ed, view_rows);

    switch (ed->view_mode) {
        case VIEW_SPLIT: {
            draw_editor_pane(ed, 0, split_col, 0, body_height);

            // Center vertical divider
            if (split_col < cols) {
                attron(COLOR_PAIR(CP_DIVIDER));
                for (int y = 0; y < body_height; ++y) {
                    mvaddstr(y, split_col, "│");
                }
                attroff(COLOR_PAIR(CP_DIVIDER));
            }

            if (new_preview_width > 0) {
                draw_preview_pane(ed, split_col + 1, new_preview_width, 0, body_height);
            }
            break;
        }
        case VIEW_INPLACE:
            draw_editor_pane(ed, 0, cols, 0, body_height);
            break;
        case VIEW_PREVIEW:
            draw_preview_pane(ed, 0, cols, 0, body_height);
            break;
    }

    draw_status_bar(ed, rows, cols, status_bar_y);
    draw_shortcut_hints(cols, status_bar_y + 1, num_hint_rows);

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

    curs_set(1);

    while (1) {
        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        int y = rows > 0 ? rows - 1 : 0;

        move(y, 0);
        clrtoeol();

        int prompt_len = (int)strlen(prompt);
        int avail_prompt = cols > 4 ? cols - 4 : 0;
        if (prompt_len > avail_prompt) prompt_len = avail_prompt;

        attron(COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_BOLD);
        mvprintw(y, 1, "%.*s: ", prompt_len, prompt);
        attroff(COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_BOLD);

        int input_x = prompt_len + 3;
        int max_vis = cols - input_x - 1;
        if (max_vis > 0) {
            const char *disp = out_buf;
            if ((int)len > max_vis) {
                disp = out_buf + (len - (size_t)max_vis);
            }
            attron(COLOR_PAIR(CP_DEFAULT));
            mvaddnstr(y, input_x, disp, max_vis);
            attroff(COLOR_PAIR(CP_DEFAULT));
        }

        refresh();

        int ch = getch();
        if (ch == ERR) continue;
        if (ch == KEY_RESIZE) {
            ed->rendered_dirty = true;
            erase();
            ui_draw(ed);
            curs_set(1);
            continue;
        }
        if (ch == 27 || ch == CTRL_KEY('c')) { // Escape or Nano ^C Cancel
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

int ui_confirm_unsaved(Editor *ed, const char *action_desc) {
    if (!ed || !ed->buffer.is_dirty) return 0;

    char msg[160];
    snprintf(msg, sizeof(msg),
             "Save modified buffer before %s? (y = Save, n = Discard, ^C/Esc = Cancel): ",
             action_desc ? action_desc : "continuing");

    while (1) {
        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        int y = rows > 0 ? rows - 1 : 0;

        move(y, 0);
        clrtoeol();

        attron(COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD);
        if (cols > 2) {
            mvaddnstr(y, 1, msg, cols - 2);
        }
        attroff(COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD);
        refresh();

        int ch = getch();
        if (ch == ERR) continue;
        if (ch == KEY_RESIZE) {
            ed->rendered_dirty = true;
            erase();
            ui_draw(ed);
            continue;
        }
        if (ch == 'y' || ch == 'Y') {
            return 1;
        } else if (ch == 'n' || ch == 'N') {
            return 0;
        } else if (ch == 27 || ch == 3) {
            return -1;
        }
    }
}

int ui_confirm_quit(Editor *ed) {
    return ui_confirm_unsaved(ed, "exit");
}

bool ui_path_exists(const char *path, bool *out_is_dir) {
    if (out_is_dir) *out_is_dir = false;
    if (!path || !path[0]) return false;
    struct stat st;
    if (stat(path, &st) == 0) {
        if (out_is_dir) *out_is_dir = S_ISDIR(st.st_mode);
        return true;
    }
    return false;
}

bool ui_confirm_overwrite(Editor *ed, const char *path) {
    if (!ed || !path || !path[0]) return false;

    const char *slash = strrchr(path, '/');
    const char *disp = slash ? (slash + 1) : path;

    char msg[256];
    snprintf(msg, sizeof(msg),
             "WARNING: File '%s' already exists! Overwrite? (y = Yes, n/^C/Esc = No): ",
             disp);

    while (1) {
        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        int y = rows > 0 ? rows - 1 : 0;

        move(y, 0);
        clrtoeol();

        attron(COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD);
        if (cols > 2) {
            mvaddnstr(y, 1, msg, cols - 2);
        }
        attroff(COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD);
        refresh();

        int ch = getch();
        if (ch == ERR) continue;
        if (ch == KEY_RESIZE) {
            ed->rendered_dirty = true;
            erase();
            ui_draw(ed);
            continue;
        }
        if (ch == 'y' || ch == 'Y') {
            return true;
        } else if (ch == 'n' || ch == 'N' || ch == 27 || ch == CTRL_KEY('c') || ch == CTRL_KEY('g')) {
            return false;
        }
    }
}

typedef struct {
    char name[256];
    bool is_dir;
    size_t size;
} FileItem;

static int compare_file_items(const void *a, const void *b) {
    const FileItem *fa = (const FileItem *)a;
    const FileItem *fb = (const FileItem *)b;

    // ".." is always first
    if (strcmp(fa->name, "..") == 0) return -1;
    if (strcmp(fb->name, "..") == 0) return 1;

    // Directories come before regular files
    if (fa->is_dir != fb->is_dir) {
        return fa->is_dir ? -1 : 1;
    }

    // Both are directories or both are files: case-insensitive alphabetical sort
    return strcasecmp(fa->name, fb->name);
}

static FileItem *load_directory_items(const char *dir_path, size_t *out_count) {
    *out_count = 0;
    DIR *d = opendir(dir_path);
    if (!d) return NULL;

    size_t cap = 64;
    FileItem *items = (FileItem *)malloc(cap * sizeof(FileItem));
    if (!items) {
        closedir(d);
        return NULL;
    }

    // Always add ".." if not root "/"
    if (strcmp(dir_path, "/") != 0) {
        strncpy(items[0].name, "..", sizeof(items[0].name) - 1);
        items[0].name[sizeof(items[0].name) - 1] = '\0';
        items[0].is_dir = true;
        items[0].size = 0;
        *out_count = 1;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        // Build full path for stat
        char full_path[PATH_MAX];
        if (strcmp(dir_path, "/") == 0) {
            snprintf(full_path, sizeof(full_path), "/%s", ent->d_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name);
        }

        struct stat st;
        bool is_dir = false;
        size_t size = 0;
        if (stat(full_path, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            size = (size_t)st.st_size;
        } else {
#ifdef DT_DIR
            if (ent->d_type == DT_DIR) is_dir = true;
#endif
        }

        if (*out_count >= cap) {
            cap *= 2;
            FileItem *new_items = (FileItem *)realloc(items, cap * sizeof(FileItem));
            if (!new_items) break;
            items = new_items;
        }

        strncpy(items[*out_count].name, ent->d_name, sizeof(items[*out_count].name) - 1);
        items[*out_count].name[sizeof(items[*out_count].name) - 1] = '\0';
        items[*out_count].is_dir = is_dir;
        items[*out_count].size = size;
        (*out_count)++;
    }

    closedir(d);

    if (*out_count > 1) {
        qsort(items, *out_count, sizeof(FileItem), compare_file_items);
    }

    return items;
}

static void navigate_to_parent(char *dir_buf, size_t buf_size) {
    (void)buf_size;
    if (strcmp(dir_buf, "/") == 0) return;

    size_t len = strlen(dir_buf);
    while (len > 1 && dir_buf[len - 1] == '/') {
        dir_buf[--len] = '\0';
    }

    char *last_slash = strrchr(dir_buf, '/');
    if (last_slash == dir_buf) {
        dir_buf[1] = '\0';
    } else if (last_slash) {
        *last_slash = '\0';
    } else {
        strncpy(dir_buf, "/", buf_size);
    }
}

static void navigate_into_sub(char *dir_buf, size_t buf_size, const char *sub) {
    if (strcmp(sub, "..") == 0) {
        navigate_to_parent(dir_buf, buf_size);
        return;
    }
    char temp[PATH_MAX];
    if (strcmp(dir_buf, "/") == 0) {
        snprintf(temp, sizeof(temp), "/%s", sub);
    } else {
        snprintf(temp, sizeof(temp), "%s/%s", dir_buf, sub);
    }
    char resolved[PATH_MAX];
    if (realpath(temp, resolved)) {
        snprintf(dir_buf, buf_size, "%s", resolved);
    } else {
        snprintf(dir_buf, buf_size, "%s", temp);
    }
}

static void format_file_size(size_t size, char *buf, size_t buf_len) {
    if (size < 1024) {
        snprintf(buf, buf_len, "%zu B", size);
    } else if (size < 1024 * 1024) {
        snprintf(buf, buf_len, "%.1f KB", (double)size / 1024.0);
    } else {
        snprintf(buf, buf_len, "%.1f MB", (double)size / (1024.0 * 1024.0));
    }
}

bool ui_file_dialog(Editor *ed, FileDialogMode mode, char *out_path, size_t max_len) {
    if (!ed || !out_path || max_len == 0) return false;

    char current_dir[PATH_MAX];
    if (ed->buffer.file_path && ed->buffer.file_path[0]) {
        char dir_copy[PATH_MAX];
        strncpy(dir_copy, ed->buffer.file_path, sizeof(dir_copy) - 1);
        dir_copy[sizeof(dir_copy) - 1] = '\0';
        char *d = dirname(dir_copy);
        if (!realpath(d, current_dir)) {
            if (!getcwd(current_dir, sizeof(current_dir))) {
                strncpy(current_dir, ".", sizeof(current_dir));
            }
        }
    } else {
        if (!getcwd(current_dir, sizeof(current_dir))) {
            strncpy(current_dir, ".", sizeof(current_dir));
        }
    }

    char input_buf[256] = {0};
    if (mode == FILE_DIALOG_SAVE_AS) {
        if (ed->buffer.file_path && ed->buffer.file_path[0]) {
            char file_copy[PATH_MAX];
            strncpy(file_copy, ed->buffer.file_path, sizeof(file_copy) - 1);
            file_copy[sizeof(file_copy) - 1] = '\0';
            char *b = basename(file_copy);
            strncpy(input_buf, b, sizeof(input_buf) - 1);
        } else {
            strncpy(input_buf, "untitled.md", sizeof(input_buf) - 1);
        }
    }

    size_t count = 0;
    FileItem *items = load_directory_items(current_dir, &count);
    int selected_idx = 0;
    int scroll_offset = 0;

    bool has_input_box = (mode == FILE_DIALOG_SAVE_AS || mode == FILE_DIALOG_LINK);
    enum { FOCUS_LIST, FOCUS_INPUT } focus = (mode == FILE_DIALOG_SAVE_AS) ? FOCUS_INPUT : FOCUS_LIST;

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int win_w = 72;
    if (win_w > cols - 4) win_w = cols - 4;
    if (win_w < 44) win_w = 44;
    if (win_w > cols) win_w = cols;
    if (win_w < 10) win_w = cols > 0 ? cols : 10;

    int win_h = 22;
    if (win_h > rows - 2) win_h = rows - 2;
    if (win_h < 12) win_h = 12;
    if (win_h > rows) win_h = rows;
    if (win_h < 6) win_h = rows > 0 ? rows : 6;

    int win_y = (rows - win_h) / 2;
    int win_x = (cols - win_w) / 2;
    if (win_y < 0) win_y = 0;
    if (win_x < 0) win_x = 0;

    WINDOW *win = newwin(win_h, win_w, win_y, win_x);
    if (!win) {
        free(items);
        return false;
    }
    keypad(win, TRUE);
    wbkgd(win, COLOR_PAIR(CP_HELP_BOX));

    bool confirmed = false;
    bool confirm_overwrite = false;
    char pending_path[PATH_MAX] = {0};
    char warn_msg[256] = {0};

    while (1) {
        werase(win);
        box(win, 0, 0);

        // Title
        wattron(win, COLOR_PAIR(CP_HEADER1) | A_BOLD);
        const char *title = " Open File ";
        if (mode == FILE_DIALOG_SAVE_AS) title = " Write Out (Save As) ";
        else if (mode == FILE_DIALOG_LINK) title = " Insert File Link ";
        int tx = (win_w - (int)strlen(title)) / 2;
        if (tx < 1) tx = 1;
        mvwaddnstr(win, 0, tx, title, win_w - 2);
        wattroff(win, COLOR_PAIR(CP_HEADER1) | A_BOLD);

        // Current Directory display (truncated on left if too long)
        wattron(win, COLOR_PAIR(CP_DEFAULT) | A_BOLD);
        mvwaddstr(win, 1, 2, "Dir: ");
        wattroff(win, COLOR_PAIR(CP_DEFAULT) | A_BOLD);

        int max_dir_disp = win_w - 9;
        if (max_dir_disp > 4) {
            size_t dlen = strlen(current_dir);
            if ((int)dlen > max_dir_disp) {
                char short_dir[256];
                snprintf(short_dir, sizeof(short_dir), "...%s", current_dir + dlen - (max_dir_disp - 3));
                wattron(win, COLOR_PAIR(CP_LINK));
                mvwaddnstr(win, 1, 7, short_dir, max_dir_disp);
                wattroff(win, COLOR_PAIR(CP_LINK));
            } else {
                wattron(win, COLOR_PAIR(CP_LINK));
                mvwaddnstr(win, 1, 7, current_dir, max_dir_disp);
                wattroff(win, COLOR_PAIR(CP_LINK));
            }
        }

        // Horizontal separator line under directory
        mvwaddch(win, 2, 0, ACS_LTEE);
        for (int x = 1; x < win_w - 1; ++x) mvwaddch(win, 2, x, ACS_HLINE);
        mvwaddch(win, 2, win_w - 1, ACS_RTEE);

        // List dimensions
        int list_start_y = 3;
        int list_h = has_input_box ? (win_h - 7) : (win_h - 5);
        if (list_h < 3) list_h = 3;

        // Ensure selected_idx and scroll_offset are valid
        if (selected_idx < 0) selected_idx = 0;
        if (count > 0 && selected_idx >= (int)count) selected_idx = (int)count - 1;
        if (selected_idx < scroll_offset) scroll_offset = selected_idx;
        if (selected_idx >= scroll_offset + list_h) scroll_offset = selected_idx - list_h + 1;

        // Render file list items
        for (int i = 0; i < list_h; ++i) {
            int idx = scroll_offset + i;
            int y = list_start_y + i;

            if (idx < (int)count) {
                bool is_sel = (idx == selected_idx && focus == FOCUS_LIST);
                FileItem *fi = &items[idx];

                if (is_sel) {
                    wattron(win, COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_REVERSE | A_BOLD);
                    for (int x = 1; x < win_w - 1; ++x) mvwaddch(win, y, x, ' ');
                    mvwaddstr(win, y, 2, "▶ ");
                } else {
                    mvwaddstr(win, y, 2, "  ");
                }

                if (fi->is_dir) {
                    wattron(win, COLOR_PAIR(CP_HEADER2) | A_BOLD);
                    char dname[260];
                    if (strcmp(fi->name, "..") == 0) {
                        snprintf(dname, sizeof(dname), "[..] (Parent Directory)");
                    } else {
                        snprintf(dname, sizeof(dname), "%s/", fi->name);
                    }
                    mvwaddnstr(win, y, 4, dname, win_w - 18);
                    wattroff(win, COLOR_PAIR(CP_HEADER2) | A_BOLD);

                    // Right column: <DIR>
                    mvwaddstr(win, y, win_w - 8, "<DIR>");
                } else {
                    wattron(win, COLOR_PAIR(CP_DEFAULT));
                    mvwaddnstr(win, y, 4, fi->name, win_w - 18);
                    wattroff(win, COLOR_PAIR(CP_DEFAULT));

                    // Right column: File size
                    char sz_buf[32];
                    format_file_size(fi->size, sz_buf, sizeof(sz_buf));
                    int sz_len = (int)strlen(sz_buf);
                    mvwaddstr(win, y, win_w - sz_len - 3, sz_buf);
                }

                if (is_sel) {
                    wattroff(win, COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_REVERSE | A_BOLD);
                }
            } else {
                for (int x = 1; x < win_w - 1; ++x) mvwaddch(win, y, x, ' ');
            }
        }

        // Scrollbar indicator on right border
        if (count > (size_t)list_h) {
            int thumb_pos = (selected_idx * (list_h - 1)) / ((int)count - 1);
            wattron(win, COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_BOLD);
            mvwaddch(win, list_start_y + thumb_pos, win_w - 1, ACS_BLOCK);
            wattroff(win, COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_BOLD);
        }

        // Save As / Link input row
        if (has_input_box) {
            int input_sep_y = win_h - 4;
            int input_y = win_h - 3;

            mvwaddch(win, input_sep_y, 0, ACS_LTEE);
            for (int x = 1; x < win_w - 1; ++x) mvwaddch(win, input_sep_y, x, ACS_HLINE);
            mvwaddch(win, input_sep_y, win_w - 1, ACS_RTEE);

            bool is_input_focused = (focus == FOCUS_INPUT);
            wattron(win, is_input_focused ? (COLOR_PAIR(CP_DEFAULT) | A_BOLD) : COLOR_PAIR(CP_MARKER));
            mvwaddstr(win, input_y, 2, mode == FILE_DIALOG_LINK ? "Target:    " : "File name: ");
            wattroff(win, is_input_focused ? (COLOR_PAIR(CP_DEFAULT) | A_BOLD) : COLOR_PAIR(CP_MARKER));

            // Input field box
            int box_start_x = 13;
            int box_width = win_w - 16;
            wattron(win, COLOR_PAIR(CP_DEFAULT) | (is_input_focused ? A_REVERSE : 0));
            for (int x = 0; x < box_width; ++x) mvwaddch(win, input_y, box_start_x + x, ' ');
            mvwaddnstr(win, input_y, box_start_x, input_buf, box_width - 1);
            wattroff(win, COLOR_PAIR(CP_DEFAULT) | (is_input_focused ? A_REVERSE : 0));

            if (is_input_focused && !confirm_overwrite) {
                curs_set(1);
                wmove(win, input_y, box_start_x + (int)strlen(input_buf));
            } else {
                curs_set(0);
            }
        } else {
            curs_set(0);
        }

        // Bottom separator & help instructions / overwrite warning
        mvwaddch(win, win_h - 2, 0, ACS_LTEE);
        for (int x = 1; x < win_w - 1; ++x) mvwaddch(win, win_h - 2, x, ACS_HLINE);
        mvwaddch(win, win_h - 2, win_w - 1, ACS_RTEE);

        if (win_w > 4) {
            if (confirm_overwrite) {
                const char *slash = strrchr(pending_path, '/');
                const char *fname = slash ? (slash + 1) : pending_path;
                char prompt_buf[300];
                snprintf(prompt_buf, sizeof(prompt_buf),
                         " WARNING: '%s' exists! Overwrite? (Y)es / (N)o ", fname);
                wattron(win, COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD | A_REVERSE);
                for (int x = 1; x < win_w - 1; ++x) mvwaddch(win, win_h - 1, x, ' ');
                mvwaddnstr(win, win_h - 1, 2, prompt_buf, win_w - 4);
                wattroff(win, COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD | A_REVERSE);
            } else if (warn_msg[0]) {
                wattron(win, COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD);
                mvwaddnstr(win, win_h - 1, 2, warn_msg, win_w - 4);
                wattroff(win, COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD);
            } else {
                wattron(win, COLOR_PAIR(CP_MARKER) | A_DIM);
                if (mode == FILE_DIALOG_OPEN) {
                    mvwaddnstr(win, win_h - 1, 2, " [Enter] Open  [Backspace] Up  [Esc/^C] Cancel ", win_w - 4);
                } else if (mode == FILE_DIALOG_LINK) {
                    mvwaddnstr(win, win_h - 1, 2, " [Enter] Link  [Tab] Type Target  [Backspace] Up  [Esc/^C] Cancel ", win_w - 4);
                } else {
                    mvwaddnstr(win, win_h - 1, 2, " [Enter] Save  [Tab] Switch  [Backspace] Up  [Esc/^C] Cancel ", win_w - 4);
                }
                wattroff(win, COLOR_PAIR(CP_MARKER) | A_DIM);
            }
        }

        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;

        if (ch == KEY_RESIZE) {
            delwin(win);
            ed->rendered_dirty = true;
            erase();
            ui_draw(ed);

            getmaxyx(stdscr, rows, cols);
            win_w = 72;
            if (win_w > cols - 4) win_w = cols - 4;
            if (win_w < 44) win_w = 44;
            if (win_w > cols) win_w = cols;
            if (win_w < 10) win_w = cols > 0 ? cols : 10;

            win_h = 22;
            if (win_h > rows - 2) win_h = rows - 2;
            if (win_h < 12) win_h = 12;
            if (win_h > rows) win_h = rows;
            if (win_h < 6) win_h = rows > 0 ? rows : 6;

            win_y = (rows - win_h) / 2;
            win_x = (cols - win_w) / 2;
            if (win_y < 0) win_y = 0;
            if (win_x < 0) win_x = 0;

            win = newwin(win_h, win_w, win_y, win_x);
            if (!win) {
                free(items);
                return false;
            }
            keypad(win, TRUE);
            wbkgd(win, COLOR_PAIR(CP_HELP_BOX));
            continue;
        }

        // Overwrite warning requires an explicit Y or N answer before overwriting
        if (confirm_overwrite) {
            if (ch == 'y' || ch == 'Y') {
                snprintf(out_path, max_len, "%s", pending_path);
                confirmed = true;
                break;
            } else if (ch == 'n' || ch == 'N' || ch == 27 ||
                       ch == CTRL_KEY('c') || ch == CTRL_KEY('g') ||
                       ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                confirm_overwrite = false;
                pending_path[0] = '\0';
            }
            continue;
        }

        warn_msg[0] = '\0';

        if (ch == 27 || ch == CTRL_KEY('q') || ch == CTRL_KEY('c') || ch == CTRL_KEY('g')) {
            confirmed = false;
            break;
        }

        if (ch == '\t') {
            if (has_input_box) {
                focus = (focus == FOCUS_INPUT) ? FOCUS_LIST : FOCUS_INPUT;
            }
            continue;
        }

        if (focus == FOCUS_INPUT) {
            if (ch == '\n' || ch == '\r') {
                if (input_buf[0] != '\0') {
                    char candidate[PATH_MAX];
                    if (input_buf[0] == '/') {
                        snprintf(candidate, sizeof(candidate), "%s", input_buf);
                    } else if (strcmp(current_dir, "/") == 0) {
                        snprintf(candidate, sizeof(candidate), "/%s", input_buf);
                    } else {
                        snprintf(candidate, sizeof(candidate), "%s/%s", current_dir, input_buf);
                    }

                    if (mode == FILE_DIALOG_SAVE_AS) {
                        bool is_dir = false;
                        if (ui_path_exists(candidate, &is_dir)) {
                            if (is_dir) {
                                snprintf(warn_msg, sizeof(warn_msg),
                                         " WARNING: Cannot overwrite directory — enter a file name ");
                                continue;
                            }
                            snprintf(pending_path, sizeof(pending_path), "%s", candidate);
                            confirm_overwrite = true;
                            continue;
                        }
                    }

                    snprintf(out_path, max_len, "%s", candidate);
                    confirmed = true;
                    break;
                }
            } else if (ch == KEY_UP) {
                focus = FOCUS_LIST;
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                size_t ilen = strlen(input_buf);
                if (ilen > 0) {
                    size_t prev = utf8_prev_char(input_buf, ilen);
                    input_buf[prev] = '\0';
                }
            } else if (ch >= 32 && ch <= 126) {
                size_t ilen = strlen(input_buf);
                if (ilen + 1 < sizeof(input_buf)) {
                    input_buf[ilen] = (char)ch;
                    input_buf[ilen + 1] = '\0';
                }
            }
        } else {
            // FOCUS_LIST
            if (ch == KEY_UP || ch == 'k') {
                if (selected_idx > 0) selected_idx--;
            } else if (ch == KEY_DOWN || ch == 'j') {
                if (selected_idx + 1 < (int)count) selected_idx++;
            } else if (ch == KEY_PPAGE) {
                selected_idx -= list_h;
                if (selected_idx < 0) selected_idx = 0;
            } else if (ch == KEY_NPAGE) {
                selected_idx += list_h;
                if (selected_idx >= (int)count) selected_idx = (int)count - 1;
            } else if (ch == KEY_HOME) {
                selected_idx = 0;
            } else if (ch == KEY_END) {
                selected_idx = (int)count - 1;
            } else if (ch == KEY_LEFT || ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                free(items);
                navigate_to_parent(current_dir, sizeof(current_dir));
                items = load_directory_items(current_dir, &count);
                selected_idx = 0;
                scroll_offset = 0;
            } else if (ch == '\n' || ch == '\r') {
                if (count > 0 && selected_idx < (int)count) {
                    if (items[selected_idx].is_dir) {
                        char sub[256];
                        strncpy(sub, items[selected_idx].name, sizeof(sub) - 1);
                        sub[sizeof(sub) - 1] = '\0';
                        free(items);
                        navigate_into_sub(current_dir, sizeof(current_dir), sub);
                        items = load_directory_items(current_dir, &count);
                        selected_idx = 0;
                        scroll_offset = 0;
                    } else {
                        // File selected
                        if (mode == FILE_DIALOG_OPEN || mode == FILE_DIALOG_LINK) {
                            snprintf(out_path, max_len, "%s/%s", current_dir, items[selected_idx].name);
                            out_path[max_len - 1] = '\0';
                            confirmed = true;
                            break;
                        } else {
                            // In Save As mode, selecting a file updates input_buf and focuses input
                            strncpy(input_buf, items[selected_idx].name, sizeof(input_buf) - 1);
                            input_buf[sizeof(input_buf) - 1] = '\0';
                            focus = FOCUS_INPUT;
                        }
                    }
                }
            } else if (ch >= 32 && ch <= 126) {
                // Quick search / jump to next entry starting with that char
                char target = (char)tolower(ch);
                for (size_t i = 1; i <= count; ++i) {
                    size_t candidate = (selected_idx + i) % count;
                    if (tolower((unsigned char)items[candidate].name[0]) == target) {
                        selected_idx = (int)candidate;
                        break;
                    }
                }
            }
        }
    }

    free(items);
    delwin(win);
    curs_set(1);
    touchwin(stdscr);
    ui_draw(ed);
    return confirmed;
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
    { HELP_LINE_SECTION, "FILE & SESSION (NANO-COMPATIBLE)", NULL },
    { HELP_LINE_ITEM, "Ctrl+S", "Save current document to disk" },
    { HELP_LINE_ITEM, "Ctrl+O", "Write Out (Save As file dialogue)" },
    { HELP_LINE_ITEM, "Ctrl+R", "Read / Open file dialogue to browse & open" },
    { HELP_LINE_ITEM, "F5 / Alt+X", "Open Export Menu (HTML, XML, EPUB, LaTeX)" },
    { HELP_LINE_ITEM, "Ctrl+X", "Exit editor (prompts if unsaved changes)" },
    { HELP_LINE_ITEM, "Ctrl+G / F1", "Open / close this Help & Reference overlay" },
    { HELP_LINE_BLANK, NULL, NULL },
    { HELP_LINE_SECTION, "HYPERTEXT FILE LINKING & EXPORT", NULL },
    { HELP_LINE_ITEM, "F6 / Alt+H", "Insert hyperlink [Label](file.md) via file picker" },
    { HELP_LINE_ITEM, "F7 / Alt+O / ^]", "Follow Markdown file or #heading link under cursor" },
    { HELP_LINE_ITEM, "F8 / Alt+-", "Navigate back to previous linked file & position" },
    { HELP_LINE_ITEM, "F5 / Alt+X", "Export (includes linked .md files in HTML/EPUB/LaTeX/XML)" },
    { HELP_LINE_BLANK, NULL, NULL },
    { HELP_LINE_SECTION, "VIEW MODES & DISPLAY", NULL },
    { HELP_LINE_ITEM, "F2 / Alt+P", "Cycle view: Split -> In-Place -> Preview" },
    { HELP_LINE_ITEM, "F4 / Alt+L", "Set auto line break column (20-255 or off)" },
    { HELP_LINE_ITEM, "Ctrl+L", "Refresh / redraw screen" },
    { HELP_LINE_ITEM, "Ins", "Toggle mode: Insert (steady █) / Overwrite (steady _)" },
    { HELP_LINE_BLANK, NULL, NULL },
    { HELP_LINE_SECTION, "SELECTION, CUT, COPY & PASTE", NULL },
    { HELP_LINE_ITEM, "Alt+A / ^6 / ^Space", "Set or unset selection mark anchor" },
    { HELP_LINE_ITEM, "Ctrl+K", "Cut selection, or cut entire line (OSC 52)" },
    { HELP_LINE_ITEM, "Alt+6", "Copy selection, or copy entire line (OSC 52)" },
    { HELP_LINE_ITEM, "Ctrl+U", "Paste (Uncut) text from clipboard" },
    { HELP_LINE_ITEM, "Esc", "Clear active selection mark (Cancel in prompts)" },
    { HELP_LINE_BLANK, NULL, NULL },
    { HELP_LINE_SECTION, "EDITING & MARKDOWN", NULL },
    { HELP_LINE_ITEM, "Alt+U / Alt+E", "Undo / Redo last editing operation" },
    { HELP_LINE_ITEM, "Ctrl+H / Bksp", "Delete character before cursor" },
    { HELP_LINE_ITEM, "Ctrl+D / Del", "Delete character under cursor" },
    { HELP_LINE_ITEM, "Ctrl+T", "Toggle task checkbox: - [ ] <-> - [x]" },
    { HELP_LINE_ITEM, "Alt+B", "Insert Markdown bold markers / wrap selection" },
    { HELP_LINE_ITEM, "Enter", "Smart Enter: auto-continues lists, tasks & indents" },
    { HELP_LINE_ITEM, "Tab / Alt+}", "Indent current line by 2 spaces" },
    { HELP_LINE_ITEM, "Shift+Tab / Alt+{", "Unindent current line by 2 spaces" },
    { HELP_LINE_BLANK, NULL, NULL },
    { HELP_LINE_SECTION, "SEARCH & NAVIGATION", NULL },
    { HELP_LINE_ITEM, "Ctrl+W / Ctrl+Q", "Where Is (search forward) / Search backward" },
    { HELP_LINE_ITEM, "Alt+W / F3", "Repeat search forward (Alt+Q for backward)" },
    { HELP_LINE_ITEM, "Ctrl+/ / Alt+G", "Go to line number (and optional column)" },
    { HELP_LINE_ITEM, "Ctrl+C", "Display cursor location (Cancel in prompts)" },
    { HELP_LINE_ITEM, "Ctrl+A / Home", "Jump to beginning of current line" },
    { HELP_LINE_ITEM, "Ctrl+E / End", "Jump to end of current line" },
    { HELP_LINE_ITEM, "Ctrl+Y / PgUp", "Previous Page (scroll up)" },
    { HELP_LINE_ITEM, "Ctrl+V / PgDn", "Next Page (scroll down)" },
    { HELP_LINE_ITEM, "Alt+\\ / Alt+/", "Jump to first / last line of document" },
    { HELP_LINE_ITEM, "^B / ^F / ^P / ^N", "Move Left / Right / Up / Down (or Arrows)" }
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
    { HELP_LINE_NOTE, "Live Preview", "Press F2 or Alt+P to toggle live rendered preview" },
    { HELP_LINE_NOTE, "Smart Lists", "Enter on empty list item automatically exits list" },
    { HELP_LINE_NOTE, "Hypertext Links", "Link local .md files (F6/Alt+H) & follow (F7/Alt+O)" },
    { HELP_LINE_NOTE, "Multi-File Export", "Export (F5) bundles linked .md files (1 .tex for LaTeX)" }
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
    if (box_h > rows) box_h = rows;
    if (box_w > cols) box_w = cols;
    if (box_h < 6) box_h = rows > 0 ? rows : 6;
    if (box_w < 16) box_w = cols > 0 ? cols : 16;

    int start_y = (rows - box_h) / 2;
    int start_x = (cols - box_w) / 2;
    if (start_y < 0) start_y = 0;
    if (start_x < 0) start_x = 0;

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

        if (ch == KEY_RESIZE) {
            delwin(win);
            ed->rendered_dirty = true;
            erase();
            ui_draw(ed);

            getmaxyx(stdscr, rows, cols);
            box_h = 26;
            box_w = 76;
            if (rows >= 32) box_h = 28;
            else if (box_h > rows - 2) box_h = rows - 2;
            if (box_w > cols - 2) box_w = cols - 2;
            if (box_h < 12) box_h = 12;
            if (box_w < 50) box_w = 50;
            if (box_h > rows) box_h = rows;
            if (box_w > cols) box_w = cols;
            if (box_h < 6) box_h = rows > 0 ? rows : 6;
            if (box_w < 16) box_w = cols > 0 ? cols : 16;

            start_y = (rows - box_h) / 2;
            start_x = (cols - box_w) / 2;
            if (start_y < 0) start_y = 0;
            if (start_x < 0) start_x = 0;

            win = newwin(box_h, box_w, start_y, start_x);
            if (!win) {
                touchwin(stdscr);
                ui_draw(ed);
                return;
            }
            keypad(win, TRUE);
            wbkgd(win, COLOR_PAIR(CP_HELP_BOX));
            curs_set(0);
            continue;
        }

        if (ch == 27 || ch == 'q' || ch == 'Q' || ch == KEY_F(1) || ch == CTRL_KEY('g') || ch == CTRL_KEY('x')) {
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

static void calc_export_win_geom(int rows, int cols, int *win_h, int *win_w, int *win_y, int *win_x) {
    int h = 16;
    int w = 72;
    if (w > cols - 4) w = cols - 4;
    if (w < 46) w = 46;
    if (w > cols) w = cols;
    if (w < 16) w = cols > 0 ? cols : 16;

    if (h > rows - 2) h = rows - 2;
    if (h < 14) h = 14;
    if (h > rows) h = rows;
    if (h < 8) h = rows > 0 ? rows : 8;

    int y = (rows - h) / 2;
    int x = (cols - w) / 2;
    if (y < 0) y = 0;
    if (x < 0) x = 0;

    *win_h = h;
    *win_w = w;
    *win_y = y;
    *win_x = x;
}

bool ui_export_menu(Editor *ed) {
    if (!ed) return false;

    bool has_pandoc = export_has_pandoc();
    size_t linked_files = export_count_linked_files(&ed->buffer);
    bool include_linked = true;
    ExportFormat selected_fmt = EXPORT_HTML;
    ExportEngine engine = EXPORT_ENGINE_AUTO;

    char path_buf[PATH_MAX];
    export_default_path(ed->buffer.file_path, selected_fmt, path_buf, sizeof(path_buf));
    char err_msg[256] = {0};

    enum { EXP_FOCUS_LIST, EXP_FOCUS_PATH } focus = EXP_FOCUS_LIST;

    int rows, cols, win_h, win_w, win_y, win_x;
    getmaxyx(stdscr, rows, cols);
    calc_export_win_geom(rows, cols, &win_h, &win_w, &win_y, &win_x);

    WINDOW *win = newwin(win_h, win_w, win_y, win_x);
    if (!win) return false;
    keypad(win, TRUE);
    wbkgd(win, COLOR_PAIR(CP_HELP_BOX));

    static const struct {
        ExportFormat fmt;
        const char *label;
        const char *desc;
    } k_formats[EXPORT_FORMAT_COUNT] = {
        { EXPORT_HTML,  "1. HTML  (.html)", "Web page (HTML5)" },
        { EXPORT_XML,   "2. XML   (.xml) ", "Structured XML / DocBook 5" },
        { EXPORT_EPUB,  "3. EPUB  (.epub)", "E-book (EPUB 3)" },
        { EXPORT_LATEX, "4. LaTeX (.tex) ", "Single-doc LaTeX source" }
    };

    bool exported = false;

    while (1) {
        werase(win);
        box(win, 0, 0);

        // Title on top border
        const char *title = " Export Markdown ";
        int tx = (win_w - (int)strlen(title)) / 2;
        if (tx < 1) tx = 1;
        wattron(win, COLOR_PAIR(CP_HEADER1) | A_BOLD);
        mvwaddnstr(win, 0, tx, title, win_w - 2);
        wattroff(win, COLOR_PAIR(CP_HEADER1) | A_BOLD);

        // Row 1: Engine status bar
        char eng_line[128];
        if (has_pandoc) {
            snprintf(eng_line, sizeof(eng_line), "Engine: %s  (pandoc detected)",
                     engine == EXPORT_ENGINE_BUILTIN ? "Built-in C11" : "Auto (Pandoc)");
            wattron(win, COLOR_PAIR(CP_TASK_CHECKED) | A_BOLD);
        } else {
            snprintf(eng_line, sizeof(eng_line), "Engine: Built-in  (pandoc not found; EPUB needs pandoc)");
            wattron(win, COLOR_PAIR(CP_TASK_UNCHECKED));
        }
        if (win_w > 4) {
            mvwaddnstr(win, 1, 2, eng_line, win_w - 4);
        }
        wattroff(win, COLOR_PAIR(CP_TASK_CHECKED) | A_BOLD);
        wattroff(win, COLOR_PAIR(CP_TASK_UNCHECKED));

        // Row 2: Linked files summary
        char link_line[128];
        if (linked_files > 0) {
            snprintf(link_line, sizeof(link_line), "Linked .md files: %zu detected (%s - press L to toggle)",
                     linked_files, include_linked ? "INCLUDED" : "EXCLUDED");
            wattron(win, include_linked ? (COLOR_PAIR(CP_HEADER1) | A_BOLD) : COLOR_PAIR(CP_MARKER));
        } else {
            snprintf(link_line, sizeof(link_line), "Linked .md files: none (insert file links with F6 / Alt+H)");
            wattron(win, COLOR_PAIR(CP_MARKER) | A_DIM);
        }
        if (win_w > 4) {
            mvwaddnstr(win, 2, 2, link_line, win_w - 4);
        }
        wattroff(win, COLOR_PAIR(CP_HEADER1) | A_BOLD);
        wattroff(win, COLOR_PAIR(CP_MARKER) | A_DIM);

        // Row 3: Divider
        mvwaddch(win, 3, 0, ACS_LTEE);
        for (int x = 1; x < win_w - 1; ++x) mvwaddch(win, 3, x, ACS_HLINE);
        mvwaddch(win, 3, win_w - 1, ACS_RTEE);

        // Rows 4..7: Format list
        for (int i = 0; i < EXPORT_FORMAT_COUNT && 4 + i < win_h - 4; ++i) {
            int y = 4 + i;
            bool is_sel = ((int)selected_fmt == i);
            bool list_active = (focus == EXP_FOCUS_LIST);

            if (is_sel && list_active) {
                wattron(win, COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_REVERSE | A_BOLD);
                for (int x = 1; x < win_w - 1; ++x) mvwaddch(win, y, x, ' ');
                mvwaddstr(win, y, 2, "▶ ");
            } else if (is_sel) {
                wattron(win, COLOR_PAIR(CP_HEADER1) | A_BOLD);
                mvwaddstr(win, y, 2, "▸ ");
            } else {
                mvwaddstr(win, y, 2, "  ");
            }

            if (win_w > 6) {
                mvwaddnstr(win, y, 4, k_formats[i].label, win_w - 6);
            }

            if (win_w > 34) {
                int max_desc = win_w - 22 - 16;
                if (max_desc > 4) {
                    mvwaddnstr(win, y, 22, k_formats[i].desc, max_desc);
                }
            }

            // Right-aligned backend badge
            const char *badge = "[built-in]";
            if (k_formats[i].fmt == EXPORT_EPUB) {
                badge = has_pandoc ? "[pandoc]" : "[needs pandoc]";
            } else if (has_pandoc && engine != EXPORT_ENGINE_BUILTIN) {
                badge = "[pandoc]";
            }
            int blen = (int)strlen(badge);
            if (win_w - blen - 2 > 22) {
                mvwaddstr(win, y, win_w - blen - 2, badge);
            }

            if (is_sel && list_active) {
                wattroff(win, COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_REVERSE | A_BOLD);
            } else if (is_sel) {
                wattroff(win, COLOR_PAIR(CP_HEADER1) | A_BOLD);
            }
        }

        // Divider above output path
        int path_sep_y = win_h - 5;
        int path_y = win_h - 4;
        int err_y = win_h - 3;
        int foot_sep_y = win_h - 2;

        if (path_sep_y > 7) {
            mvwaddch(win, path_sep_y, 0, ACS_LTEE);
            for (int x = 1; x < win_w - 1; ++x) mvwaddch(win, path_sep_y, x, ACS_HLINE);
            mvwaddch(win, path_sep_y, win_w - 1, ACS_RTEE);
        }

        // Output path row
        bool path_focused = (focus == EXP_FOCUS_PATH);
        wattron(win, path_focused ? (COLOR_PAIR(CP_DEFAULT) | A_BOLD) : COLOR_PAIR(CP_MARKER));
        mvwaddstr(win, path_y, 2, "Output: ");
        wattroff(win, path_focused ? (COLOR_PAIR(CP_DEFAULT) | A_BOLD) : COLOR_PAIR(CP_MARKER));

        int box_x = 10;
        int box_w = win_w - box_x - 2;
        if (box_w > 2) {
            size_t plen = strlen(path_buf);
            const char *disp = path_buf;
            if ((int)plen > box_w - 1) {
                disp = path_buf + (plen - (size_t)(box_w - 1));
            }
            wattron(win, COLOR_PAIR(CP_DEFAULT) | (path_focused ? A_REVERSE : 0));
            for (int x = 0; x < box_w; ++x) mvwaddch(win, path_y, box_x + x, ' ');
            mvwaddnstr(win, path_y, box_x, disp, box_w - 1);
            wattroff(win, COLOR_PAIR(CP_DEFAULT) | (path_focused ? A_REVERSE : 0));

            if (path_focused) {
                curs_set(1);
                int cx = box_x + (int)strlen(disp);
                if (cx >= box_x + box_w) cx = box_x + box_w - 1;
                wmove(win, path_y, cx);
            } else {
                curs_set(0);
            }
        }

        // Inline error row (if last export attempt failed)
        if (err_msg[0] && win_w > 4) {
            wattron(win, COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD);
            mvwaddnstr(win, err_y, 2, err_msg, win_w - 4);
            wattroff(win, COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD);
        }

        // Footer divider & hints
        mvwaddch(win, foot_sep_y, 0, ACS_LTEE);
        for (int x = 1; x < win_w - 1; ++x) mvwaddch(win, foot_sep_y, x, ACS_HLINE);
        mvwaddch(win, foot_sep_y, win_w - 1, ACS_RTEE);

        const char *hints = has_pandoc
            ? " [Enter] Export  [1-4] Format  [Tab] Path  [L] Links  [E] Engine  [Esc] Cancel "
            : " [Enter] Export  [1-4] Format  [Tab] Path  [L] Links  [Esc/^C] Cancel ";
        if (win_w > 4) {
            wattron(win, COLOR_PAIR(CP_MARKER) | A_DIM);
            mvwaddnstr(win, win_h - 1, 2, hints, win_w - 4);
            wattroff(win, COLOR_PAIR(CP_MARKER) | A_DIM);
        }

        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) continue;

        if (ch == KEY_RESIZE) {
            delwin(win);
            ed->rendered_dirty = true;
            erase();
            ui_draw(ed);
            getmaxyx(stdscr, rows, cols);
            calc_export_win_geom(rows, cols, &win_h, &win_w, &win_y, &win_x);
            win = newwin(win_h, win_w, win_y, win_x);
            if (!win) return false;
            keypad(win, TRUE);
            wbkgd(win, COLOR_PAIR(CP_HELP_BOX));
            continue;
        }

        if (ch == 27 || ch == CTRL_KEY('q') || ch == CTRL_KEY('c') || ch == CTRL_KEY('g')) {
            break;
        }

        if (ch == '\t') {
            focus = (focus == EXP_FOCUS_LIST) ? EXP_FOCUS_PATH : EXP_FOCUS_LIST;
            continue;
        }

        if (ch == '\n' || ch == '\r') {
            err_msg[0] = '\0';
            char used_engine[32] = {0};
            size_t included_linked = 0;
            if (export_document_ex(&ed->buffer, selected_fmt, path_buf, engine,
                                   include_linked, &included_linked,
                                   used_engine, sizeof(used_engine), err_msg, sizeof(err_msg))) {
                if (included_linked > 0) {
                    editor_set_status(ed, "Exported %s to %s (+%zu linked, %s)",
                                      export_format_name(selected_fmt), path_buf,
                                      included_linked, used_engine);
                } else {
                    editor_set_status(ed, "Exported %s to %s (%s)",
                                      export_format_name(selected_fmt), path_buf, used_engine);
                }
                exported = true;
                break;
            }
            continue;
        }

        if (focus == EXP_FOCUS_PATH) {
            if (ch == KEY_UP) {
                focus = EXP_FOCUS_LIST;
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                size_t plen = strlen(path_buf);
                if (plen > 0) {
                    size_t prev = utf8_prev_char(path_buf, plen);
                    path_buf[prev] = '\0';
                }
                err_msg[0] = '\0';
            } else if (ch >= 32 && ch <= 126) {
                size_t plen = strlen(path_buf);
                if (plen + 1 < sizeof(path_buf)) {
                    path_buf[plen] = (char)ch;
                    path_buf[plen + 1] = '\0';
                }
                err_msg[0] = '\0';
            }
        } else {
            // EXP_FOCUS_LIST
            ExportFormat prev_fmt = selected_fmt;
            if (ch == KEY_UP || ch == 'k' || ch == 'K') {
                selected_fmt = (selected_fmt == 0)
                    ? (ExportFormat)(EXPORT_FORMAT_COUNT - 1)
                    : (ExportFormat)(selected_fmt - 1);
            } else if (ch == KEY_DOWN || ch == 'j' || ch == 'J') {
                selected_fmt = (ExportFormat)((selected_fmt + 1) % EXPORT_FORMAT_COUNT);
            } else if (ch >= '1' && ch <= '4') {
                selected_fmt = (ExportFormat)(ch - '1');
            } else if ((ch == 'e' || ch == 'E') && has_pandoc) {
                engine = (engine == EXPORT_ENGINE_AUTO) ? EXPORT_ENGINE_BUILTIN : EXPORT_ENGINE_AUTO;
                err_msg[0] = '\0';
            } else if (ch == 'l' || ch == 'L') {
                include_linked = !include_linked;
                err_msg[0] = '\0';
            } else if (ch == 'q' || ch == 'Q') {
                break;
            }

            if (selected_fmt != prev_fmt) {
                char updated[PATH_MAX];
                export_default_path(path_buf, selected_fmt, updated, sizeof(updated));
                snprintf(path_buf, sizeof(path_buf), "%s", updated);
                err_msg[0] = '\0';
            }
        }
    }

    delwin(win);
    curs_set(1);
    touchwin(stdscr);
    ui_draw(ed);
    return exported;
}
