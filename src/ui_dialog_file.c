/**
 * @file ui_dialog_file.c
 * @brief Interactive modal file browser dialogue for Open, Save As, and Link.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "ui_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

/** @brief Test whether path exists on disk and report whether it is a directory. */
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

/** @brief Display a bottom-bar overwrite confirmation prompt requiring explicit Y/N input. */
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

/** @brief qsort comparator ordering ".." first, then directories, then files alphabetically. */
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

/** @brief Read and sort directory entries from dir_path into a heap-allocated FileItem array. */
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
        snprintf(items[0].name, sizeof(items[0].name), "..");
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

        snprintf(items[*out_count].name, sizeof(items[*out_count].name), "%s", ent->d_name);
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

/** @brief Trim the last path component from dir_buf to ascend to its parent directory. */
static void navigate_to_parent(char *dir_buf, size_t buf_size) {
    if (!dir_buf || buf_size == 0) return;
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
        snprintf(dir_buf, buf_size, "/");
    }
}

/** @brief Append subdirectory sub to dir_buf and canonicalize via realpath when possible. */
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

/**
 * @brief Compute centered geometry for the interactive file browser modal.
 */
void calc_file_dialog_geom(int rows, int cols,
                           int *out_h, int *out_w, int *out_y, int *out_x) {
    int w = 72;
    if (w > cols - 4) w = cols - 4;
    if (w < 44) w = 44;
    if (w > cols) w = cols;
    if (w < 10) w = cols > 0 ? cols : 10;

    int h = 22;
    if (h > rows - 2) h = rows - 2;
    if (h < 12) h = 12;
    if (h > rows) h = rows;
    if (h < 6) h = rows > 0 ? rows : 6;

    int y = (rows - h) / 2;
    int x = (cols - w) / 2;
    if (y < 0) y = 0;
    if (x < 0) x = 0;

    *out_h = h;
    *out_w = w;
    *out_y = y;
    *out_x = x;
}

/**
 * @brief Format a file byte size as human-readable B, KB, or MB.
 */
static void format_file_size(size_t size, char *buf, size_t buf_len) {
    if (size < 1024) {
        snprintf(buf, buf_len, "%zu B", size);
    } else if (size < 1024 * 1024) {
        snprintf(buf, buf_len, "%.1f KB", (double)size / 1024.0);
    } else {
        snprintf(buf, buf_len, "%.1f MB", (double)size / (1024.0 * 1024.0));
    }
}

/** @brief Run the interactive modal file browser for Open, Write Out (with overwrite guard), or Link. */
bool ui_file_dialog(Editor *ed, FileDialogMode mode, char *out_path, size_t max_len) {
    if (!ed || !out_path || max_len == 0) return false;

    char current_dir[PATH_MAX];
    if (ed->buffer.file_path && ed->buffer.file_path[0]) {
        char dir_copy[PATH_MAX];
        snprintf(dir_copy, sizeof(dir_copy), "%s", ed->buffer.file_path);
        char *d = dirname(dir_copy);
        if (!realpath(d, current_dir)) {
            if (!getcwd(current_dir, sizeof(current_dir))) {
                snprintf(current_dir, sizeof(current_dir), ".");
            }
        }
    } else {
        if (!getcwd(current_dir, sizeof(current_dir))) {
            snprintf(current_dir, sizeof(current_dir), ".");
        }
    }

    char input_buf[256] = {0};
    if (mode == FILE_DIALOG_SAVE_AS) {
        if (ed->buffer.file_path && ed->buffer.file_path[0]) {
            char file_copy[PATH_MAX];
            snprintf(file_copy, sizeof(file_copy), "%s", ed->buffer.file_path);
            char *b = basename(file_copy);
            snprintf(input_buf, sizeof(input_buf), "%s", b);
        } else {
            snprintf(input_buf, sizeof(input_buf), "untitled.md");
        }
    }

    size_t count = 0;
    FileItem *items = load_directory_items(current_dir, &count);
    int selected_idx = 0;
    int scroll_offset = 0;

    bool has_input_box = (mode == FILE_DIALOG_SAVE_AS || mode == FILE_DIALOG_LINK);
    enum { FOCUS_LIST, FOCUS_INPUT } focus = (mode == FILE_DIALOG_SAVE_AS) ? FOCUS_INPUT : FOCUS_LIST;

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

    bool confirmed = false;
    bool confirm_overwrite = false;
    char pending_path[PATH_MAX * 2] = {0};
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
                int tail = max_dir_disp - 3;
                if (tail > (int)sizeof(short_dir) - 4) tail = (int)sizeof(short_dir) - 4;
                snprintf(short_dir, sizeof(short_dir), "...%s", current_dir + dlen - (size_t)tail);
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
        draw_box_hrule(win, 2, win_w);

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
                    if (win_w > 18) mvwaddnstr(win, y, 4, dname, win_w - 18);
                    wattroff(win, COLOR_PAIR(CP_HEADER2) | A_BOLD);

                    // Right column: <DIR>
                    if (win_w > 9) mvwaddstr(win, y, win_w - 8, "<DIR>");
                } else {
                    wattron(win, COLOR_PAIR(CP_DEFAULT));
                    if (win_w > 18) mvwaddnstr(win, y, 4, fi->name, win_w - 18);
                    wattroff(win, COLOR_PAIR(CP_DEFAULT));

                    // Right column: File size
                    char sz_buf[32];
                    format_file_size(fi->size, sz_buf, sizeof(sz_buf));
                    int sz_len = (int)strlen(sz_buf);
                    if (win_w - sz_len - 3 > 4) mvwaddstr(win, y, win_w - sz_len - 3, sz_buf);
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

            draw_box_hrule(win, input_sep_y, win_w);

            bool is_input_focused = (focus == FOCUS_INPUT);
            wattron(win, is_input_focused ? (COLOR_PAIR(CP_DEFAULT) | A_BOLD) : COLOR_PAIR(CP_MARKER));
            mvwaddstr(win, input_y, 2, mode == FILE_DIALOG_LINK ? "Target:    " : "File name: ");
            wattroff(win, is_input_focused ? (COLOR_PAIR(CP_DEFAULT) | A_BOLD) : COLOR_PAIR(CP_MARKER));

            // Input field box
            int box_start_x = 13;
            int box_width = win_w - 16;
            if (box_width > 1) {
                wattron(win, COLOR_PAIR(CP_DEFAULT) | (is_input_focused ? A_REVERSE : 0));
                for (int x = 0; x < box_width; ++x) mvwaddch(win, input_y, box_start_x + x, ' ');
                mvwaddnstr(win, input_y, box_start_x, input_buf, box_width - 1);
                wattroff(win, COLOR_PAIR(CP_DEFAULT) | (is_input_focused ? A_REVERSE : 0));

                if (is_input_focused && !confirm_overwrite) {
                    curs_set(1);
                    int cx = box_start_x + (int)strlen(input_buf);
                    if (cx >= box_start_x + box_width) cx = box_start_x + box_width - 1;
                    wmove(win, input_y, cx);
                } else {
                    curs_set(0);
                }
            } else {
                curs_set(0);
            }
        } else {
            curs_set(0);
        }

        // Bottom separator & help instructions / overwrite warning
        draw_box_hrule(win, win_h - 2, win_w);

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
            calc_file_dialog_geom(rows, cols, &win_h, &win_w, &win_y, &win_x);

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
                    char candidate[PATH_MAX * 2];
                    int cand_n;
                    if (input_buf[0] == '/') {
                        cand_n = snprintf(candidate, sizeof(candidate), "%s", input_buf);
                    } else if (strcmp(current_dir, "/") == 0) {
                        cand_n = snprintf(candidate, sizeof(candidate), "/%s", input_buf);
                    } else {
                        cand_n = snprintf(candidate, sizeof(candidate), "%s/%s", current_dir, input_buf);
                    }
                    if (cand_n < 0 || (size_t)cand_n >= max_len || (size_t)cand_n >= sizeof(pending_path)) {
                        snprintf(warn_msg, sizeof(warn_msg), " WARNING: Path exceeds maximum length ");
                        continue;
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
            } else {
                char seq[8];
                size_t slen = read_utf8_input(win, ch, seq, sizeof(seq));
                size_t ilen = strlen(input_buf);
                if (slen > 0 && ilen + slen < sizeof(input_buf)) {
                    memcpy(input_buf + ilen, seq, slen);
                    input_buf[ilen + slen] = '\0';
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
                        snprintf(sub, sizeof(sub), "%s", items[selected_idx].name);
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
                            snprintf(input_buf, sizeof(input_buf), "%s", items[selected_idx].name);
                            focus = FOCUS_INPUT;
                        }
                    }
                }
            } else if (ch >= 32 && ch <= 126) {
                // Quick search / jump to next entry starting with that char
                char target = (char)tolower(ch);
                size_t base = selected_idx >= 0 ? (size_t)selected_idx : 0;
                for (size_t i = 1; i <= count; ++i) {
                    size_t candidate = (base + i) % count;
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
