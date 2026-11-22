/**
 * @file ui_dialog_outline.c
 * @brief Modal document outline dialog listing headings and jumping to anchors.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "ui_internal.h"
#include "markdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t row;
    int level;
    char title[128];
} OutlineHeading;

/** @brief Display a scrollable Document Outline modal listing all Markdown headings (H1-H6). */
bool ui_show_outline(Editor *ed) {
    if (!ed) return false;

    size_t cap = 32, count = 0;
    OutlineHeading *items = (OutlineHeading *)malloc(cap * sizeof(OutlineHeading));
    if (!items) return false;

    bool in_code = false;
    char lang[32] = {0};
    size_t nlines = buffer_line_count(&ed->buffer);
    int initial_sel = 0;

    for (size_t r = 0; r < nlines; ++r) {
        const char *l = buffer_line(&ed->buffer, r);
        size_t len = buffer_line_len(&ed->buffer, r);
        MdParsedLine pl;
        md_parse_line(l, len, &in_code, lang, sizeof(lang), &pl);
        if (pl.header_level > 0) {
            if (count >= cap) {
                cap *= 2;
                OutlineHeading *ni = (OutlineHeading *)realloc(items, cap * sizeof(OutlineHeading));
                if (!ni) { md_parsed_line_free(&pl); break; }
                items = ni;
            }
            OutlineHeading *h = &items[count];
            h->row = r;
            h->level = pl.header_level;
            size_t pos = 0;
            for (size_t s = 0; s < pl.span_count; ++s) {
                if (md_is_header_marker(pl.spans[s].type) ||
                    pl.spans[s].type == MD_BOLD_MARKER ||
                    pl.spans[s].type == MD_ITALIC_MARKER ||
                    pl.spans[s].type == MD_INLINE_CODE_MARKER) {
                    continue;
                }
                size_t sl = pl.spans[s].end - pl.spans[s].start;
                if (pos + sl >= sizeof(h->title) - 1) sl = sizeof(h->title) - 1 - pos;
                if (sl > 0) { memcpy(h->title + pos, l + pl.spans[s].start, sl); pos += sl; }
            }
            h->title[pos] = '\0';
            if (r <= ed->cursor_row) initial_sel = (int)count;
            count++;
        }
        md_parsed_line_free(&pl);
    }

    if (count == 0) {
        free(items);
        editor_set_status(ed, "Document Outline: no headings found");
        return false;
    }

    int rows, cols, win_h, win_w, win_y, win_x;
    getmaxyx(stdscr, rows, cols);
    calc_file_dialog_geom(rows, cols, &win_h, &win_w, &win_y, &win_x);

    WINDOW *win = newwin(win_h, win_w, win_y, win_x);
    if (!win) {
        free(items);
        return false;
    }
    keypad(win, TRUE);
    wbkgd(win, COLOR_PAIR(CP_HELP_BOX));
    curs_set(0);

    int sel = initial_sel;
    int scroll = 0;
    bool jumped = false;

    while (1) {
        werase(win);
        box(win, 0, 0);

        char title_buf[64];
        snprintf(title_buf, sizeof(title_buf), " Document Outline (%zu headings) ", count);
        int tx = (win_w - (int)strlen(title_buf)) / 2;
        if (tx < 1) tx = 1;
        wattron(win, COLOR_PAIR(CP_HEADER1) | A_BOLD);
        mvwaddnstr(win, 0, tx, title_buf, win_w - 2);
        wattroff(win, COLOR_PAIR(CP_HEADER1) | A_BOLD);

        int list_h = win_h - 4;
        if (list_h < 1) list_h = 1;
        if (sel < 0) sel = 0;
        if (sel >= (int)count) sel = (int)count - 1;
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + list_h) scroll = sel - list_h + 1;

        for (int i = 0; i < list_h; ++i) {
            int idx = scroll + i;
            int y = 1 + i;
            if (idx >= (int)count) break;

            const OutlineHeading *h = &items[idx];
            bool is_sel = (idx == sel);
            int indent = (h->level - 1) * 2;
            attr_t h_attr = theme_token_attr((MdTokenType)(MD_HEADER_TEXT_1 + (h->level - 1) * 2), false);

            if (is_sel) {
                wattron(win, COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_REVERSE | A_BOLD);
                for (int x = 1; x < win_w - 1; ++x) mvwaddch(win, y, x, ' ');
                mvwaddstr(win, y, 2, "▶ ");
            } else {
                wattron(win, h_attr);
                mvwaddstr(win, y, 2, "  ");
            }

            char line_tag[24];
            snprintf(line_tag, sizeof(line_tag), "L%-4zu", h->row + 1);
            int text_x = 4 + indent;
            if (win_w > 14 && text_x < win_w - 9) {
                char entry[160];
                snprintf(entry, sizeof(entry), "H%d %s", h->level, h->title);
                mvwaddnstr(win, y, text_x, entry, (win_w - 9) - text_x);
            }
            if (win_w > 10) {
                mvwaddnstr(win, y, win_w - 7, line_tag, 5);
            }

            if (is_sel) {
                wattroff(win, COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_REVERSE | A_BOLD);
            } else {
                wattroff(win, h_attr);
            }
        }

        draw_box_hrule(win, win_h - 2, win_w);
        if (win_w > 4) {
            wattron(win, COLOR_PAIR(CP_MARKER) | A_DIM);
            mvwaddnstr(win, win_h - 1, 2, " [Enter] Jump  [↑/↓/PgUp/PgDn] Browse  [Esc/^C/q] Close ", win_w - 4);
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
            calc_file_dialog_geom(rows, cols, &win_h, &win_w, &win_y, &win_x);
            win = newwin(win_h, win_w, win_y, win_x);
            if (!win) { free(items); return false; }
            keypad(win, TRUE);
            wbkgd(win, COLOR_PAIR(CP_HELP_BOX));
            curs_set(0);
            continue;
        }
        if (ch == 27 || ch == 'q' || ch == 'Q' || ch == CTRL_KEY('c') || ch == CTRL_KEY('g')) {
            break;
        } else if (ch == '\n' || ch == '\r') {
            editor_goto_line(ed, items[sel].row + 1, 1);
            editor_set_status(ed, "Jumped to H%d: %s", items[sel].level, items[sel].title);
            jumped = true;
            break;
        } else if (ch == KEY_UP || ch == 'k' || ch == 'K') {
            if (sel > 0) sel--;
        } else if (ch == KEY_DOWN || ch == 'j' || ch == 'J') {
            if (sel + 1 < (int)count) sel++;
        } else if (ch == KEY_PPAGE) {
            sel -= list_h;
            if (sel < 0) sel = 0;
        } else if (ch == KEY_NPAGE) {
            sel += list_h;
            if (sel >= (int)count) sel = (int)count - 1;
        } else if (ch == KEY_HOME) {
            sel = 0;
        } else if (ch == KEY_END) {
            sel = (int)count - 1;
        }
    }

    free(items);
    delwin(win);
    curs_set(1);
    touchwin(stdscr);
    ui_draw(ed);
    return jumped;
}
