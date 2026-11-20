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

/**
 * @brief Macro to compute ASCII control key code.
 */
#define CTRL_KEY(k) ((k) & 0x1f)

static Editor g_editor;

/**
 * @brief Signal handler for terminal window resize (SIGWINCH).
 */
static void handle_sigwinch(int sig) {
    (void)sig;
    endwin();
    refresh();
    clear();
    ui_draw(&g_editor);
}

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
    printf("  -h, --help       Display this help message and exit\n");
    printf("  -v, --version    Output version and build information and exit\n\n");
    printf("Keybindings:\n");
    printf("  Ctrl+S           Save file\n");
    printf("  Ctrl+O           Open file\n");
    printf("  Ctrl+Q           Quit\n");
    printf("  Ctrl+P / F2      Toggle View Mode (Split / In-place / Preview)\n");
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

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        } else if (argv[i][0] != '-') {
            file_to_open = argv[i];
        }
    }

    // Set up signal handling
    signal(SIGWINCH, handle_sigwinch);
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    // Initialize UI and Editor state
    ui_init();
    editor_init(&g_editor, file_to_open);

    char search_buf[128] = {0};

    // Main event loop
    while (!g_editor.quit_requested) {
        ui_draw(&g_editor);

        int ch = getch();
        if (ch == ERR) continue;

        editor_touch_key(&g_editor);

        // Terminal resize event
        if (ch == KEY_RESIZE) {
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
                    // Click is below the header/ruler (row 0) and above the status bar
                    if (click_y >= 1 && click_y < g_editor.screen_rows - 2) {
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
                    char save_path[256];
                    if (ui_prompt(&g_editor, "Save file as", save_path, sizeof(save_path))) {
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
                char save_path[256];
                if (ui_prompt(&g_editor, "Save file as", save_path, sizeof(save_path))) {
                    editor_save(&g_editor, save_path);
                }
            } else {
                editor_save(&g_editor, NULL);
            }
        } else if (ch == CTRL_KEY('o')) {
            // Open file
            char open_path[256];
            if (ui_prompt(&g_editor, "Open file", open_path, sizeof(open_path))) {
                editor_open(&g_editor, open_path);
            }
        } else if (ch == CTRL_KEY('p') || ch == KEY_F(2)) {
            // Toggle view mode: Split <-> In-place <-> Preview
            editor_cycle_view_mode(&g_editor);
        } else if (ch == KEY_F(1) || ch == CTRL_KEY('h')) {
            // Help overlay (Cheat sheet & Markdown guide)
            ui_show_help(&g_editor);
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
        } else if (ch == CTRL_KEY('k')) {
            // Wrap inline code
            editor_wrap_format(&g_editor, "`");
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
        } else if (ch == KEY_HOME) {
            editor_move_home(&g_editor);
        } else if (ch == KEY_END) {
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
