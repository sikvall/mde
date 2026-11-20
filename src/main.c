/**
 * @file main.c
 * @brief Application entry point, CLI arguments, and main event loop.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _XOPEN_SOURCE_EXTENDED 1
#include "editor.h"
#include "ui.h"
#include "utf8.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>

/**
 * @brief Macro to compute ASCII control key code.
 */
#define CTRL_KEY(k) ((k) & 0x1f)

static Editor g_editor;

/**
 * @brief Signal handler for program termination (SIGINT, SIGTERM).
 */
static void handle_sigint(int sig) {
    (void)sig;
    ui_shutdown();
    exit(0);
}

/**
 * @brief Print command-line usage information.
 */
static void print_usage(const char *prog) {
    printf("%s - %s\n\n", MDE_APP_NAME, MDE_DESCRIPTION);
    printf("Usage: %s [options] [filename.md]\n\n", prog);
    printf("Options:\n");
    printf("  -h, --help         Display this help message and exit\n");
    printf("  -v, --version      Output version and build information and exit\n");
    printf("  -w, --wrap <cols>  Set automatic line break (20-255, or off; default %d)\n\n", MDE_WRAP_COL_DEFAULT);
    printf("Keybindings (Nano-compatible):\n");
    printf("  Ctrl+G / F1      Get Help overlay (Cheat sheet & Markdown guide)\n");
    printf("  Ctrl+X           Exit editor (prompts if unsaved)\n");
    printf("  Ctrl+S           Save current file\n");
    printf("  Ctrl+O           Write Out (Save As file dialogue)\n");
    printf("  Ctrl+R           Read / Open file dialogue\n");
    printf("  Ctrl+W / Ctrl+Q  Where Is (Search forward / backward)\n");
    printf("  Alt+W / F3       Find next match (Alt+Q for previous)\n");
    printf("  Ctrl+K / Ctrl+U  Cut (selection or line) / Paste (Uncut)\n");
    printf("  Alt+6            Copy (selection or line)\n");
    printf("  Alt+A / Ctrl+6   Set or unset selection mark (also Ctrl+Space)\n");
    printf("  Alt+U / Alt+E    Undo / Redo\n");
    printf("  Ctrl+C           Display cursor location (Cancel in prompts)\n");
    printf("  Ctrl+/ / Alt+G   Go to line (and optional column)\n");
    printf("  Ctrl+Y / Ctrl+V  Previous Page / Next Page\n");
    printf("  Ctrl+A / Ctrl+E  Beginning / End of line\n");
    printf("  Ctrl+B/F/P/N     Move Left / Right / Up / Down\n");
    printf("  Ctrl+H / Ctrl+D  Backspace / Delete character\n");
    printf("  Ctrl+L           Refresh screen\n");
    printf("  F2 / Alt+P       Cycle View Mode (Split / In-place / Preview)\n");
    printf("  F4 / Alt+L       Set automatic line break column (20-255 or off)\n");
    printf("  F5 / Alt+X       Open Export Menu (HTML, XML, EPUB, LaTeX; includes linked files)\n");
    printf("  F6 / Alt+H       Insert hypertext file link [Label](file.md)\n");
    printf("  F7 / Alt+O / ^]  Follow file or #heading link under cursor\n");
    printf("  F8 / Alt+-       Navigate back to previous linked file\n");
    printf("  Ctrl+T           Toggle checkbox [- [ ] <-> - [x]]\n");
    printf("  Alt+B            Insert bold markers (**) or wrap selection\n");
    printf("  Enter            Smart list / task continuation\n");
    printf("  Tab / Shift+Tab  Indent / Dedent (also Alt+} / Alt+{)\n");
    printf("  Ins              Toggle Insert / Overwrite mode\n");
}

/**
 * @brief Confirm saving/discarding unsaved changes before switching files.
 * @return true if safe to proceed, false if user cancelled.
 */
static bool confirm_switch_file(void) {
    if (!g_editor.buffer.is_dirty) return true;
    int action = ui_confirm_unsaved(&g_editor, "switching file");
    if (action == 1) {
        if (!g_editor.buffer.file_path) {
            char save_path[PATH_MAX];
            if (ui_file_dialog(&g_editor, FILE_DIALOG_SAVE_AS, save_path, sizeof(save_path))) {
                return editor_save(&g_editor, save_path);
            }
            return false;
        }
        return editor_save(&g_editor, NULL);
    }
    return (action == 0);
}

/**
 * @brief Open file picker to insert a relative Markdown hyperlink at cursor.
 */
static void handle_insert_file_link(void) {
    char link_path[PATH_MAX];
    if (ui_file_dialog(&g_editor, FILE_DIALOG_LINK, link_path, sizeof(link_path))) {
        editor_insert_file_link(&g_editor, link_path);
    }
}

/**
 * @brief Follow a Markdown file or #heading link under the cursor.
 */
static void handle_follow_link(void) {
    char url[PATH_MAX] = {0};
    char anchor[128] = {0};
    if (!editor_extract_link_at_cursor(&g_editor, url, sizeof(url), anchor, sizeof(anchor))) {
        editor_set_status(&g_editor, "No link found on current line");
        return;
    }
    // Only prompt about unsaved changes when switching to a different local file
    if (url[0] && strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0 &&
        strncmp(url, "mailto:", 7) != 0 && strncmp(url, "ftp://", 6) != 0) {
        if (!confirm_switch_file()) return;
    }
    editor_follow_link(&g_editor);
}

/**
 * @brief Navigate back to the previously visited linked file.
 */
static void handle_navigate_back(void) {
    if (g_editor.link_history_count == 0) {
        editor_set_status(&g_editor, "No previous file in link history");
        return;
    }
    if (!confirm_switch_file()) return;
    editor_navigate_back(&g_editor);
}

/**
 * @brief Prompt the user to configure automatic line break column width.
 */
static void prompt_wrap_col(void) {
    char prompt_label[80];
    if (g_editor.wrap_col > 0) {
        snprintf(prompt_label, sizeof(prompt_label),
                 "Auto line break (20-255 or off, now %d)", g_editor.wrap_col);
    } else {
        snprintf(prompt_label, sizeof(prompt_label),
                 "Auto line break (20-255 or off, now OFF)");
    }
    char wrap_input[32];
    if (ui_prompt(&g_editor, prompt_label, wrap_input, sizeof(wrap_input))) {
        int new_wrap = 0;
        if (editor_parse_wrap_col(wrap_input, &new_wrap)) {
            editor_set_wrap_col(&g_editor, new_wrap);
        } else {
            editor_set_status(&g_editor, "Invalid wrap column: use 20-255 or off");
        }
    }
}

/**
 * @brief Prompt the user for a target line number (and optional column).
 */
static void prompt_goto_line(void) {
    char goto_buf[64];
    if (ui_prompt(&g_editor, "Enter line number[, column]", goto_buf, sizeof(goto_buf))) {
        char *p = goto_buf;
        while (*p == ' ' || *p == '\t') p++;
        char *end1 = NULL;
        unsigned long ln = strtoul(p, &end1, 10);
        if (end1 != p && ln > 0) {
            unsigned long col = 1;
            while (*end1 == ' ' || *end1 == '\t' || *end1 == ',' || *end1 == ':') end1++;
            if (*end1 != '\0') {
                char *end2 = NULL;
                unsigned long c = strtoul(end1, &end2, 10);
                if (end2 != end1 && c > 0) col = c;
            }
            editor_goto_line(&g_editor, (size_t)ln, (size_t)col);
        } else {
            editor_set_status(&g_editor, "Invalid line number");
        }
    }
}

/**
 * @brief Print all application metadata, version, and build information from src/version.h.
 */
static void print_version(void) {
    printf("%s (%s) version %s (%d.%d.%d)\n",
           MDE_APP_NAME, MDE_APP_TITLE, MDE_VERSION_STRING,
           MDE_VERSION_MAJOR, MDE_VERSION_MINOR, MDE_VERSION_PATCH);
    printf("Description: %s\n", MDE_DESCRIPTION);
    printf("Built:       %s %s\n", MDE_BUILD_DATE, MDE_BUILD_TIME);
    printf("Author:      %s\n", MDE_AUTHOR);
    printf("Maintainer:  %s\n", MDE_MAINTAINER);
}

int main(int argc, char *argv[]) {
    const char *file_to_open = NULL;
    bool has_cli_wrap = false;
    int cli_wrap_col = MDE_WRAP_COL_DEFAULT;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--wrap") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: %s requires an argument (20-255 or off)\n", argv[i]);
                return 1;
            }
            if (!editor_parse_wrap_col(argv[++i], &cli_wrap_col)) {
                fprintf(stderr, "Error: invalid wrap column '%s' (expected 20-255 or off)\n", argv[i]);
                return 1;
            }
            has_cli_wrap = true;
        } else if (strncmp(argv[i], "--wrap=", 7) == 0) {
            if (!editor_parse_wrap_col(argv[i] + 7, &cli_wrap_col)) {
                fprintf(stderr, "Error: invalid wrap column '%s' (expected 20-255 or off)\n", argv[i] + 7);
                return 1;
            }
            has_cli_wrap = true;
        } else if (argv[i][0] != '-') {
            file_to_open = argv[i];
        }
    }

    // Set up signal handling (leave SIGWINCH to ncurses for async-signal-safe KEY_RESIZE)
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    // Initialize UI and Editor state
    ui_init();
    editor_init(&g_editor, file_to_open);
    if (has_cli_wrap) {
        editor_set_wrap_col(&g_editor, cli_wrap_col);
    }

    char search_buf[128] = {0};
    bool last_search_forward = true;

    // Main event loop
    while (!g_editor.quit_requested) {
        ui_draw(&g_editor);

        int ch = getch();
        if (ch == ERR) continue;

        editor_touch_key(&g_editor);

        // Terminal resize event (handled safely via ncurses KEY_RESIZE)
        if (ch == KEY_RESIZE) {
            g_editor.rendered_dirty = true;
            erase();
            continue;
        }

        // Mouse events (clicks and wheel scrolls)
        if (ch == KEY_MOUSE) {
            MEVENT mevent;
            if (getmouse(&mevent) == OK) {
                if (mevent.bstate & BUTTON4_PRESSED) { // Wheel Up
                    editor_move_up(&g_editor);
                    editor_move_up(&g_editor);
                } else if (mevent.bstate & BUTTON5_PRESSED) { // Wheel Down
                    editor_move_down(&g_editor);
                    editor_move_down(&g_editor);
                } else if (mevent.bstate & BUTTON1_CLICKED) {
                    // Left click to position cursor
                    int click_y = mevent.y;
                    int click_x = mevent.x;
                    int num_hints = ui_get_shortcut_hint_rows(g_editor.screen_rows);
                    int text_max_y = g_editor.screen_rows - num_hints - 1;
                    // Click is below the header/ruler (row 0) and above the status bar
                    if (click_y >= 1 && click_y < text_max_y) {
                        if (g_editor.view_mode == VIEW_PREVIEW) {
                            size_t rend_idx = g_editor.preview_scroll_row + (size_t)(click_y - 1);
                            if (rend_idx < g_editor.rendered_doc.count) {
                                size_t target_row = g_editor.rendered_doc.lines[rend_idx].source_row;
                                if (target_row < buffer_line_count(&g_editor.buffer)) {
                                    g_editor.cursor_row = target_row;
                                    const char *l = buffer_line(&g_editor.buffer, target_row);
                                    int target_vcol = click_x >= 2 ? (click_x - 2) : 0;
                                    g_editor.cursor_col = utf8_col_to_byte(l, target_vcol);
                                    g_editor.target_visual_col = utf8_byte_to_col(l, g_editor.cursor_col);
                                }
                            }
                        } else {
                            size_t target_row = g_editor.scroll_row + (size_t)(click_y - 1);
                            if (target_row < buffer_line_count(&g_editor.buffer)) {
                                g_editor.cursor_row = target_row;
                                const char *l = buffer_line(&g_editor.buffer, target_row);
                                int gutter = 7;
                                int target_vcol = (click_x >= gutter) ? (click_x - gutter + (int)g_editor.scroll_col) : 0;
                                g_editor.cursor_col = utf8_col_to_byte(l, target_vcol);
                                g_editor.target_visual_col = utf8_byte_to_col(l, g_editor.cursor_col);
                            }
                        }
                    }
                }
            }
            continue;
        }

        // Escape / Meta (Alt+key) sequences (Nano-compatible)
        if (ch == 27) {
            nodelay(stdscr, TRUE);
            int next = getch();
            nodelay(stdscr, FALSE);
            if (next != ERR) {
                switch (next) {
                    case 'u':
                    case 'U':
                        editor_undo(&g_editor);
                        break;
                    case 'e':
                    case 'E':
                        editor_redo(&g_editor);
                        break;
                    case 'a':
                    case 'A':
                        editor_toggle_mark(&g_editor);
                        break;
                    case '6':
                        if (editor_has_selection(&g_editor)) {
                            editor_copy(&g_editor);
                        } else {
                            editor_copy_line(&g_editor);
                        }
                        break;
                    case 'w':
                    case 'W':
                        if (search_buf[0]) {
                            last_search_forward = true;
                            editor_search_find_next(&g_editor, search_buf, true);
                        } else {
                            editor_set_status(&g_editor, "No previous search string");
                        }
                        break;
                    case 'q':
                    case 'Q':
                        if (search_buf[0]) {
                            last_search_forward = false;
                            editor_search_find_next(&g_editor, search_buf, false);
                        } else {
                            editor_set_status(&g_editor, "No previous search string");
                        }
                        break;
                    case 'g':
                    case 'G':
                        prompt_goto_line();
                        break;
                    case '\\':
                        editor_move_doc_start(&g_editor);
                        break;
                    case '/':
                        editor_move_doc_end(&g_editor);
                        break;
                    case '}':
                        editor_indent(&g_editor);
                        break;
                    case '{':
                        editor_unindent(&g_editor);
                        break;
                    case 'p':
                    case 'P':
                        editor_cycle_view_mode(&g_editor);
                        break;
                    case 'l':
                    case 'L':
                        prompt_wrap_col();
                        break;
                    case 'b':
                    case 'B':
                        editor_wrap_format(&g_editor, "**");
                        break;
                    case 'x':
                    case 'X':
                        ui_export_menu(&g_editor);
                        break;
                    case 'h':
                    case 'H':
                        handle_insert_file_link();
                        break;
                    case 'o':
                    case 'O':
                        handle_follow_link();
                        break;
                    case '-':
                        handle_navigate_back();
                        break;
                    default:
                        break;
                }
            } else {
                // Standalone Escape: clear active selection mark
                if (g_editor.mark_active) {
                    editor_clear_mark(&g_editor);
                }
            }
            continue;
        }

        // Key bindings dispatch (Nano-compatible)
        if (ch == CTRL_KEY('x')) {
            // Exit requested (Nano ^X)
            int action = ui_confirm_quit(&g_editor);
            if (action == 1) { // Save and quit
                if (!g_editor.buffer.file_path) {
                    char save_path[PATH_MAX];
                    if (ui_file_dialog(&g_editor, FILE_DIALOG_SAVE_AS, save_path, sizeof(save_path))) {
                        editor_save(&g_editor, save_path);
                        g_editor.quit_requested = true;
                    }
                } else {
                    editor_save(&g_editor, NULL);
                    g_editor.quit_requested = true;
                }
            } else if (action == 0) { // Discard and quit
                g_editor.quit_requested = true;
            }
        } else if (ch == CTRL_KEY('s')) {
            // Save file directly (Nano ^S)
            if (!g_editor.buffer.file_path) {
                char save_path[PATH_MAX];
                if (ui_file_dialog(&g_editor, FILE_DIALOG_SAVE_AS, save_path, sizeof(save_path))) {
                    editor_save(&g_editor, save_path);
                }
            } else {
                editor_save(&g_editor, NULL);
            }
        } else if (ch == CTRL_KEY('o')) {
            // Write Out / Save As file dialog (Nano ^O)
            char save_path[PATH_MAX];
            if (ui_file_dialog(&g_editor, FILE_DIALOG_SAVE_AS, save_path, sizeof(save_path))) {
                editor_save(&g_editor, save_path);
            }
        } else if (ch == CTRL_KEY('r')) {
            // Read / Open file dialog (Nano ^R)
            char open_path[PATH_MAX];
            if (ui_file_dialog(&g_editor, FILE_DIALOG_OPEN, open_path, sizeof(open_path))) {
                editor_open(&g_editor, open_path);
            }
        } else if (ch == CTRL_KEY('g') || ch == KEY_F(1)) {
            // Get Help overlay (Nano ^G / F1)
            ui_show_help(&g_editor);
        } else if (ch == CTRL_KEY('w')) {
            // Where Is: Search forward (Nano ^W)
            if (ui_prompt(&g_editor, "Search", search_buf, sizeof(search_buf))) {
                last_search_forward = true;
                editor_search_find_next(&g_editor, search_buf, true);
            } else {
                g_editor.search_active = false;
            }
        } else if (ch == CTRL_KEY('q')) {
            // Where Was: Search backward (Nano ^Q)
            if (ui_prompt(&g_editor, "Search Backward", search_buf, sizeof(search_buf))) {
                last_search_forward = false;
                editor_search_find_next(&g_editor, search_buf, false);
            } else {
                g_editor.search_active = false;
            }
        } else if (ch == KEY_F(3)) {
            if (search_buf[0]) {
                editor_search_find_next(&g_editor, search_buf, last_search_forward);
            }
        } else if (ch == CTRL_KEY('k')) {
            // Cut selection if mark active, otherwise cut entire current line (Nano ^K)
            if (editor_has_selection(&g_editor)) {
                editor_cut(&g_editor);
            } else {
                editor_cut_line(&g_editor);
            }
        } else if (ch == CTRL_KEY('u')) {
            // Paste / Uncut from clipboard (Nano ^U)
            editor_paste(&g_editor);
        } else if (ch == CTRL_KEY('c')) {
            // Report cursor location (Nano ^C)
            editor_report_position(&g_editor);
        } else if (ch == CTRL_KEY('l')) {
            // Refresh / redraw screen (Nano ^L)
            g_editor.rendered_dirty = true;
            clearok(stdscr, TRUE);
            erase();
        } else if (ch == CTRL_KEY('_')) {
            // Go to line (Nano ^_ / ^/)
            prompt_goto_line();
        } else if (ch == 0 || ch == CTRL_KEY('^')) {
            // Set / unset selection mark (Nano ^^ / Ctrl+Space)
            editor_toggle_mark(&g_editor);
        } else if (ch == CTRL_KEY('t')) {
            // Toggle checkbox [- [ ] <-> - [x]]
            editor_toggle_checkbox(&g_editor);
        } else if (ch == KEY_F(2)) {
            // Cycle view mode: Split <-> In-place <-> Preview
            editor_cycle_view_mode(&g_editor);
        } else if (ch == KEY_F(4)) {
            // Configure automatic line break column (20-255 or off)
            prompt_wrap_col();
        } else if (ch == KEY_F(5)) {
            // Export menu (HTML, XML, EPUB, LaTeX; includes linked files)
            ui_export_menu(&g_editor);
        } else if (ch == KEY_F(6)) {
            // Insert hypertext file link [Label](file.md)
            handle_insert_file_link();
        } else if (ch == KEY_F(7) || ch == CTRL_KEY(']')) {
            // Follow hypertext file or #heading link under cursor
            handle_follow_link();
        } else if (ch == KEY_F(8)) {
            // Navigate back to previous linked file
            handle_navigate_back();
        } else if (ch == KEY_LEFT || ch == CTRL_KEY('b')) {
            editor_move_left(&g_editor);
        } else if (ch == KEY_RIGHT || ch == CTRL_KEY('f')) {
            editor_move_right(&g_editor);
        } else if (ch == KEY_UP || ch == CTRL_KEY('p')) {
            editor_move_up(&g_editor);
        } else if (ch == KEY_DOWN || ch == CTRL_KEY('n')) {
            editor_move_down(&g_editor);
        } else if (ch == KEY_HOME || ch == CTRL_KEY('a')) {
            editor_move_home(&g_editor);
        } else if (ch == KEY_END || ch == CTRL_KEY('e')) {
            editor_move_end(&g_editor);
        } else if (ch == KEY_PPAGE || ch == CTRL_KEY('y')) {
            editor_move_page_up(&g_editor);
        } else if (ch == KEY_NPAGE || ch == CTRL_KEY('v')) {
            editor_move_page_down(&g_editor);
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == CTRL_KEY('h')) {
            editor_backspace(&g_editor);
        } else if (ch == KEY_DC || ch == CTRL_KEY('d')) {
            editor_delete(&g_editor);
        } else if (ch == '\n' || ch == '\r') {
            editor_smart_enter(&g_editor);
        } else if (ch == '\t') {
            editor_indent(&g_editor);
        } else if (ch == KEY_BTAB) {
            editor_unindent(&g_editor);
        } else if (ch == KEY_IC
#ifdef KEY_SIC
                   || ch == KEY_SIC
#endif
                   ) {
            editor_toggle_insert_mode(&g_editor);
        } else if (ch >= 0xC0 && ch <= 0xFF) {
            // Multi-byte UTF-8 character leading byte
            char utf8_buf[8] = {0};
            utf8_buf[0] = (char)ch;
            size_t ulen = utf8_char_len((unsigned char)ch);
            for (size_t b = 1; b < ulen; ++b) {
                int next_b = getch();
                if (next_b == ERR) break;
                utf8_buf[b] = (char)next_b;
            }
            editor_insert_char(&g_editor, utf8_buf, ulen);
        } else if (ch >= 32 && ch <= 126) {
            // Standard printable ASCII character
            char c = (char)ch;
            editor_insert_char(&g_editor, &c, 1);
        }
    }

    editor_free(&g_editor);
    ui_shutdown();
    return 0;
}
