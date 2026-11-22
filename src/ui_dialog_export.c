/**
 * @file ui_dialog_export.c
 * @brief Interactive export modal menu for HTML, XML, EPUB, and LaTeX export.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "ui_internal.h"
#include "export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <sys/stat.h>

/** @brief Compute centered geometry for the Export Menu modal dialog. */
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

/** @brief Display the interactive Export Menu modal for HTML, XML, EPUB, and LaTeX export. */
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
    bool confirm_overwrite = false;

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
            snprintf(eng_line, sizeof(eng_line), "Engine: %s  (pandoc detected - press E to toggle)",
                     engine == EXPORT_ENGINE_PANDOC ? "Pandoc (external)" : "Built-in C11 (native)");
            wattron(win, COLOR_PAIR(CP_TASK_CHECKED) | A_BOLD);
        } else {
            snprintf(eng_line, sizeof(eng_line), "Engine: Built-in C11 (native conversion; pandoc optional)");
            wattron(win, COLOR_PAIR(CP_TASK_CHECKED));
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
        draw_box_hrule(win, 3, win_w);

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
            const char *badge = (engine == EXPORT_ENGINE_PANDOC && has_pandoc) ? "[pandoc]" : "[built-in]";
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
            draw_box_hrule(win, path_sep_y, win_w);
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

            if (path_focused && !confirm_overwrite) {
                curs_set(1);
                int cx = box_x + utf8_visual_width(disp, strlen(disp));
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

        // Footer divider & hints / overwrite confirmation
        draw_box_hrule(win, foot_sep_y, win_w);

        if (win_w > 4) {
            if (confirm_overwrite) {
                const char *slash = strrchr(path_buf, '/');
                const char *fname = slash ? (slash + 1) : path_buf;
                char warn[300];
                snprintf(warn, sizeof(warn),
                         " WARNING: '%s' exists! Overwrite? (Y)es / (N)o ", fname);
                wattron(win, COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD | A_REVERSE);
                for (int x = 1; x < win_w - 1; ++x) mvwaddch(win, win_h - 1, x, ' ');
                mvwaddnstr(win, win_h - 1, 2, warn, win_w - 4);
                wattroff(win, COLOR_PAIR(CP_STATUS_BAR_DIRTY) | A_BOLD | A_REVERSE);
            } else {
                const char *hints = has_pandoc
                    ? " [Enter] Export  [1-4] Format  [Tab] Path  [L] Links  [E] Engine  [Esc] Cancel "
                    : " [Enter] Export  [1-4] Format  [Tab] Path  [L] Links  [Esc/^C] Cancel ";
                wattron(win, COLOR_PAIR(CP_MARKER) | A_DIM);
                mvwaddnstr(win, win_h - 1, 2, hints, win_w - 4);
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
            calc_export_win_geom(rows, cols, &win_h, &win_w, &win_y, &win_x);
            win = newwin(win_h, win_w, win_y, win_x);
            if (!win) return false;
            keypad(win, TRUE);
            wbkgd(win, COLOR_PAIR(CP_HELP_BOX));
            continue;
        }

        if (confirm_overwrite) {
            if (ch == 'y' || ch == 'Y') {
                confirm_overwrite = false;
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
            } else if (ch == 'n' || ch == 'N' || ch == 27 ||
                       ch == CTRL_KEY('c') || ch == CTRL_KEY('g') ||
                       ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                confirm_overwrite = false;
            }
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
            bool is_dir = false;
            if (ui_path_exists(path_buf, &is_dir)) {
                if (is_dir) {
                    snprintf(err_msg, sizeof(err_msg), "Cannot overwrite directory — specify a file path");
                    continue;
                }
                confirm_overwrite = true;
                continue;
            }
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
            } else {
                char seq[8];
                size_t slen = read_utf8_input(win, ch, seq, sizeof(seq));
                size_t plen = strlen(path_buf);
                if (slen > 0 && plen + slen < sizeof(path_buf)) {
                    memcpy(path_buf + plen, seq, slen);
                    path_buf[plen + slen] = '\0';
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
                engine = (engine == EXPORT_ENGINE_PANDOC) ? EXPORT_ENGINE_AUTO : EXPORT_ENGINE_PANDOC;
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

