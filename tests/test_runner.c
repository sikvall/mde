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
    TEST_ASSERT(strcmp(MDE_VERSION_STRING, "1.0.0") == 0, "Version string is '1.0.0'");
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

    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Normal cursor is visible after init");

    uint64_t now = editor_get_time_ms();
    ed.last_key_time = now - 100; // 100 ms ago
    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Normal cursor is steady while typing (100 ms)");

    ed.last_key_time = now - 600; // 600 ms ago
    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Normal cursor remains steady after typing (no flash)");

    ed.last_key_time = now - 5000; // 5000 ms ago
    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Normal cursor remains steady when idle (no flash)");

    // Test in Insert mode as well
    editor_toggle_insert_mode(&ed);
    TEST_ASSERT(ed.insert_mode == true, "Insert mode enabled");
    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Insert cursor is visible");

    ed.last_key_time = now - 200;
    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Insert cursor is steady while typing (200 ms)");

    ed.last_key_time = now - 800;
    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Insert cursor remains steady after typing (no flash)");

    ed.last_key_time = now - 10000;
    TEST_ASSERT(editor_cursor_is_visible(&ed) == true, "Insert cursor remains steady when idle (no flash)");

    editor_free(&ed);
}

static void test_insert_mode(void) {
    printf("--- Testing Insert Mode & Cursor Toggle ---\n");
    Editor ed;
    editor_init(&ed, NULL);

    TEST_ASSERT(ed.insert_mode == false, "Initial mode is Normal (normal cursor)");

    // Switch to Insert mode via Ins button toggle
    editor_toggle_insert_mode(&ed);
    TEST_ASSERT(ed.insert_mode == true, "Switched to Insert mode (underscore cursor)");

    // Test text insertion in insert mode
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

    // Toggle back to Normal mode
    editor_toggle_insert_mode(&ed);
    TEST_ASSERT(ed.insert_mode == false, "Switched to Normal mode");

    // In normal mode, cursor is at index 2 ('e'), typing 'i' overwrites 'e' with 'i'
    editor_insert_char(&ed, "i", 1);
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "Haillo") == 0, "Normal mode replaced 'e' with 'i'");

    // Move to end of line and type in normal mode (should append)
    ed.cursor_col = buffer_line_len(&ed.buffer, 0);
    editor_insert_char(&ed, "!", 1);
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "Haillo!") == 0, "Normal mode appended '!' at line end");

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

    printf("==========================================\n");
    printf("Test Results: %d / %d tests passed (%.1f%%)\n",
           g_tests_passed, g_tests_run,
           g_tests_run > 0 ? (100.0 * g_tests_passed / g_tests_run) : 0.0);
    printf("==========================================\n");

    return (g_tests_run == g_tests_passed) ? 0 : 1;
}
