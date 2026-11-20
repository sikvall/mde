/**
 * @file test_runner.c
 * @brief Comprehensive test suite for mde text buffer, UTF-8, Markdown parser, and themes.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _XOPEN_SOURCE_EXTENDED 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <locale.h>

#include "../src/version.h"
#include "../src/utf8.h"
#include "../src/buffer.h"
#include "../src/markdown.h"
#include "../src/renderer.h"
#include "../src/theme.h"
#include "../src/editor.h"
#include "../src/ui.h"

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        printf("  [FAIL] %s (line %d)\n", msg, __LINE__); \
    } \
} while(0)

static void test_version(void) {
    printf("--- Testing Version & Build Metadata ---\n");
    TEST_ASSERT(strcmp(MDE_APP_NAME, "mde") == 0, "App name is 'mde'");
    TEST_ASSERT(strcmp(MDE_VERSION_STRING, "0.1.0") == 0, "Version string is '0.1.0'");
    TEST_ASSERT(strlen(MDE_BUILD_DATE) > 0, "Build date is non-empty");
}

static void test_utf8(void) {
    printf("--- Testing UTF-8 Subsystem ---\n");

    const char *ascii = "Hello, world!";
    TEST_ASSERT(utf8_visual_width(ascii, strlen(ascii)) == 13, "ASCII visual width");
    TEST_ASSERT(utf8_codepoint_count(ascii, strlen(ascii)) == 13, "ASCII codepoint count");

    // "Héllo" with é (2 bytes)
    const char *accent = "H\xC3\xA9llo";
    TEST_ASSERT(utf8_visual_width(accent, strlen(accent)) == 5, "Accent visual width");
    TEST_ASSERT(utf8_codepoint_count(accent, strlen(accent)) == 5, "Accent codepoint count");

    // Navigation
    size_t next = utf8_next_char(accent, 1, strlen(accent));
    TEST_ASSERT(next == 3, "Next char skips 2-byte UTF-8 char");
    size_t prev = utf8_prev_char(accent, 3);
    TEST_ASSERT(prev == 1, "Prev char skips backward over 2-byte UTF-8 char");

    // Col conversions
    TEST_ASSERT(utf8_byte_to_col(accent, 3) == 2, "Byte to col conversion");
    TEST_ASSERT(utf8_col_to_byte(accent, 2) == 3, "Col to byte conversion");
}

static void test_buffer_and_undo(void) {
    printf("--- Testing Text Buffer & Undo/Redo ---\n");

    Buffer buf;
    buffer_init(&buf);

    TEST_ASSERT(buffer_line_count(&buf) == 1, "Initial buffer has 1 empty line");
    TEST_ASSERT(strcmp(buffer_line(&buf, 0), "") == 0, "Initial line is empty string");

    // Insert text
    buffer_insert_string(&buf, 0, 0, "Hello Markdown", 14, 1);
    TEST_ASSERT(strcmp(buffer_line(&buf, 0), "Hello Markdown") == 0, "Insert string");
    TEST_ASSERT(buffer_word_count(&buf) == 2, "Word count is 2");

    // Split line (Enter key)
    buffer_split_line(&buf, 0, 5, 2); // Split after "Hello"
    TEST_ASSERT(buffer_line_count(&buf) == 2, "Line count is 2 after split");
    TEST_ASSERT(strcmp(buffer_line(&buf, 0), "Hello") == 0, "Line 0 after split is 'Hello'");
    TEST_ASSERT(strcmp(buffer_line(&buf, 1), " Markdown") == 0, "Line 1 after split is ' Markdown'");

    // Join lines
    buffer_join_lines(&buf, 0, 3);
    TEST_ASSERT(buffer_line_count(&buf) == 1, "Line count is 1 after join");
    TEST_ASSERT(strcmp(buffer_line(&buf, 0), "Hello Markdown") == 0, "Joined line restored");

    // Backspace
    size_t row = 0, col = 5;
    buffer_backspace(&buf, &row, &col, 4);
    TEST_ASSERT(col == 4, "Col decremented on backspace");
    TEST_ASSERT(strcmp(buffer_line(&buf, 0), "Hell Markdown") == 0, "Backspace deleted char 'o'");

    // Undo
    size_t u_row = 0, u_col = 0;
    bool undone = buffer_undo(&buf, &u_row, &u_col);
    TEST_ASSERT(undone, "Undo succeeded");
    TEST_ASSERT(strcmp(buffer_line(&buf, 0), "Hello Markdown") == 0, "Undo restored 'o'");

    // Redo
    size_t r_row = 0, r_col = 0;
    bool redone = buffer_redo(&buf, &r_row, &r_col);
    TEST_ASSERT(redone, "Redo succeeded");
    TEST_ASSERT(strcmp(buffer_line(&buf, 0), "Hell Markdown") == 0, "Redo re-applied backspace");

    // File Save & Load
    const char *test_path = "/tmp/test_mde_buffer.md";
    bool saved = buffer_save(&buf, test_path);
    TEST_ASSERT(saved, "Buffer saved to file");

    Buffer buf2;
    buffer_init(&buf2);
    bool loaded = buffer_load(&buf2, test_path);
    TEST_ASSERT(loaded, "Buffer loaded from file");
    TEST_ASSERT(strcmp(buffer_line(&buf2, 0), "Hell Markdown") == 0, "Loaded content matches saved content");

    buffer_free(&buf);
    buffer_free(&buf2);
    remove(test_path);
}

static void test_markdown_parser(void) {
    printf("--- Testing Markdown Parser & Lexer ---\n");

    bool in_code = false;
    char lang[32] = {0};
    MdParsedLine pl;

    // Header 1
    const char *h1 = "# Main Heading";
    md_parse_line(h1, strlen(h1), &in_code, lang, sizeof(lang), &pl);
    TEST_ASSERT(pl.header_level == 1, "Parsed H1 level is 1");
    TEST_ASSERT(pl.span_count >= 2, "H1 has at least marker and text spans");
    TEST_ASSERT(pl.spans[0].type == MD_HEADER_MARKER_1, "H1 marker type");
    TEST_ASSERT(pl.spans[1].type == MD_HEADER_TEXT_1, "H1 text type");
    TEST_ASSERT(md_is_header_marker(pl.spans[0].type), "md_is_header_marker identifies marker");
    TEST_ASSERT(md_is_header_text(pl.spans[1].type), "md_is_header_text identifies header text");
    md_parsed_line_free(&pl);

    // Header 3
    const char *h3 = "### Sub Section";
    md_parse_line(h3, strlen(h3), &in_code, lang, sizeof(lang), &pl);
    TEST_ASSERT(pl.header_level == 3, "Parsed H3 level is 3");
    md_parsed_line_free(&pl);

    // Task list unchecked
    const char *task_uncheck = "- [ ] Write tests";
    md_parse_line(task_uncheck, strlen(task_uncheck), &in_code, lang, sizeof(lang), &pl);
    TEST_ASSERT(pl.is_list_item, "Task list is list item");
    TEST_ASSERT(pl.is_task, "Detected as task box");
    TEST_ASSERT(!pl.task_checked, "Task is unchecked");
    md_parsed_line_free(&pl);

    // Task list checked
    const char *task_check = "- [x] Done feature";
    md_parse_line(task_check, strlen(task_check), &in_code, lang, sizeof(lang), &pl);
    TEST_ASSERT(pl.is_task, "Detected as task box");
    TEST_ASSERT(pl.task_checked, "Task is checked");
    md_parsed_line_free(&pl);

    // Inlines: Bold and Inline code
    const char *inline_text = "Here is **bold text** and `code` span.";
    md_parse_line(inline_text, strlen(inline_text), &in_code, lang, sizeof(lang), &pl);
    bool found_bold = false;
    bool found_code = false;
    for (size_t i = 0; i < pl.span_count; ++i) {
        if (pl.spans[i].type == MD_BOLD_TEXT) found_bold = true;
        if (pl.spans[i].type == MD_INLINE_CODE_TEXT) found_code = true;
    }
    TEST_ASSERT(found_bold, "Found bold text span");
    TEST_ASSERT(found_code, "Found inline code text span");
    md_parsed_line_free(&pl);

    // Code block opening and closing
    const char *fence_open = "```c";
    md_parse_line(fence_open, strlen(fence_open), &in_code, lang, sizeof(lang), &pl);
    TEST_ASSERT(in_code == true, "Entering code block");
    TEST_ASSERT(strcmp(lang, "c") == 0, "Detected language 'c'");
    md_parsed_line_free(&pl);

    const char *code_line = "int x = 42;";
    md_parse_line(code_line, strlen(code_line), &in_code, lang, sizeof(lang), &pl);
    TEST_ASSERT(pl.is_code_block, "Line inside code block identified");
    md_parsed_line_free(&pl);

    const char *fence_close = "```";
    md_parse_line(fence_close, strlen(fence_close), &in_code, lang, sizeof(lang), &pl);
    TEST_ASSERT(in_code == false, "Exiting code block");
    md_parsed_line_free(&pl);

    // Horizontal rule
    const char *hr = "---";
    TEST_ASSERT(md_is_hrule(hr, strlen(hr)), "Detected horizontal rule ---");
}

static void test_active_markers(void) {
    printf("--- Testing Active vs Inactive Marker Highlighting ---\n");
    attr_t inactive_attr = theme_token_attr(MD_BOLD_MARKER, false);
    attr_t active_attr = theme_token_attr(MD_BOLD_MARKER, true);

    TEST_ASSERT(inactive_attr != active_attr, "Active marker attribute differs from inactive");
    TEST_ASSERT(active_attr & A_BOLD, "Active marker is bold");
}

static void test_cursor_math(void) {
    printf("--- Testing Editor Cursor Coordinate Calculations ---\n");
    Editor ed;
    editor_init(&ed, NULL);

    // Initial state: row 0, col 0
    TEST_ASSERT(ed.cursor_row == 0, "Initial cursor row is 0");
    TEST_ASSERT(ed.cursor_col == 0, "Initial cursor col is 0");

    // Insert text: "Hello UTF-8"
    editor_insert_char(&ed, "H", 1);
    editor_insert_char(&ed, "i", 1);
    TEST_ASSERT(ed.cursor_col == 2, "Cursor col is 2 after inserting 'Hi'");

    // Smart enter: new line
    editor_smart_enter(&ed);
    TEST_ASSERT(ed.cursor_row == 1, "Cursor row is 1 after Enter");
    TEST_ASSERT(ed.cursor_col == 0, "Cursor col is 0 on new line");

    // Move up
    editor_move_up(&ed);
    TEST_ASSERT(ed.cursor_row == 0, "Cursor row is 0 after move up");

    // Move end
    editor_move_end(&ed);
    TEST_ASSERT(ed.cursor_col == 2, "Cursor at line end (col 2)");

    editor_free(&ed);
}

static void test_cursor_visibility(void) {
    printf("--- Testing Steady Non-Flashing Cursor ---\n");
    Editor ed;
    editor_init(&ed, NULL);

    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Insert cursor is visible after init");

    uint64_t now = editor_get_time_ms();
    ed.last_key_time = now - 100; // 100 ms ago
    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Insert cursor is steady while typing (100 ms)");

    ed.last_key_time = now - 600; // 600 ms ago
    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Insert cursor remains steady after typing (no flash)");

    ed.last_key_time = now - 5000; // 5000 ms ago
    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Insert cursor remains steady when idle (no flash)");

    // Test in Overwrite mode as well
    editor_toggle_insert_mode(&ed);
    TEST_ASSERT(ed.insert_mode == false, "Overwrite mode enabled");
    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Overwrite cursor is visible");

    ed.last_key_time = now - 200;
    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Overwrite cursor is steady while typing (200 ms)");

    ed.last_key_time = now - 800;
    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Overwrite cursor remains steady after typing (no flash)");

    ed.last_key_time = now - 10000;
    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Overwrite cursor remains steady when idle (no flash)");

    editor_free(&ed);
}

static void test_insert_mode(void) {
    printf("--- Testing Insert Mode & Cursor Toggle ---\n");
    Editor ed;
    editor_init(&ed, NULL);

    TEST_ASSERT(ed.insert_mode == true, "Initial mode is Insert (block cursor)");

    // Test text insertion in default insert mode
    editor_insert_char(&ed, "H", 1);
    editor_insert_char(&ed, "e", 1);
    editor_insert_char(&ed, "l", 1);
    editor_insert_char(&ed, "l", 1);
    editor_insert_char(&ed, "o", 1);
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "Hello") == 0, "Line has 'Hello'");

    // Move cursor to index 1 ('e') and insert 'a' in insert mode
    ed.cursor_col = 1;
    editor_insert_char(&ed, "a", 1);
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "Haello") == 0, "Insert mode inserted 'a' without overwriting");

    // Toggle to Overwrite mode via Ins button toggle
    editor_toggle_insert_mode(&ed);
    TEST_ASSERT(ed.insert_mode == false, "Switched to Overwrite mode (underscore cursor)");

    // In overwrite mode, cursor is at index 2 ('e'), typing 'i' overwrites 'e' with 'i'
    editor_insert_char(&ed, "i", 1);
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "Haillo") == 0, "Overwrite mode replaced 'e' with 'i'");

    // Move to end of line and type in overwrite mode (should append)
    ed.cursor_col = buffer_line_len(&ed.buffer, 0);
    editor_insert_char(&ed, "!", 1);
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "Haillo!") == 0, "Overwrite mode appended '!' at line end");

    // Toggle back to Insert mode
    editor_toggle_insert_mode(&ed);
    TEST_ASSERT(ed.insert_mode == true, "Switched back to Insert mode");

    editor_free(&ed);
}

static void test_renderer(void) {
    printf("--- Testing Markdown Renderer ---\n");

    Buffer buf;
    buffer_init(&buf);

    buffer_replace_line(&buf, 0, "# Document Title", 16, 0);
    buffer_split_line(&buf, 0, 16, 0);
    buffer_insert_string(&buf, 1, 0, "- [ ] Task one", 14, 0);

    RenderedDoc doc;
    rendered_doc_init(&doc);
    render_document(&buf, 80, &doc);

    TEST_ASSERT(doc.count >= 2, "Renderer produced rendered lines");
    TEST_ASSERT(doc.lines[0].chunk_count > 0, "Rendered line has chunks");

    size_t line_idx = rendered_doc_find_line(&doc, 1);
    TEST_ASSERT(line_idx > 0, "Found rendered line corresponding to buffer row 1");

    // Test cursor vcol mapping in rendered view
    int vcol_h1_start = renderer_get_cursor_vcol(&buf, &doc, 0, 0);
    TEST_ASSERT(vcol_h1_start == 0, "H1 marker start maps to vcol 0 in preview");

    int vcol_h1_end = renderer_get_cursor_vcol(&buf, &doc, 0, 16);
    TEST_ASSERT(vcol_h1_end == 14, "H1 end of line maps to end of rendered title (14 cols)");

    int vcol_task = renderer_get_cursor_vcol(&buf, &doc, 1, 6);
    TEST_ASSERT(vcol_task >= 3, "Task item cursor accounts for checkbox prefix");

    // Test editor navigation and editing in VIEW_PREVIEW mode
    Editor ed;
    editor_init(&ed, NULL);
    ed.view_mode = VIEW_PREVIEW;
    editor_insert_char(&ed, "A", 1);
    editor_smart_enter(&ed);
    editor_insert_char(&ed, "B", 1);
    TEST_ASSERT(ed.cursor_row == 1, "Cursor row is 1 in preview mode after Enter");
    editor_move_up(&ed);
    TEST_ASSERT(ed.cursor_row == 0, "Cursor row moved up to 0 in preview mode");
    editor_move_down(&ed);
    TEST_ASSERT(ed.cursor_row == 1, "Cursor row moved down to 1 in preview mode");
    editor_free(&ed);

    rendered_doc_free(&doc);
    buffer_free(&buf);
}

static void test_selection_and_mark(void) {
    printf("--- Testing Selection & Mark (Ctrl-Space) ---\n");
    Editor ed;
    editor_init(&ed, NULL);

    // Insert text: "Hello World\nSecond Line"
    editor_paste_text(&ed, "Hello World\nSecond Line", 23);
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 2, "Buffer has 2 lines");

    // Initially no selection
    TEST_ASSERT(!editor_has_selection(&ed), "No selection initially");

    // Move to (0, 0), set mark
    ed.cursor_row = 0;
    ed.cursor_col = 0;
    editor_set_mark(&ed);
    TEST_ASSERT(ed.mark_active == true, "Mark is active");
    TEST_ASSERT(!editor_has_selection(&ed), "No selection yet (mark == cursor)");

    // Move cursor to (0, 5)
    ed.cursor_col = 5;
    TEST_ASSERT(editor_has_selection(&ed), "Selection active after cursor move");
    size_t sr, sc, er, ec;
    TEST_ASSERT(editor_get_selection(&ed, &sr, &sc, &er, &ec), "get_selection succeeds");
    TEST_ASSERT(sr == 0 && sc == 0 && er == 0 && ec == 5, "Selection coordinates are (0,0)-(0,5)");

    size_t sel_len = 0;
    char *sel_text = editor_get_selected_text(&ed, &sel_len);
    TEST_ASSERT(sel_text != NULL, "Selected text extracted");
    TEST_ASSERT(sel_len == 5 && strcmp(sel_text, "Hello") == 0, "Selected text is 'Hello'");
    free(sel_text);

    // Clear mark
    editor_clear_mark(&ed);
    TEST_ASSERT(ed.mark_active == false, "Mark deactivated after clear");
    TEST_ASSERT(!editor_has_selection(&ed), "No selection after clear");

    // Test reverse selection: mark at (0, 5), cursor at (0, 0)
    ed.cursor_col = 5;
    editor_set_mark(&ed);
    ed.cursor_col = 0;
    TEST_ASSERT(editor_get_selection(&ed, &sr, &sc, &er, &ec), "get_selection succeeds for reversed selection");
    TEST_ASSERT(sr == 0 && sc == 0 && er == 0 && ec == 5, "Normalized selection is (0,0)-(0,5)");

    editor_free(&ed);
}

static void test_cut_copy_paste(void) {
    printf("--- Testing Cut, Copy, Paste ---\n");
    Editor ed;
    editor_init(&ed, NULL);

    editor_paste_text(&ed, "Hello World", 11);

    // Select "World"
    ed.cursor_col = 6;
    editor_set_mark(&ed);
    ed.cursor_col = 11;

    // Copy
    TEST_ASSERT(editor_copy(&ed), "Copy succeeded");
    TEST_ASSERT(ed.clipboard != NULL && strcmp(ed.clipboard, "World") == 0, "Clipboard has 'World'");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "Hello World") == 0, "Buffer unchanged after copy");

    // Select "Hello " and Cut
    ed.cursor_col = 0;
    editor_set_mark(&ed);
    ed.cursor_col = 6;
    TEST_ASSERT(editor_cut(&ed), "Cut succeeded");
    TEST_ASSERT(ed.clipboard != NULL && strcmp(ed.clipboard, "Hello ") == 0, "Clipboard has 'Hello '");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "World") == 0, "Buffer has 'World' after cut");
    TEST_ASSERT(ed.cursor_col == 0, "Cursor at start of cut");

    // Undo cut
    editor_undo(&ed);
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "Hello World") == 0, "Undo restored 'Hello World'");

    // Redo cut
    editor_redo(&ed);
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "World") == 0, "Redo re-cut 'Hello '");

    // Move to end and Paste ("Hello ")
    ed.cursor_col = 5;
    TEST_ASSERT(editor_paste(&ed), "Paste succeeded");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "WorldHello ") == 0, "Pasted 'Hello ' at end");

    // Multiline selection cut & paste
    editor_smart_enter(&ed);
    editor_paste_text(&ed, "Line Two\nLine Three", 19);
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 3, "Buffer has 3 lines");

    // Select from row 0 col 5 to row 2 col 4
    ed.cursor_row = 0; ed.cursor_col = 5;
    editor_set_mark(&ed);
    ed.cursor_row = 2; ed.cursor_col = 4;
    TEST_ASSERT(editor_cut(&ed), "Multiline cut succeeded");
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 1, "Multiline cut reduced to 1 line");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "World Three") == 0, "Joined line is 'World Three'");

    // Undo multiline cut
    editor_undo(&ed);
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 3, "Undo restored all 3 lines");

    editor_free(&ed);
}

static void test_kill_line_and_word(void) {
    printf("--- Testing C-k (Kill Line) & C-w (Kill Word) ---\n");
    Editor ed;
    editor_init(&ed, NULL);

    editor_paste_text(&ed, "First Line\nSecond Line", 22);

    // C-k in middle of line 0
    ed.cursor_row = 0;
    ed.cursor_col = 5; // after "First"
    editor_kill_to_end_of_line(&ed);
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "First") == 0, "Line 0 killed to 'First'");
    TEST_ASSERT(strcmp(ed.clipboard, " Line") == 0, "Clipboard has ' Line'");

    // C-k at end of line 0 kills newline (joins with Line 1)
    TEST_ASSERT(ed.cursor_col == 5, "Cursor at line end");
    editor_kill_to_end_of_line(&ed);
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 1, "C-k at line end joined lines");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "FirstSecond Line") == 0, "Joined line content");

    // C-w (Kill previous word)
    // Cursor at col 5 ("First|Second Line") -> kill prev word kills "First"
    editor_kill_prev_word(&ed);
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "Second Line") == 0, "C-w killed 'First'");
    TEST_ASSERT(strcmp(ed.clipboard, "First") == 0, "Clipboard has 'First'");
    TEST_ASSERT(ed.cursor_col == 0, "Cursor at 0 after killing word");

    // Move to end of line
    ed.cursor_col = buffer_line_len(&ed.buffer, 0); // after "Line"
    editor_kill_prev_word(&ed);
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "Second ") == 0, "C-w killed 'Line'");
    TEST_ASSERT(strcmp(ed.clipboard, "Line") == 0, "Clipboard has 'Line'");

    editor_kill_prev_word(&ed);
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "") == 0, "C-w killed 'Second '");

    editor_free(&ed);
}

static void test_home_and_end(void) {
    printf("--- Testing C-a (Home) & C-e (End) ---\n");
    Editor ed;
    editor_init(&ed, NULL);

    editor_paste_text(&ed, "The quick brown fox", 19);
    ed.cursor_col = 5;

    // C-a (Home)
    editor_move_home(&ed);
    TEST_ASSERT(ed.cursor_col == 0, "C-a moved cursor to 0");
    TEST_ASSERT(ed.target_visual_col == 0, "Target visual col is 0");

    // C-e (End)
    editor_move_end(&ed);
    TEST_ASSERT(ed.cursor_col == 19, "C-e moved cursor to 19 (line length)");

    editor_free(&ed);
}

static void test_auto_line_break(void) {
    printf("--- Testing Automatic Line Break (Default 78, 20-255 or Off) ---\n");
    Editor ed;
    editor_init(&ed, NULL);

    // Default wrap column
    TEST_ASSERT(ed.wrap_col == 78, "Default wrap_col is 78");

    // Parsing validation
    int parsed = -1;
    TEST_ASSERT(editor_parse_wrap_col("off", &parsed) && parsed == 0, "Parse 'off' -> 0");
    TEST_ASSERT(editor_parse_wrap_col("OFF", &parsed) && parsed == 0, "Parse 'OFF' -> 0");
    TEST_ASSERT(editor_parse_wrap_col("none", &parsed) && parsed == 0, "Parse 'none' -> 0");
    TEST_ASSERT(editor_parse_wrap_col("0", &parsed) && parsed == 0, "Parse '0' -> 0");
    TEST_ASSERT(editor_parse_wrap_col("20", &parsed) && parsed == 20, "Parse '20' -> 20");
    TEST_ASSERT(editor_parse_wrap_col("78", &parsed) && parsed == 78, "Parse '78' -> 78");
    TEST_ASSERT(editor_parse_wrap_col("255", &parsed) && parsed == 255, "Parse '255' -> 255");
    TEST_ASSERT(editor_parse_wrap_col(" 100 ", &parsed) && parsed == 100, "Parse ' 100 ' -> 100");
    TEST_ASSERT(!editor_parse_wrap_col("19", &parsed), "Reject 19 (< 20)");
    TEST_ASSERT(!editor_parse_wrap_col("256", &parsed), "Reject 256 (> 255)");
    TEST_ASSERT(!editor_parse_wrap_col("-10", &parsed), "Reject negative value");
    TEST_ASSERT(!editor_parse_wrap_col("abc", &parsed), "Reject non-numeric string");
    TEST_ASSERT(!editor_parse_wrap_col("", &parsed), "Reject empty string");

    // Setter bounds validation
    TEST_ASSERT(!editor_set_wrap_col(&ed, 19), "editor_set_wrap_col rejects 19");
    TEST_ASSERT(!editor_set_wrap_col(&ed, 256), "editor_set_wrap_col rejects 256");
    TEST_ASSERT(!editor_set_wrap_col(&ed, -1), "editor_set_wrap_col rejects -1");
    TEST_ASSERT(ed.wrap_col == 78, "wrap_col unchanged after invalid sets");

    // 1. Default 78-column word wrap
    const char *sample78 = "The quick brown fox jumps over the lazy dog while writing clean Markdown text today";
    for (size_t i = 0; sample78[i] != '\0'; ++i) {
        editor_insert_char(&ed, &sample78[i], 1);
    }
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 2, "Typing > 78 cols wraps to 2 lines");
    TEST_ASSERT(utf8_visual_width(buffer_line(&ed.buffer, 0), buffer_line_len(&ed.buffer, 0)) <= 78,
                "First line visual width <= 78");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0),
                       "The quick brown fox jumps over the lazy dog while writing clean Markdown text") == 0,
                "Wrapped cleanly at word boundary before 'today'");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 1), "today") == 0, "Second line contains 'today'");
    TEST_ASSERT(ed.cursor_row == 1 && ed.cursor_col == 5, "Cursor at end of 'today' on line 1");
    editor_free(&ed);

    // 2. Custom column width = 20 (word wrap + hard break on unbroken token)
    editor_init(&ed, NULL);
    TEST_ASSERT(editor_set_wrap_col(&ed, 20), "Set wrap_col to 20");
    const char *s20 = "Hello world, this foo";
    for (size_t i = 0; s20[i] != '\0'; ++i) {
        editor_insert_char(&ed, &s20[i], 1);
    }
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 2, "Custom wrap 20 splits into 2 lines");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "Hello world, this") == 0, "Line 0 is 'Hello world, this'");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 1), "foo") == 0, "Line 1 is 'foo'");
    TEST_ASSERT(ed.cursor_row == 1 && ed.cursor_col == 3, "Cursor follows wrapped word");

    // Hard break for unbroken 22-char token
    editor_smart_enter(&ed);
    const char *unbroken = "abcdefghijklmnopqrstuv";
    for (size_t i = 0; unbroken[i] != '\0'; ++i) {
        editor_insert_char(&ed, &unbroken[i], 1);
    }
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 4, "Unbroken 22-char word hard-wraps at col 20");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 2), "abcdefghijklmnopqrst") == 0, "First 20 chars on line 2");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 3), "uv") == 0, "Remaining 2 chars on line 3");
    editor_free(&ed);

    // 3. Blockquote continuation prefix preservation on wrap
    editor_init(&ed, NULL);
    TEST_ASSERT(editor_set_wrap_col(&ed, 20), "Set wrap_col to 20 for blockquote test");
    const char *bq = "> Quoted sentence wraps";
    for (size_t i = 0; bq[i] != '\0'; ++i) {
        editor_insert_char(&ed, &bq[i], 1);
    }
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 2, "Blockquote wrapped to 2 lines");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "> Quoted sentence") == 0, "Blockquote line 0");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 1), "> wraps") == 0, "Blockquote line 1 preserves '> ' prefix");
    editor_free(&ed);

    // 4. Wrap OFF (0) disables automatic line break
    editor_init(&ed, NULL);
    TEST_ASSERT(editor_set_wrap_col(&ed, MDE_WRAP_COL_OFF), "Disable wrap_col (0)");
    for (int i = 0; i < 120; ++i) {
        editor_insert_char(&ed, "a", 1);
    }
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 1, "No line break when wrap is OFF (120 chars)");
    TEST_ASSERT(buffer_line_len(&ed.buffer, 0) == 120, "All 120 chars stay on line 0");

    // 5. Max boundary (255 columns)
    TEST_ASSERT(editor_set_wrap_col(&ed, 255), "Set wrap_col to 255");
    for (int i = 0; i < 140; ++i) {
        editor_insert_char(&ed, "b", 1);
    }
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 2, "Line > 255 chars wraps at 255");
    TEST_ASSERT(buffer_line_len(&ed.buffer, 0) == 255, "Line 0 is exactly 255 chars");
    TEST_ASSERT(buffer_line_len(&ed.buffer, 1) == 5, "Line 1 has remaining 5 chars");

    editor_free(&ed);
}

static void test_resize_graceful(void) {
    printf("--- Testing Graceful Resize & Scroll Clamping ---\n");
    Editor ed;
    editor_init(&ed, NULL);

    // Populate 30 lines of text
    for (int i = 0; i < 30; ++i) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Line %02d with enough text to test horizontal scrolling on narrow panes", i + 1);
        if (i > 0) {
            buffer_split_line(&ed.buffer, (size_t)(i - 1), buffer_line_len(&ed.buffer, (size_t)(i - 1)), 0);
        }
        buffer_replace_line(&ed.buffer, (size_t)i, buf, strlen(buf), 0);
    }

    // Small viewport (10 rows, 30 cols): move cursor to bottom line end
    ed.editor_width = 30;
    ed.cursor_row = 29;
    ed.cursor_col = buffer_line_len(&ed.buffer, 29);
    editor_update_scroll(&ed, 10);
    TEST_ASSERT(ed.scroll_row == 20, "Small 10-row viewport scrolls to row 20 for cursor at 29");
    TEST_ASSERT(ed.scroll_col > 0, "Narrow 30-col viewport scrolls horizontally for long line");

    // Grow viewport taller (40 rows > 30 total lines) and wider (120 cols)
    ed.editor_width = 120;
    editor_update_scroll(&ed, 40);
    TEST_ASSERT(ed.scroll_row == 0, "Growing taller than file clamps scroll_row back to 0");
    TEST_ASSERT(ed.scroll_col == 0, "Growing wider than line resets horizontal scroll_col to 0");

    // Shrink viewport again (15 rows, 40 cols)
    ed.editor_width = 40;
    editor_update_scroll(&ed, 15);
    TEST_ASSERT(ed.scroll_row == 15, "Shrinking to 15 rows keeps cursor row 29 visible");
    TEST_ASSERT(ed.scroll_col > 0, "Shrinking to 40 cols scrolls horizontally to keep cursor visible");

    // Ultra-tiny terminal hint rows check
    TEST_ASSERT(ui_get_shortcut_hint_rows(5) == 0, "Ultra-tiny 5-row terminal hides shortcut hints");
    TEST_ASSERT(ui_get_shortcut_hint_rows(6) == 1, "6-row terminal gets 1 shortcut hint row");

    // Renderer adapts across extreme widths (narrow 12 cols to wide 200 cols)
    RenderedDoc doc;
    rendered_doc_init(&doc);
    buffer_replace_line(&ed.buffer, 0, "---", 3, 0);
    render_document(&ed.buffer, 12, &doc);
    TEST_ASSERT(doc.count > 0 && doc.lines[0].chunk_count > 0, "Renderer handles narrow 12-col width");
    int narrow_w = utf8_visual_width(doc.lines[0].chunks[0].text, doc.lines[0].chunks[0].text_len);
    TEST_ASSERT(narrow_w <= 12, "Horizontal rule fits inside narrow 12-col width");

    render_document(&ed.buffer, 200, &doc);
    int wide_w = utf8_visual_width(doc.lines[0].chunks[0].text, doc.lines[0].chunks[0].text_len);
    TEST_ASSERT(wide_w > 100 && wide_w <= 200, "Horizontal rule expands cleanly for wide 200-col width");

    rendered_doc_free(&doc);
    editor_free(&ed);
}

static void test_shortcut_hints_and_dialog(void) {
    printf("--- Testing Shortcut Hints & File Dialogue API ---\n");

    // Shortcut hint rows calculation
    TEST_ASSERT(ui_get_shortcut_hint_rows(24) == 2, "Standard 24-row terminal gets 2 shortcut hint rows");
    TEST_ASSERT(ui_get_shortcut_hint_rows(25) == 2, "25-row terminal gets 2 shortcut hint rows");
    TEST_ASSERT(ui_get_shortcut_hint_rows(28) == 3, "28-row terminal gets 3 shortcut hint rows");
    TEST_ASSERT(ui_get_shortcut_hint_rows(40) == 3, "40-row terminal gets 3 shortcut hint rows");
    TEST_ASSERT(ui_get_shortcut_hint_rows(15) == 1, "Small 15-row terminal gets 1 shortcut hint row");

    // File dialog safe parameter validation
    Editor ed;
    editor_init(&ed, NULL);

    char path_buf[256];
    TEST_ASSERT(!ui_file_dialog(NULL, FILE_DIALOG_OPEN, path_buf, sizeof(path_buf)), "Null editor returns false");
    TEST_ASSERT(!ui_file_dialog(&ed, FILE_DIALOG_OPEN, NULL, 100), "Null out_path returns false");
    TEST_ASSERT(!ui_file_dialog(&ed, FILE_DIALOG_OPEN, path_buf, 0), "Zero max_len returns false");

    editor_free(&ed);
}

int main(void) {
    setlocale(LC_ALL, "");
    printf("==========================================\n");
    printf("Running mde Test Suite (C11)...\n");
    printf("==========================================\n");

    test_version();
    test_utf8();
    test_buffer_and_undo();
    test_markdown_parser();
    test_active_markers();
    test_cursor_math();
    test_cursor_visibility();
    test_insert_mode();
    test_renderer();
    test_selection_and_mark();
    test_cut_copy_paste();
    test_kill_line_and_word();
    test_home_and_end();
    test_auto_line_break();
    test_resize_graceful();
    test_shortcut_hints_and_dialog();

    printf("==========================================\n");
    printf("Test Results: %d / %d tests passed (%.1f%%)\n",
           g_tests_passed, g_tests_run,
           g_tests_run > 0 ? (100.0 * g_tests_passed / g_tests_run) : 0.0);
    printf("==========================================\n");

    return (g_tests_run == g_tests_passed) ? 0 : 1;
}
