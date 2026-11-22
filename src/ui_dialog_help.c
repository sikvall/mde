/**
 * @file ui_dialog_help.c
 * @brief Modal help dialog showing keyboard shortcuts and Markdown cheatsheet.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "ui_internal.h"
#include "version.h"
#include <string.h>

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
    { HELP_LINE_ITEM, "Alt+D", "Document & selection statistics popover" },
    { HELP_LINE_ITEM, "Ctrl+G / F1", "Open / close this Help & Reference overlay" },
    { HELP_LINE_BLANK, NULL, NULL },
    { HELP_LINE_SECTION, "HYPERTEXT FILE LINKING & EXPORT", NULL },
    { HELP_LINE_ITEM, "F6 / Alt+H", "Insert hyperlink [Label](file.md) via file picker" },
    { HELP_LINE_ITEM, "F7 / Alt+O", "Follow Markdown file or #heading link under cursor" },
    { HELP_LINE_ITEM, "F8 / Alt+-", "Navigate back to previous linked file & position" },
    { HELP_LINE_ITEM, "F5 / Alt+X", "Export (includes linked .md files in HTML/EPUB/LaTeX/XML)" },
    { HELP_LINE_BLANK, NULL, NULL },
    { HELP_LINE_SECTION, "VIEW MODES & DISPLAY", NULL },
    { HELP_LINE_ITEM, "F2 / Alt+P", "Cycle view: Split -> In-Place -> Preview" },
    { HELP_LINE_ITEM, "F10 / F12 / M-K", "Toggle bottom shortcut hint rows (hide / show)" },
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
    { HELP_LINE_ITEM, "Alt+J", "Reflow (re-wrap) current paragraph to wrap_col" },
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
    { HELP_LINE_ITEM, "Ctrl+\\ / Alt+R", "Search and Replace (Yes / No / All)" },
    { HELP_LINE_ITEM, "Alt+W / F3", "Repeat search forward (Alt+Q for backward)" },
    { HELP_LINE_ITEM, "F9 / Alt+T", "Open Document Outline (jump to H1-H6 heading)" },
    { HELP_LINE_ITEM, "Ctrl+/ / Alt+G", "Go to line number (and optional column)" },
    { HELP_LINE_ITEM, "Ctrl+] / Alt+]", "Jump between matching brackets () [] {} and markers" },
    { HELP_LINE_ITEM, "Ctrl+C", "Display cursor location (Cancel in prompts)" },
    { HELP_LINE_ITEM, "Ctrl+A / Home", "Jump to beginning of current line" },
    { HELP_LINE_ITEM, "Ctrl+E / End", "Jump to end of current line" },
    { HELP_LINE_ITEM, "Ctrl+← / Ctrl+→", "Jump backward / forward by one word" },
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

/**
 * @brief Compute centered geometry for the two-tab Help overlay modal.
 */
static void calc_help_win_geom(int rows, int cols,
                               int *out_h, int *out_w, int *out_y, int *out_x) {
    int h = (rows >= 32) ? 28 : 26;
    int w = 76;
    if (h > rows - 2) h = rows - 2;
    if (w > cols - 2) w = cols - 2;
    if (h < 12) h = 12;
    if (w < 50) w = 50;
    if (h > rows) h = rows;
    if (w > cols) w = cols;
    if (h < 6) h = rows > 0 ? rows : 6;
    if (w < 16) w = cols > 0 ? cols : 16;

    int y = (rows - h) / 2;
    int x = (cols - w) / 2;
    if (y < 0) y = 0;
    if (x < 0) x = 0;

    *out_h = h;
    *out_w = w;
    *out_y = y;
    *out_x = x;
}

/** @brief Display the scrollable two-tab Help overlay (Editor Cheat Sheet & Markdown Guide). */
void ui_show_help(Editor *ed) {
    int rows, cols, box_h, box_w, start_y, start_x;
    getmaxyx(stdscr, rows, cols);
    calc_help_win_geom(rows, cols, &box_h, &box_w, &start_y, &start_x);

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
        draw_box_hrule(win, 3, box_w);

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
        draw_box_hrule(win, box_h - 3, box_w);

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
            calc_help_win_geom(rows, cols, &box_h, &box_w, &start_y, &start_x);

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
