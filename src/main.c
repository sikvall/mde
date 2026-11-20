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
    printf("Keybindings:\n");
    printf("  Ctrl+S           Save file\n");
    printf("  Ctrl+O           Open file\n");
    printf("  Ctrl+Q           Quit\n");
    printf("  Ctrl+P / F2      Toggle View Mode (Split / In-place / Preview)\n");
    printf("  Ctrl+L / F4      Set automatic line break column (20-255 or off)\n");
    printf("  F5 / Ctrl+D      Open Export Menu (HTML, XML, EPUB, LaTeX)\n");
    printf("  Ctrl+T           Toggle checkbox [- [ ] <-> - [x]]\n");
    printf("  Ctrl+B           Insert bold markers (**)\n");
    printf("  Ctrl+K           Insert inline code markers (`)\n");
    printf("  Ctrl+Z           Undo\n");
    printf("  Ctrl+Y           Redo\n");
    printf("  Ctrl+F           Find / Search text\n");
    printf("  F1 / Ctrl+H      Help overlay (Cheat sheet & Markdown guide)\n");
    printf("  Enter            Smart list / task continuation\n");
    printf("  Tab / Shift+Tab  Indent / Dedent\n");
    printf("  Ins              Toggle Insert / Overwrite mode\n");
}

/**
 * @brief Print application version and build date.
 */
static void print_version(void) {
    printf("%s version %s (built: %s %s)\n",
           MDE_APP_NAME, MDE_VERSION_STRING, MDE_BUILD_DATE, MDE_BUILD_TIME);
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

        // Key bindings dispatch
        if (ch == CTRL_KEY('q')) {
            // Quit requested
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
            // Save file
            if (!g_editor.buffer.file_path) {
                char save_path[PATH_MAX];
                if (ui_file_dialog(&g_editor, FILE_DIALOG_SAVE_AS, save_path, sizeof(save_path))) {
                    editor_save(&g_editor, save_path);
                }
            } else {
                editor_save(&g_editor, NULL);
            }
        } else if (ch == CTRL_KEY('o')) {
            // Open file dialog
            char open_path[PATH_MAX];
            if (ui_file_dialog(&g_editor, FILE_DIALOG_OPEN, open_path, sizeof(open_path))) {
                editor_open(&g_editor, open_path);
            }
        } else if (ch == CTRL_KEY('w')) {
            // Save As file dialog
            char save_path[PATH_MAX];
            if (ui_file_dialog(&g_editor, FILE_DIALOG_SAVE_AS, save_path, sizeof(save_path))) {
                editor_save(&g_editor, save_path);
            }
        } else if (ch == CTRL_KEY('p') || ch == KEY_F(2)) {
            // Toggle view mode: Split <-> In-place <-> Preview
            editor_cycle_view_mode(&g_editor);
        } else if (ch == CTRL_KEY('l') || ch == KEY_F(4)) {
            // Configure automatic line break column (20-255 or off)
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
        } else if (ch == KEY_F(5) || ch == CTRL_KEY('d')) {
            // Export menu (HTML, XML, EPUB, LaTeX)
            ui_export_menu(&g_editor);
        } else if (ch == KEY_F(1) || ch == CTRL_KEY('h')) {
            // Help overlay (Cheat sheet & Markdown guide)
            ui_show_help(&g_editor);
        } else if (ch == 0 || ch == CTRL_KEY(' ')) {
            // Set selection mark start
            editor_set_mark(&g_editor);
        } else if (ch == 27 || ch == CTRL_KEY('g')) {
            // Escape / Cancel: clear selection mark
            if (g_editor.mark_active) {
                editor_clear_mark(&g_editor);
            }
        } else if (ch == CTRL_KEY('x')) {
            // Cut selection
            editor_cut(&g_editor);
        } else if (ch == CTRL_KEY('c')) {
            // Copy selection
            editor_copy(&g_editor);
        } else if (ch == CTRL_KEY('v')) {
            // Paste
            editor_paste(&g_editor);
        } else if (ch == CTRL_KEY('k')) {
            // Kill to end of line / kill newline
            editor_kill_to_end_of_line(&g_editor);
        } else if (ch == CTRL_KEY('z')) {
            // Undo
            editor_undo(&g_editor);
        } else if (ch == CTRL_KEY('y')) {
            // Redo
            editor_redo(&g_editor);
        } else if (ch == CTRL_KEY('t')) {
            // Toggle checkbox [- [ ] <-> - [x]]
            editor_toggle_checkbox(&g_editor);
        } else if (ch == CTRL_KEY('b')) {
            // Wrap bold
            editor_wrap_format(&g_editor, "**");
        } else if (ch == CTRL_KEY('f')) {
            // Find in file
            if (ui_prompt(&g_editor, "Find", search_buf, sizeof(search_buf))) {
                editor_search_find_next(&g_editor, search_buf, true);
            } else {
                g_editor.search_active = false;
            }
        } else if (ch == KEY_F(3)) {
            if (search_buf[0]) {
                editor_search_find_next(&g_editor, search_buf, true);
            }
        } else if (ch == KEY_LEFT) {
            editor_move_left(&g_editor);
        } else if (ch == KEY_RIGHT) {
            editor_move_right(&g_editor);
        } else if (ch == KEY_UP) {
            editor_move_up(&g_editor);
        } else if (ch == KEY_DOWN) {
            editor_move_down(&g_editor);
        } else if (ch == KEY_HOME || ch == CTRL_KEY('a')) {
            editor_move_home(&g_editor);
        } else if (ch == KEY_END || ch == CTRL_KEY('e')) {
            editor_move_end(&g_editor);
        } else if (ch == KEY_PPAGE) {
            editor_move_page_up(&g_editor);
        } else if (ch == KEY_NPAGE) {
            editor_move_page_down(&g_editor);
        } else if (ch == KEY_BACKSPACE || ch == 127) {
            editor_backspace(&g_editor);
        } else if (ch == KEY_DC) {
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
        } else if ((unsigned char)ch >= 0xC0) {
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
