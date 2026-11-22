/**
 * @file ui_dialog_metadata.c
 * @brief Interactive modal dialog for viewing and editing document & EPUB metadata fields.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "ui_internal.h"
#include "epub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

typedef struct {
    const char *label;
    char *val;
    size_t cap;
    size_t cursor;
} MetaField;

static void calc_meta_win_geom(int rows, int cols, int *win_h, int *win_w, int *win_y, int *win_x) {
    int h = 17;
    int w = 76;
    if (w > cols - 4) w = cols - 4;
    if (w < 52) w = 52;
    if (w > cols) w = cols;

    if (h > rows - 2) h = rows - 2;
    if (h < 15) h = 15;
    if (h > rows) h = rows;

    int y = (rows - h) / 2;
    int x = (cols - w) / 2;
    if (y < 0) y = 0;
    if (x < 0) x = 0;

    *win_h = h;
    *win_w = w;
    *win_y = y;
    *win_x = x;
}

bool ui_dialog_metadata(Editor *ed) {
    if (!ed) return false;

    EpubMetadata meta;
    memset(&meta, 0, sizeof(meta));
    size_t body_start = 0;
    epub_parse_front_matter(&ed->buffer, &meta, &body_start);

    // Initial fallbacks if fields are empty
    if (!meta.title[0]) {
        char first_h1[128] = {0};
        size_t lines = buffer_line_count(&ed->buffer);
        for (size_t r = 0; r < lines; ++r) {
            const char *l = buffer_line(&ed->buffer, r);
            size_t len = buffer_line_len(&ed->buffer, r);
            if (len >= 2 && l[0] == '#' && l[1] == ' ') {
                size_t hlen = len - 2;
                if (hlen >= sizeof(first_h1)) hlen = sizeof(first_h1) - 1;
                memcpy(first_h1, l + 2, hlen);
                first_h1[hlen] = '\0';
                break;
            }
        }
        if (first_h1[0]) {
            snprintf(meta.title, sizeof(meta.title), "%s", first_h1);
        } else if (ed->buffer.file_path && ed->buffer.file_path[0]) {
            const char *slash = strrchr(ed->buffer.file_path, '/');
            const char *base = slash ? slash + 1 : ed->buffer.file_path;
            const char *dot = strrchr(base, '.');
            size_t stem = dot ? (size_t)(dot - base) : strlen(base);
            snprintf(meta.title, sizeof(meta.title), "%.*s", (int)stem, base);
        } else {
            snprintf(meta.title, sizeof(meta.title), "Untitled Document");
        }
    }
    if (!meta.author[0]) {
        const char *user = getenv("USER");
        snprintf(meta.author, sizeof(meta.author), "%s", user ? user : "Anonymous");
    }
    if (!meta.language[0]) {
        snprintf(meta.language, sizeof(meta.language), "en");
    }
    if (!meta.date[0]) {
        time_t t = time(NULL);
        struct tm tm_utc;
        gmtime_r(&t, &tm_utc);
        strftime(meta.date, sizeof(meta.date), "%Y-%m-%d", &tm_utc);
    }

    MetaField fields[10] = {
        { "Title:        ", meta.title, sizeof(meta.title), strlen(meta.title) },
        { "Author:       ", meta.author, sizeof(meta.author), strlen(meta.author) },
        { "Language:     ", meta.language, sizeof(meta.language), strlen(meta.language) },
        { "Date:         ", meta.date, sizeof(meta.date), strlen(meta.date) },
        { "Publisher:    ", meta.publisher, sizeof(meta.publisher), strlen(meta.publisher) },
        { "Identifier:   ", meta.identifier, sizeof(meta.identifier), strlen(meta.identifier) },
        { "Rights:       ", meta.rights, sizeof(meta.rights), strlen(meta.rights) },
        { "Description:  ", meta.description, sizeof(meta.description), strlen(meta.description) },
        { "Cover Picture:", meta.cover_path, sizeof(meta.cover_path), strlen(meta.cover_path) },
        { "Custom CSS:   ", meta.css_path, sizeof(meta.css_path), strlen(meta.css_path) }
    };
    const int field_count = 10;
    const int total_items = 12; // 0..9: fields, 10: Save, 11: Cancel
    int focus = 0;

    int rows, cols, win_h, win_w, win_y, win_x;
    getmaxyx(stdscr, rows, cols);
    calc_meta_win_geom(rows, cols, &win_h, &win_w, &win_y, &win_x);

    WINDOW *win = newwin(win_h, win_w, win_y, win_x);
    if (!win) return false;
    keypad(win, TRUE);
    wbkgd(win, COLOR_PAIR(CP_HELP_BOX));

    bool saved = false;

    while (1) {
        werase(win);
        box(win, 0, 0);

        // Title
        const char *wtitle = " Document & EPUB Metadata ";
        int tx = (win_w - (int)strlen(wtitle)) / 2;
        if (tx < 1) tx = 1;
        wattron(win, COLOR_PAIR(CP_HEADER1) | A_BOLD);
        mvwaddnstr(win, 0, tx, wtitle, win_w - 2);
        wattroff(win, COLOR_PAIR(CP_HEADER1) | A_BOLD);

        // Render fields 0..9
        int box_x = 18;
        int box_w = win_w - box_x - 3;
        if (box_w < 10) box_w = 10;

        for (int i = 0; i < field_count && i + 1 < win_h - 4; ++i) {
            int y = 1 + i;
            bool is_active = (focus == i);

            // Label
            wattron(win, is_active ? (COLOR_PAIR(CP_HEADER1) | A_BOLD) : COLOR_PAIR(CP_DEFAULT));
            mvwaddnstr(win, y, 2, fields[i].label, 15);
            wattroff(win, is_active ? (COLOR_PAIR(CP_HEADER1) | A_BOLD) : COLOR_PAIR(CP_DEFAULT));

            // Input box background
            wattron(win, is_active ? (COLOR_PAIR(CP_DEFAULT) | A_REVERSE) : COLOR_PAIR(CP_HELP_BOX));
            for (int x = 0; x < box_w; ++x) {
                mvwaddch(win, y, box_x + x, ' ');
            }

            // Text content with horizontal scroll if exceeding field box width
            size_t flen = strlen(fields[i].val);
            size_t fcurs = fields[i].cursor;
            if (fcurs > flen) fcurs = flen;

            int view_offset = 0;
            if (is_active && (int)fcurs >= box_w - 2) {
                view_offset = (int)fcurs - (box_w - 3);
            }
            if (view_offset < 0) view_offset = 0;

            const char *disp_str = fields[i].val + view_offset;
            mvwaddnstr(win, y, box_x, disp_str, box_w - 1);
            wattroff(win, is_active ? (COLOR_PAIR(CP_DEFAULT) | A_REVERSE) : COLOR_PAIR(CP_HELP_BOX));

            if (is_active) {
                curs_set(1);
                int cx = box_x + (int)fcurs - view_offset;
                if (cx >= box_x && cx < box_x + box_w) {
                    wmove(win, y, cx);
                }
            }
        }

        if (focus >= field_count) {
            curs_set(0);
        }

        // Divider above action buttons
        int btn_sep_y = win_h - 4;
        int btn_y = win_h - 3;
        int foot_sep_y = win_h - 2;

        if (btn_sep_y > 10) {
            draw_box_hrule(win, btn_sep_y, win_w);
        }

        // Action Buttons: Save (10) and Cancel (11)
        const char *btn_save = " [ Save to Document ] ";
        const char *btn_cancel = " [ Cancel ] ";
        int b1_x = (win_w - 44) / 2;
        if (b1_x < 4) b1_x = 4;
        int b2_x = b1_x + 24;

        if (btn_y < win_h - 1) {
            bool save_sel = (focus == 10);
            wattron(win, save_sel ? (COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_REVERSE | A_BOLD)
                                  : (COLOR_PAIR(CP_HEADER1) | A_BOLD));
            mvwaddstr(win, btn_y, b1_x, btn_save);
            wattroff(win, save_sel ? (COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_REVERSE | A_BOLD)
                                   : (COLOR_PAIR(CP_HEADER1) | A_BOLD));

            bool cancel_sel = (focus == 11);
            wattron(win, cancel_sel ? (COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_REVERSE | A_BOLD)
                                    : (COLOR_PAIR(CP_MARKER) | A_BOLD));
            mvwaddstr(win, btn_y, b2_x, btn_cancel);
            wattroff(win, cancel_sel ? (COLOR_PAIR(CP_STATUS_BAR_ACCENT) | A_REVERSE | A_BOLD)
                                     : (COLOR_PAIR(CP_MARKER) | A_BOLD));
        }

        // Bottom instruction hints
        if (foot_sep_y > btn_sep_y) {
            draw_box_hrule(win, foot_sep_y, win_w);
        }
        if (win_w > 4) {
            const char *hints = " [Tab/↑/↓] Field  [Enter] Next  [^R/F6] Browse  [^S] Save  [Esc] Cancel ";
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
            calc_meta_win_geom(rows, cols, &win_h, &win_w, &win_y, &win_x);
            win = newwin(win_h, win_w, win_y, win_x);
            if (!win) return false;
            keypad(win, TRUE);
            wbkgd(win, COLOR_PAIR(CP_HELP_BOX));
            continue;
        }

        if (ch == 27 || ch == CTRL_KEY('c')) {
            // Cancel without saving
            break;
        }

        if (ch == CTRL_KEY('s')) {
            // Save immediately
            saved = true;
            break;
        }

        if (ch == '\t' || ch == KEY_DOWN) {
            focus = (focus + 1) % total_items;
            continue;
        }

        if (ch == KEY_BTAB || ch == KEY_UP) {
            focus = (focus == 0) ? (total_items - 1) : (focus - 1);
            continue;
        }

        if (focus == 10) {
            // [ Save to Document ] button
            if (ch == '\n' || ch == '\r' || ch == ' ') {
                saved = true;
                break;
            } else if (ch == KEY_RIGHT) {
                focus = 11;
            }
        } else if (focus == 11) {
            // [ Cancel ] button
            if (ch == '\n' || ch == '\r' || ch == ' ') {
                break;
            } else if (ch == KEY_LEFT) {
                focus = 10;
            }
        } else {
            // Editing field 0..9
            MetaField *f = &fields[focus];
            size_t flen = strlen(f->val);
            if (f->cursor > flen) f->cursor = flen;

            if (ch == CTRL_KEY('r') || ch == KEY_F(6)) {
                // Open file browser dialogue to select a file path
                char picked[PATH_MAX] = {0};
                if (f->val[0]) {
                    snprintf(picked, sizeof(picked), "%s", f->val);
                }
                if (ui_file_dialog(ed, FILE_DIALOG_OPEN, picked, sizeof(picked))) {
                    snprintf(f->val, f->cap, "%s", picked);
                    f->cursor = strlen(f->val);
                }
                ed->rendered_dirty = true;
                erase();
                ui_draw(ed);
                continue;
            } else if (ch == '\n' || ch == '\r') {
                focus = (focus + 1) % total_items;
            } else if (ch == KEY_LEFT) {
                if (f->cursor > 0) {
                    f->cursor = utf8_prev_char(f->val, f->cursor);
                }
            } else if (ch == KEY_RIGHT) {
                if (f->cursor < flen) {
                    f->cursor = utf8_next_char(f->val, f->cursor, flen);
                }
            } else if (ch == KEY_HOME || ch == CTRL_KEY('a')) {
                f->cursor = 0;
            } else if (ch == KEY_END || ch == CTRL_KEY('e')) {
                f->cursor = flen;
            } else if (ch == CTRL_KEY('u')) {
                f->val[0] = '\0';
                f->cursor = 0;
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                if (f->cursor > 0) {
                    size_t prev = utf8_prev_char(f->val, f->cursor);
                    memmove(f->val + prev, f->val + f->cursor, flen - f->cursor + 1);
                    f->cursor = prev;
                }
            } else if (ch == KEY_DC) {
                if (f->cursor < flen) {
                    size_t next = utf8_next_char(f->val, f->cursor, flen);
                    memmove(f->val + f->cursor, f->val + next, flen - next + 1);
                }
            } else {
                char seq[8];
                size_t slen = read_utf8_input(win, ch, seq, sizeof(seq));
                if (slen > 0 && flen + slen < f->cap) {
                    memmove(f->val + f->cursor + slen, f->val + f->cursor, flen - f->cursor + 1);
                    memcpy(f->val + f->cursor, seq, slen);
                    f->cursor += slen;
                }
            }
        }
    }

    delwin(win);
    curs_set(0);

    if (saved) {
        editor_update_front_matter(ed, &meta);
        editor_set_status(ed, "Metadata updated in document front matter.");
    }

    ed->rendered_dirty = true;
    clearok(stdscr, TRUE);
    erase();
    ui_draw(ed);
    return saved;
}
