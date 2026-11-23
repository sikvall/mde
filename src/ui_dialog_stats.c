/**
 * @file ui_dialog_stats.c
 * @brief Modal dialog displaying comprehensive document and selection statistics.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "ui_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * @brief Compute document and selection metrics.
 */
void compute_document_stats(const Editor *ed, DocumentStats *stats) {
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    if (!ed) return;

    const Buffer *buf = &ed->buffer;
    stats->total_lines = buffer_line_count(buf);
    stats->total_words = buffer_word_count(buf);

    for (size_t r = 0; r < stats->total_lines; ++r) {
        const char *l = buffer_line(buf, r);
        size_t len = buffer_line_len(buf, r);
        stats->total_chars += len;
        if (r + 1 < stats->total_lines) {
            stats->total_chars++; // newline between lines
        }
        for (size_t i = 0; i < len; ++i) {
            if (!isspace((unsigned char)l[i])) {
                stats->total_chars_no_spaces++;
            }
        }
    }

    // Estimated reading time at 200 words per minute
    size_t total_sec = (stats->total_words * 60) / 200;
    stats->read_time_min = (int)(total_sec / 60);
    stats->read_time_sec = (int)(total_sec % 60);

    // Selection statistics
    size_t s_row = 0, s_col = 0, e_row = 0, e_col = 0;
    if (editor_get_selection(ed, &s_row, &s_col, &e_row, &e_col)) {
        stats->has_selection = true;
        stats->sel_lines = (e_row >= s_row) ? (e_row - s_row + 1) : 0;

        bool in_word = false;
        for (size_t r = s_row; r <= e_row; ++r) {
            const char *l = buffer_line(buf, r);
            size_t len = buffer_line_len(buf, r);
            size_t from = (r == s_row) ? s_col : 0;
            size_t to = (r == e_row) ? e_col : len;
            if (from > len) from = len;
            if (to > len) to = len;

            if (to > from) {
                stats->sel_chars += (to - from);
                for (size_t i = from; i < to; ++i) {
                    unsigned char c = (unsigned char)l[i];
                    if (!isspace(c)) {
                        stats->sel_chars_no_spaces++;
                        if (!in_word) {
                            stats->sel_words++;
                            in_word = true;
                        }
                    } else {
                        in_word = false;
                    }
                }
            }
            if (r < e_row) {
                stats->sel_chars++; // count newline
                in_word = false;
            }
        }
    }
}

/**
 * @brief Display an interactive modal popover showing document and selection statistics.
 */
bool ui_show_document_stats(Editor *ed) {
    if (!ed) return false;

    DocumentStats stats;
    compute_document_stats(ed, &stats);

    int rows = ed->screen_rows;
    int cols = ed->screen_cols;

    int box_w = 60;
    if (box_w > cols - 4) box_w = cols - 4;
    if (box_w < 44) box_w = 44;
    if (box_w > cols) box_w = cols;
    if (box_w < 12) box_w = cols > 0 ? cols : 12;

    int box_h = stats.has_selection ? 18 : 13;
    if (box_h > rows - 2) box_h = rows - 2;
    if (box_h < 6) box_h = rows > 0 ? rows : 6;

    int start_y = (rows - box_h) / 2;
    int start_x = (cols - box_w) / 2;
    if (start_y < 0) start_y = 0;
    if (start_x < 0) start_x = 0;

    WINDOW *win = newwin(box_h, box_w, start_y, start_x);
    if (!win) return false;

    keypad(win, TRUE);
    wbkgd(win, COLOR_PAIR(CP_HELP_BOX));
    curs_set(0);

    const char *doc_name = (ed->buffer.file_path && ed->buffer.file_path[0])
                               ? ed->buffer.file_path : "untitled.md";
    const char *slash = strrchr(doc_name, '/');
    if (slash) doc_name = slash + 1;

    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), " Document Statistics ");
    int tlen = (int)strlen(title_buf);
    int tx = (box_w - tlen) / 2;

    werase(win);
    box(win, 0, 0);

    wattron(win, COLOR_PAIR(CP_HEADER1) | A_BOLD);
    mvwaddstr(win, 1, tx > 1 ? tx : 1, title_buf);
    wattroff(win, COLOR_PAIR(CP_HEADER1) | A_BOLD);

    draw_box_hrule(win, 2, box_w);

    int y = 3;
    if (y < box_h - 3 && box_w > 10) {
        wattron(win, COLOR_PAIR(CP_BOLD) | A_BOLD);
        mvwprintw(win, y++, 3, "File: %.*s", box_w - 10, doc_name);
        wattroff(win, COLOR_PAIR(CP_BOLD) | A_BOLD);
    }

    if (y < box_h - 3) mvwprintw(win, y++, 3, "%-26s %10zu", "Total Lines:", stats.total_lines);
    if (y < box_h - 3) mvwprintw(win, y++, 3, "%-26s %10zu", "Total Words:", stats.total_words);
    if (y < box_h - 3) mvwprintw(win, y++, 3, "%-26s %10zu", "Total Characters:", stats.total_chars);
    if (y < box_h - 3) mvwprintw(win, y++, 3, "%-26s %10zu", "Chars (no whitespace):", stats.total_chars_no_spaces);

    if (y < box_h - 3) {
        if (stats.read_time_min > 0) {
            mvwprintw(win, y++, 3, "%-26s %d min %d sec", "Reading Time (200 wpm):", stats.read_time_min, stats.read_time_sec);
        } else {
            mvwprintw(win, y++, 3, "%-26s %d sec", "Reading Time (200 wpm):", stats.read_time_sec);
        }
    }

    if (stats.has_selection && y + 4 < box_h - 2) {
        draw_box_hrule(win, y++, box_w);
        wattron(win, COLOR_PAIR(CP_HEADER2) | A_BOLD);
        mvwaddstr(win, y++, 3, "Active Selection:");
        wattroff(win, COLOR_PAIR(CP_HEADER2) | A_BOLD);
        mvwprintw(win, y++, 5, "%-24s %10zu", "Selected Lines:", stats.sel_lines);
        mvwprintw(win, y++, 5, "%-24s %10zu", "Selected Words:", stats.sel_words);
        mvwprintw(win, y++, 5, "%-24s %10zu", "Selected Characters:", stats.sel_chars);
    }

    draw_box_hrule(win, box_h - 3, box_w);
    const char *foot = "[Esc / Enter / q / Alt+D] Close";
    int fx = (box_w - (int)strlen(foot)) / 2;
    wattron(win, COLOR_PAIR(CP_MARKER));
    if (box_w > 2) mvwaddnstr(win, box_h - 2, fx > 1 ? fx : 1, foot, box_w - 2);
    wattroff(win, COLOR_PAIR(CP_MARKER));

    wrefresh(win);

    while (1) {
        int ch = wgetch(win);
        if (ch == 27 || ch == '\n' || ch == '\r' ||
            ch == 'q' || ch == 'Q' || ch == 'd' || ch == 'D' ||
            ch == KEY_F(1) || ch == KEY_F(10)) {
            break;
        }
    }

    delwin(win);
    curs_set(1);
    touchwin(stdscr);
    ui_draw(ed);
    return true;
}
