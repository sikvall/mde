/**
 * @file ui.c
 * @brief Implementation of user interface, layout, column ruler, and dialogs.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _XOPEN_SOURCE_EXTENDED 1
#include "ui.h"
#include "ui_internal.h"
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

/** @brief Initialize ncurses, raw input, UTF-8 locale, steady cursor, theme, and mouse events. */
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
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
}

/** @brief Restore default terminal cursor style and exit ncurses cleanly. */
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
 * every 5 columns, tens digits (1, 2, 3...) every 10 columns, a dynamic wrap
 * guide (┆) at the configured auto line break column, and a cursor marker (▼)
 * above the active column.
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
        if (r >= total_lines) break;
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

    size_t cur_rend_idx = (size_t)-1;
    int cur_vcol = -1;
    if (ed->view_mode == VIEW_SPLIT && total_rendered > 0 && ed->cursor_row < buffer_line_count(&ed->buffer)) {
        cur_rend_idx = rendered_doc_find_line(&ed->rendered_doc, ed->cursor_row);
        cur_vcol = renderer_get_cursor_vcol(&ed->buffer, &ed->rendered_doc, ed->cursor_row, ed->cursor_col);
    }

    for (int y = 0; y < text_height; ++y) {
        size_t rend_idx = ed->preview_scroll_row + (size_t)y;
        int screen_y = text_start_y + y;
        int render_x = start_col;
        bool is_cursor_line = (rend_idx == cur_rend_idx);

        // Left margin padding: in VIEW_SPLIT, highlight active cursor line with pointer marker
        int pad = width >= 2 ? 2 : width;
        if (is_cursor_line && pad >= 2) {
            attron(COLOR_PAIR(CP_GUTTER_MARKER) | A_BOLD);
            mvaddstr(screen_y, render_x, "▶ ");
            attroff(COLOR_PAIR(CP_GUTTER_MARKER) | A_BOLD);
            render_x += 2;
        } else {
            for (int p = 0; p < pad; ++p) {
                mvaddch(screen_y, render_x++, ' ');
            }
        }

        if (rend_idx < total_rendered) {
            RenderedLine *rl = &ed->rendered_doc.lines[rend_idx];

            for (size_t c = 0; c < rl->chunk_count; ++c) {
                RenderChunk *rc = &rl->chunks[c];
                if (render_x >= start_col + width) break;

                attrset(rc->attr);
                size_t byte_pos = 0;
                while (byte_pos < rc->text_len && render_x < start_col + width) {
                    size_t ch_len = utf8_char_len((unsigned char)rc->text[byte_pos]);
                    int ch_w = utf8_visual_width_range(rc->text, byte_pos, byte_pos + ch_len);

                    if (render_x + ch_w <= start_col + width) {
                        int current_rel_col = render_x - (start_col + pad);
                        bool is_cursor_cell = (is_cursor_line && cur_vcol >= 0 &&
                                               current_rel_col <= cur_vcol &&
                                               current_rel_col + ch_w > cur_vcol);
                        if (is_cursor_cell) {
                            attron(A_REVERSE | A_BOLD);
                            mvaddnstr(screen_y, render_x, rc->text + byte_pos, ch_len);
                            attroff(A_REVERSE | A_BOLD);
                            attrset(rc->attr);
                        } else {
                            mvaddnstr(screen_y, render_x, rc->text + byte_pos, ch_len);
                        }
                        render_x += ch_w;
                    }
                    byte_pos += ch_len;
                }
            }
            attrset(COLOR_PAIR(CP_DEFAULT));
        }

        // Clear rest of line in preview pane; if cursor is at or beyond end of line, show cursor badge
        int cur_screen_x = (is_cursor_line && cur_vcol >= 0) ? (start_col + pad + cur_vcol) : -1;
        while (render_x < start_col + width) {
            if (render_x == cur_screen_x) {
                attron(A_REVERSE | A_BOLD | COLOR_PAIR(CP_STATUS_BAR_ACCENT));
                mvaddch(screen_y, render_x++, ' ');
                attroff(A_REVERSE | A_BOLD | COLOR_PAIR(CP_STATUS_BAR_ACCENT));
            } else {
                mvaddch(screen_y, render_x++, ' ');
            }
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
    { "M-G", "Go Line" },
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

static const ShortcutHint s_hints_r4[] = {
    { "F9", "Outline" },
    { "M-D", "Stats" },
    { "M-M", "Jump" },
    { "F7", "Follow" },
    { "M-J", "Reflow" },
    { "F10", "Hints" }
};

/** @brief Compute the number of bottom shortcut hint rows appropriate for screen_rows. */
int ui_get_shortcut_hint_rows(int screen_rows) {
    if (screen_rows >= 24) return 4;
    if (screen_rows >= 18) return 3;
    if (screen_rows >= 12) return 2;
    if (screen_rows >= 6) return 1;
    return 0;
}

/** @brief Render the bottom Nano-style shortcut hint rows with reverse-video key badges. */
static void draw_shortcut_hints(int screen_cols, int start_y, int num_rows) {
    if (num_rows <= 0 || screen_cols <= 2) return;
    if (num_rows > 4) num_rows = 4;
    const ShortcutHint *all_rows[4] = { s_hints_r1, s_hints_r2, s_hints_r3, s_hints_r4 };

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

/** @brief Emit DECSCUSR escape sequence to keep cursor steady block (Insert) or underline (Overwrite). */
void update_cursor_shape(bool insert_mode) {
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

/** @brief Render the active view mode, status bar, shortcut hints, and hardware cursor. */
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

    int num_hint_rows = (ed && ed->show_keybindings) ? ui_get_shortcut_hint_rows(rows) : 0;
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
    if (num_hint_rows > 0) {
        draw_shortcut_hints(cols, status_bar_y + 1, num_hint_rows);
    }

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

/**
 * @brief Read a printable ASCII or multi-byte UTF-8 sequence starting with first_ch from win.
 * @return Number of UTF-8 bytes written into out_seq (0 if first_ch is not printable text).
 */
/**
 * @brief Draw a full-width horizontal box divider with left/right T-junctions.
 */
void draw_box_hrule(WINDOW *win, int y, int width) {
    mvwaddch(win, y, 0, ACS_LTEE);
    for (int x = 1; x < width - 1; ++x) {
        mvwaddch(win, y, x, ACS_HLINE);
    }
    mvwaddch(win, y, width - 1, ACS_RTEE);
}

size_t read_utf8_input(WINDOW *win, int first_ch, char *out_seq, size_t cap) {
    if (!out_seq || cap < 2) return 0;
    if (first_ch >= 32 && first_ch <= 126) {
        out_seq[0] = (char)first_ch;
        out_seq[1] = '\0';
        return 1;
    }
    if (first_ch >= 0xC0 && first_ch <= 0xFF) {
        size_t ulen = utf8_char_len((unsigned char)first_ch);
        if (ulen >= cap) return 0;
        out_seq[0] = (char)first_ch;
        size_t got = 1;
        for (size_t b = 1; b < ulen; ++b) {
            int nb = win ? wgetch(win) : getch();
            if (nb == ERR || nb < 0 || nb > 255) break;
            out_seq[got++] = (char)nb;
        }
        out_seq[got] = '\0';
        return got;
    }
    return 0;
}

/** @brief Shared interactive bottom-row prompt with full UTF-8 input support. */
static bool ui_prompt_internal(Editor *ed, const char *prompt, char *out_buf,
                               size_t buf_size, bool allow_empty) {
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
            while (utf8_visual_width(disp, strlen(disp)) > max_vis && *disp) {
                disp += utf8_char_len((unsigned char)*disp);
            }
            attron(COLOR_PAIR(CP_DEFAULT));
            mvaddstr(y, input_x, disp);
            attroff(COLOR_PAIR(CP_DEFAULT));
            wmove(stdscr, y, input_x + utf8_visual_width(disp, strlen(disp)));
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
            return allow_empty ? true : (len > 0);
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (len > 0) {
                size_t prev = utf8_prev_char(out_buf, len);
                out_buf[prev] = '\0';
                len = prev;
            }
        } else {
            char seq[8];
            size_t slen = read_utf8_input(NULL, ch, seq, sizeof(seq));
            if (slen > 0 && len + slen < buf_size) {
                memcpy(out_buf + len, seq, slen);
                len += slen;
                out_buf[len] = '\0';
            }
        }
    }
}

/** @brief Run an interactive single-line text input prompt on the bottom row. */
bool ui_prompt(Editor *ed, const char *prompt, char *out_buf, size_t buf_size) {
    return ui_prompt_internal(ed, prompt, out_buf, buf_size, false);
}

/** @brief Run an interactive single-line text input prompt allowing an empty submission. */
bool ui_prompt_allow_empty(Editor *ed, const char *prompt, char *out_buf, size_t buf_size) {
    return ui_prompt_internal(ed, prompt, out_buf, buf_size, true);
}

/** @brief Run Nano-compatible interactive Search & Replace across the document. */
void ui_search_and_replace(Editor *ed, char *search_buf, size_t search_cap) {
    if (!ed || !search_buf || search_cap == 0) return;

    char query[128] = {0};
    if (!ui_prompt(ed, "Search (to replace)", query, sizeof(query))) {
        editor_set_status(ed, "Replace cancelled");
        return;
    }

    char replacement[128] = {0};
    if (!ui_prompt_allow_empty(ed, "Replace with", replacement, sizeof(replacement))) {
        editor_set_status(ed, "Replace cancelled");
        return;
    }

    snprintf(search_buf, search_cap, "%s", query);
    size_t rlen = strlen(replacement);
    size_t replaced = 0;
    size_t scan_row = ed->cursor_row;
    size_t scan_col = ed->cursor_col;

    while (editor_search_find_from(ed, query, scan_row, scan_col, false)) {
        ui_draw(ed);

        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        int y = rows > 0 ? rows - 1 : 0;
        move(y, 0);
        clrtoeol();
        const char *msg = "Replace this instance? (y = Yes, n = No, a = All, Esc/^C = Cancel): ";
        attron(COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_BOLD);
        if (cols > 2) mvaddnstr(y, 1, msg, cols - 2);
        attroff(COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_BOLD);
        refresh();

        int ch = getch();
        if (ch == ERR) continue;
        if (ch == KEY_RESIZE) {
            ed->rendered_dirty = true;
            erase();
            continue;
        }
        if (ch == 'y' || ch == 'Y') {
            size_t mrow = ed->search_match_row;
            size_t mcol = ed->search_match_col;
            if (editor_replace_current_match(ed, replacement, 0)) {
                replaced++;
            }
            scan_row = mrow;
            scan_col = mcol + rlen;
        } else if (ch == 'n' || ch == 'N') {
            scan_row = ed->search_match_row;
            scan_col = ed->search_match_col + 1;
        } else if (ch == 'a' || ch == 'A') {
            uint64_t group = buffer_next_group(&ed->buffer);
            size_t mrow = ed->search_match_row;
            size_t mcol = ed->search_match_col;
            if (editor_replace_current_match(ed, replacement, group)) {
                replaced++;
            }
            scan_row = mrow;
            scan_col = mcol + rlen;
            while (editor_search_find_from(ed, query, scan_row, scan_col, false)) {
                mrow = ed->search_match_row;
                mcol = ed->search_match_col;
                if (!editor_replace_current_match(ed, replacement, group)) break;
                replaced++;
                scan_row = mrow;
                scan_col = mcol + rlen;
            }
            break;
        } else if (ch == 27 || ch == CTRL_KEY('c') || ch == CTRL_KEY('g')) {
            break;
        }
    }

    ed->search_active = false;
    if (replaced > 0) {
        editor_set_status(ed, "Replaced %zu occurrence%s", replaced, replaced == 1 ? "" : "s");
    } else {
        editor_set_status(ed, "No occurrences replaced");
    }
}

/** @brief Prompt the user to save, discard, or cancel when unsaved edits exist. */
int ui_confirm_unsaved(Editor *ed, const char *action_desc) {
    if (!ed || !ed->buffer.is_dirty) return 0;

    const char *cur_name = "[No Name]";
    if (ed->buffer.file_path && ed->buffer.file_path[0]) {
        const char *slash = strrchr(ed->buffer.file_path, '/');
        cur_name = slash ? (slash + 1) : ed->buffer.file_path;
    }

    char msg[256];
    snprintf(msg, sizeof(msg),
             "Save changes to '%s' before %s? (y = Save, n = Discard, ^C/Esc = Cancel): ",
             cur_name, action_desc ? action_desc : "continuing");

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

/** @brief Prompt the user to save or discard unsaved changes before exiting. */
int ui_confirm_quit(Editor *ed) {
    return ui_confirm_unsaved(ed, "exit");
}

