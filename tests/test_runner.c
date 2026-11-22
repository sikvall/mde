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
#include <unistd.h>
#include <sys/stat.h>

#include "../src/version.h"
#include "../src/utf8.h"
#include "../src/buffer.h"
#include "../src/markdown.h"
#include "../src/renderer.h"
#include "../src/theme.h"
#include "../src/editor.h"
#include "../src/export.h"
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

/** @brief Verify centralized application metadata and version macros from version.h. */
static void test_version(void) {
    printf("--- Testing Version & Build Metadata ---\n");
    TEST_ASSERT(strcmp(MDE_APP_NAME, "mde") == 0, "App name is 'mde'");
    TEST_ASSERT(strcmp(MDE_APP_TITLE, "Markdown Editor") == 0, "App title is 'Markdown Editor'");
    TEST_ASSERT(strcmp(MDE_VERSION_STRING, "0.1.1") == 0, "Version string is '0.1.1'");
    TEST_ASSERT(MDE_VERSION_MAJOR == 0 && MDE_VERSION_MINOR == 1 && MDE_VERSION_PATCH == 1,
                "Numeric version macros match 0.1.1");
    TEST_ASSERT(strlen(MDE_BUILD_DATE) > 0, "Build date is non-empty");
    TEST_ASSERT(strlen(MDE_BUILD_TIME) > 0, "Build time is non-empty");
    TEST_ASSERT(strlen(MDE_AUTHOR) > 0, "Author is non-empty");
    TEST_ASSERT(strlen(MDE_MAINTAINER) > 0, "Maintainer is non-empty");
    TEST_ASSERT(strlen(MDE_DESCRIPTION) > 0, "Description is non-empty");
}

/** @brief Verify UTF-8 width, codepoint counting, navigation, and column conversions. */
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

/** @brief Verify line buffer mutations, undo/redo stacks, and disk save/load round-trips. */
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

/** @brief Verify Markdown block and inline token span lexing across syntax constructs. */
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

/** @brief Verify active-line syntax markers render distinctly from dimmed inactive markers. */
static void test_active_markers(void) {
    printf("--- Testing Active vs Inactive Marker Highlighting ---\n");
    attr_t inactive_attr = theme_token_attr(MD_BOLD_MARKER, false);
    attr_t active_attr = theme_token_attr(MD_BOLD_MARKER, true);

    TEST_ASSERT(inactive_attr != active_attr, "Active marker attribute differs from inactive");
    TEST_ASSERT(active_attr & A_BOLD, "Active marker is bold");
}

/** @brief Verify basic editor cursor movement and line-end positioning. */
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

/** @brief Verify steady non-flashing cursor visibility in both Insert and Overwrite modes. */
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

/** @brief Verify Insert vs. Overwrite typing behavior and mode toggling. */
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

/** @brief Verify rendered preview generation, line mapping, and preview cursor tracking. */
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

/** @brief Verify selection mark anchor, forward/reversed normalization, and text extraction. */
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

/** @brief Verify single-line and multi-line cut, copy, paste, and atomic undo/redo. */
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

/** @brief Verify kill-to-end-of-line and kill-previous-word clipboard mutations. */
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

/** @brief Verify Home (^A) and End (^E) line navigation. */
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

/** @brief Verify automatic line breaking (default 78, configurable 20-255, or off). */
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

/** @brief Verify graceful terminal resizing, viewport scroll clamping, and preview scaling. */
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

/** @brief Read an entire file from disk into a newly allocated null-terminated string. */
static char *read_entire_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

/** @brief Verify built-in HTML5, XML, LaTeX, and Pandoc EPUB export pipelines. */
static void test_export_subsystem(void) {
    printf("--- Testing Export Subsystem (HTML, XML, EPUB, LaTeX & Pandoc) ---\n");

    // Format names & extensions
    TEST_ASSERT(strcmp(export_format_name(EXPORT_HTML), "HTML") == 0, "HTML format name");
    TEST_ASSERT(strcmp(export_format_name(EXPORT_XML), "XML") == 0, "XML format name");
    TEST_ASSERT(strcmp(export_format_name(EXPORT_EPUB), "EPUB") == 0, "EPUB format name");
    TEST_ASSERT(strcmp(export_format_name(EXPORT_LATEX), "LaTeX") == 0, "LaTeX format name");

    // Default output path derivation
    char path[256];
    export_default_path("notes.md", EXPORT_HTML, path, sizeof(path));
    TEST_ASSERT(strcmp(path, "notes.html") == 0, "notes.md -> notes.html");
    export_default_path("/tmp/doc.markdown", EXPORT_XML, path, sizeof(path));
    TEST_ASSERT(strcmp(path, "/tmp/doc.xml") == 0, "/tmp/doc.markdown -> /tmp/doc.xml");
    export_default_path("book.md", EXPORT_EPUB, path, sizeof(path));
    TEST_ASSERT(strcmp(path, "book.epub") == 0, "book.md -> book.epub");
    export_default_path("paper.md", EXPORT_LATEX, path, sizeof(path));
    TEST_ASSERT(strcmp(path, "paper.tex") == 0, "paper.md -> paper.tex");
    export_default_path(NULL, EXPORT_HTML, path, sizeof(path));
    TEST_ASSERT(strcmp(path, "untitled.html") == 0, "NULL -> untitled.html");

    // Build sample Markdown buffer with rich features
    Buffer buf;
    buffer_init(&buf);
    const char *sample_lines[] = {
        "# Export Title",
        "",
        "Paragraph with **bold**, *italic*, `code`, and [Link](https://example.com) & <tag>.",
        "",
        "- [x] Done task",
        "- [ ] Open task",
        "",
        "> Quoted wisdom",
        "",
        "```c",
        "int x = 1 < 2;",
        "```",
        "",
        "| Key | Val |",
        "| --- | --- |",
        "| A   | B   |",
        "",
        "---"
    };
    size_t nsample = sizeof(sample_lines) / sizeof(sample_lines[0]);
    for (size_t i = 0; i < nsample; ++i) {
        if (i > 0) {
            buffer_split_line(&buf, i - 1, buffer_line_len(&buf, i - 1), 0);
        }
        buffer_replace_line(&buf, i, sample_lines[i], strlen(sample_lines[i]), 0);
    }

    char used[32] = {0};
    char err[256] = {0};

    // 1. Built-in HTML5 export
    const char *html_out = "/tmp/test_mde_export.html";
    bool ok = export_document(&buf, EXPORT_HTML, html_out, EXPORT_ENGINE_BUILTIN,
                              used, sizeof(used), err, sizeof(err));
    TEST_ASSERT(ok, "Built-in HTML export succeeded");
    TEST_ASSERT(strcmp(used, "built-in") == 0, "HTML engine reported as built-in");
    char *html = read_entire_file(html_out);
    TEST_ASSERT(html != NULL, "Read exported HTML file");
    if (html) {
        TEST_ASSERT(strstr(html, "<!DOCTYPE html>") != NULL, "HTML has DOCTYPE");
        TEST_ASSERT(strstr(html, "<h1>Export Title</h1>") != NULL, "HTML has H1");
        TEST_ASSERT(strstr(html, "<strong>bold</strong>") != NULL, "HTML has strong tag");
        TEST_ASSERT(strstr(html, "<em>italic</em>") != NULL, "HTML has em tag");
        TEST_ASSERT(strstr(html, "<code>code</code>") != NULL, "HTML has inline code");
        TEST_ASSERT(strstr(html, "<a href=\"https://example.com\">Link</a>") != NULL, "HTML has link");
        TEST_ASSERT(strstr(html, "&amp; &lt;tag&gt;") != NULL, "HTML escapes entities");
        TEST_ASSERT(strstr(html, "type=\"checkbox\" disabled checked") != NULL, "HTML has checked task");
        TEST_ASSERT(strstr(html, "<blockquote>") != NULL, "HTML has blockquote");
        TEST_ASSERT(strstr(html, "<pre><code class=\"language-c\">") != NULL, "HTML has fenced code block");
        TEST_ASSERT(strstr(html, "<table>") != NULL && strstr(html, "<th>") != NULL, "HTML has table");
        TEST_ASSERT(strstr(html, "<hr>") != NULL, "HTML has horizontal rule");
        free(html);
    }
    remove(html_out);

    // 2. Built-in XML export
    const char *xml_out = "/tmp/test_mde_export.xml";
    ok = export_document(&buf, EXPORT_XML, xml_out, EXPORT_ENGINE_BUILTIN,
                         used, sizeof(used), err, sizeof(err));
    TEST_ASSERT(ok, "Built-in XML export succeeded");
    char *xml = read_entire_file(xml_out);
    TEST_ASSERT(xml != NULL, "Read exported XML file");
    if (xml) {
        TEST_ASSERT(strstr(xml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>") != NULL, "XML has declaration");
        TEST_ASSERT(strstr(xml, "<heading level=\"1\">Export Title</heading>") != NULL, "XML has heading");
        TEST_ASSERT(strstr(xml, "<bold>bold</bold>") != NULL, "XML has bold element");
        TEST_ASSERT(strstr(xml, "<task checked=\"true\">") != NULL, "XML has task element");
        TEST_ASSERT(strstr(xml, "<codeblock language=\"c\">") != NULL, "XML has codeblock");
        TEST_ASSERT(strstr(xml, "<table>") != NULL && strstr(xml, "<hrule/>") != NULL, "XML has table and hrule");
        TEST_ASSERT(strstr(xml, "</document>") != NULL, "XML closes root element");
        free(xml);
    }
    remove(xml_out);

    // 3. Built-in LaTeX export
    const char *tex_out = "/tmp/test_mde_export.tex";
    ok = export_document(&buf, EXPORT_LATEX, tex_out, EXPORT_ENGINE_BUILTIN,
                         used, sizeof(used), err, sizeof(err));
    TEST_ASSERT(ok, "Built-in LaTeX export succeeded");
    char *tex = read_entire_file(tex_out);
    TEST_ASSERT(tex != NULL, "Read exported LaTeX file");
    if (tex) {
        TEST_ASSERT(strstr(tex, "\\documentclass") != NULL, "LaTeX has documentclass");
        TEST_ASSERT(strstr(tex, "\\section{Export Title}") != NULL, "LaTeX has section");
        TEST_ASSERT(strstr(tex, "\\textbf{bold}") != NULL, "LaTeX has textbf");
        TEST_ASSERT(strstr(tex, "\\textit{italic}") != NULL, "LaTeX has textit");
        TEST_ASSERT(strstr(tex, "\\texttt{code}") != NULL, "LaTeX has texttt");
        TEST_ASSERT(strstr(tex, "\\href{https://example.com}{Link}") != NULL, "LaTeX has href");
        TEST_ASSERT(strstr(tex, "\\&") != NULL, "LaTeX escapes ampersand");
        TEST_ASSERT(strstr(tex, "\\begin{verbatim}") != NULL, "LaTeX has verbatim block");
        TEST_ASSERT(strstr(tex, "\\begin{tabular}") != NULL, "LaTeX has tabular");
        TEST_ASSERT(strstr(tex, "\\end{document}") != NULL, "LaTeX closes document");
        free(tex);
    }
    remove(tex_out);

    // 4. EPUB built-in rejection (requires pandoc)
    const char *epub_out = "/tmp/test_mde_export.epub";
    ok = export_document(&buf, EXPORT_EPUB, epub_out, EXPORT_ENGINE_BUILTIN,
                         used, sizeof(used), err, sizeof(err));
    TEST_ASSERT(!ok, "EPUB fails gracefully when built-in engine is forced");
    TEST_ASSERT(strstr(err, "pandoc") != NULL, "EPUB error mentions pandoc");

    // 5. Pandoc execution pipeline verified with mock pandoc script on PATH
    const char *mock_dir = "/tmp/mde_mock_bin";
    mkdir(mock_dir, 0755);
    const char *mock_pandoc = "/tmp/mde_mock_bin/pandoc";
    FILE *mf = fopen(mock_pandoc, "wb");
    if (mf) {
        fputs("#!/bin/sh\n"
              "out=\"\"\n"
              "while [ $# -gt 0 ]; do\n"
              "  if [ \"$1\" = \"-o\" ]; then shift; out=\"$1\"; fi\n"
              "  shift\n"
              "done\n"
              "echo \"MOCK_PANDOC_OUTPUT\" > \"$out\"\n", mf);
        fclose(mf);
        chmod(mock_pandoc, 0755);

        const char *old_path = getenv("PATH");
        char new_path[2048];
        snprintf(new_path, sizeof(new_path), "%s:%s", mock_dir, old_path ? old_path : "");
        setenv("PATH", new_path, 1);

        TEST_ASSERT(export_has_pandoc(), "export_has_pandoc detects pandoc on PATH");
        ok = export_document(&buf, EXPORT_EPUB, epub_out, EXPORT_ENGINE_AUTO,
                             used, sizeof(used), err, sizeof(err));
        TEST_ASSERT(ok, "EPUB export succeeds via pandoc");
        TEST_ASSERT(strcmp(used, "pandoc") == 0, "EPUB engine reported as pandoc");
        char *epub_txt = read_entire_file(epub_out);
        TEST_ASSERT(epub_txt != NULL && strstr(epub_txt, "MOCK_PANDOC_OUTPUT") != NULL,
                    "Pandoc wrote destination EPUB file");
        free(epub_txt);
        remove(epub_out);

        if (old_path) setenv("PATH", old_path, 1);
        remove(mock_pandoc);
    }

    buffer_free(&buf);
}

/** @brief Verify Nano-compatible whole-line cut/copy, mark toggle, go-to-line, and search. */
static void test_nano_editing_and_nav(void) {
    printf("--- Testing Nano-Compatible Line Cut/Copy, Mark Toggle, Goto & Search ---\n");
    Editor ed;
    editor_init(&ed, NULL);

    editor_paste_text(&ed, "Alpha line\nBeta line\nGamma line", 31);
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 3, "Buffer has 3 lines");

    // Mark toggle (M-A / ^^ / Ctrl+Space)
    ed.cursor_row = 0; ed.cursor_col = 0;
    editor_toggle_mark(&ed);
    TEST_ASSERT(ed.mark_active == true, "Toggle mark turns mark ON");
    editor_toggle_mark(&ed);
    TEST_ASSERT(ed.mark_active == false, "Toggle mark turns mark OFF");

    // Copy entire line (M-6 without selection)
    ed.cursor_row = 1; ed.cursor_col = 2;
    TEST_ASSERT(editor_copy_line(&ed), "editor_copy_line succeeded on middle line");
    TEST_ASSERT(ed.clipboard != NULL && strcmp(ed.clipboard, "Beta line\n") == 0,
                "Copied line includes trailing newline");
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 3, "Buffer unchanged after copy line");

    // Cut entire line (^K without selection) + Undo/Redo + Paste (^U)
    ed.cursor_row = 0; ed.cursor_col = 3;
    TEST_ASSERT(editor_cut_line(&ed), "editor_cut_line succeeded on row 0");
    TEST_ASSERT(ed.clipboard != NULL && strcmp(ed.clipboard, "Alpha line\n") == 0,
                "Cut line stored 'Alpha line\\n' in clipboard");
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 2, "Line count reduced to 2 after cutting row 0");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "Beta line") == 0, "Row 0 is now 'Beta line'");
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 0, "Cursor moved to (0, 0)");

    // Paste cut line back at row 0 (^U)
    TEST_ASSERT(editor_paste(&ed), "Paste cut line succeeded");
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 3, "Line count restored to 3 after paste");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "Alpha line") == 0, "Row 0 restored to 'Alpha line'");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 1), "Beta line") == 0, "Row 1 is 'Beta line'");

    // Undo paste and undo cut_line
    editor_undo(&ed);
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 2, "Undo paste left 2 lines");
    editor_undo(&ed);
    TEST_ASSERT(buffer_line_count(&ed.buffer) == 3, "Undo cut_line restored 3 lines");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "Alpha line") == 0, "Undo cut_line restored 'Alpha line'");

    // Cut last line (^K on last row)
    ed.cursor_row = 2; ed.cursor_col = 4;
    TEST_ASSERT(editor_cut_line(&ed), "editor_cut_line succeeded on last line");
    TEST_ASSERT(ed.clipboard != NULL && strcmp(ed.clipboard, "Gamma line") == 0,
                "Cut last line stored 'Gamma line'");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 2), "") == 0, "Last line cleared");
    editor_undo(&ed);
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 2), "Gamma line") == 0, "Undo restored last line");

    // Go to line (^/ / M-G)
    editor_goto_line(&ed, 2, 6);
    TEST_ASSERT(ed.cursor_row == 1 && ed.cursor_col == 5, "Goto line 2, col 6 -> (1, 5)");
    editor_goto_line(&ed, 999, 1);
    TEST_ASSERT(ed.cursor_row == 2 && ed.cursor_col == 0, "Goto clamps past end to last line");

    // Doc start / Doc end (M-\ and M-/)
    editor_move_doc_start(&ed);
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 0, "Doc start moves to (0, 0)");
    editor_move_doc_end(&ed);
    TEST_ASSERT(ed.cursor_row == 2 && ed.cursor_col == 10, "Doc end moves to end of last line");

    // Report cursor position (^C)
    editor_report_position(&ed);
    TEST_ASSERT(strstr(ed.status_msg, "Ln 3/3") != NULL, "Report position shows line 3/3");
    TEST_ASSERT(strstr(ed.status_msg, "100%") != NULL, "Report position shows 100%");

    // Forward & backward search (^W and ^Q)
    editor_move_doc_start(&ed);
    TEST_ASSERT(editor_search_find_next(&ed, "line", true), "Forward search found 'line'");
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 6, "First forward match on row 0 col 6");
    TEST_ASSERT(editor_search_find_next(&ed, "line", true), "Second forward search found 'line'");
    TEST_ASSERT(ed.cursor_row == 1 && ed.cursor_col == 5, "Second forward match on row 1 col 5");
    TEST_ASSERT(editor_search_find_next(&ed, "line", false), "Backward search found previous 'line'");
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 6, "Backward match returned to row 0 col 6");
    TEST_ASSERT(editor_search_find_next(&ed, "Alpha", true), "Wrap-around forward search finds 'Alpha' at (0, 0)");
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 0, "Wrapped match is at (0, 0)");

    editor_free(&ed);
}

/** @brief Write a string to a file on disk for test fixture setup. */
static void write_text_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

/** @brief Count non-overlapping occurrences of needle in haystack. */
static size_t count_occurrences(const char *haystack, const char *needle) {
    if (!haystack || !needle || !*needle) return 0;
    size_t count = 0;
    size_t nlen = strlen(needle);
    const char *p = haystack;
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += nlen;
    }
    return count;
}

/** @brief Verify hypertext file linking, navigation history, unsaved-change guards, and multi-file export. */
static void test_hypertext_linking_and_export(void) {
    printf("--- Testing Hypertext File Linking & Multi-File Linked Export ---\n");

    const char *root_dir = "/tmp/mde_ht_test";
    const char *sub_dir  = "/tmp/mde_ht_test/chapters";
    mkdir(root_dir, 0755);
    mkdir(sub_dir, 0755);

    const char *index_path = "/tmp/mde_ht_test/index.md";
    const char *ch1_path   = "/tmp/mde_ht_test/chapters/ch1.md";
    const char *ch2_path   = "/tmp/mde_ht_test/chapters/ch2.md";
    const char *new_path   = "/tmp/mde_ht_test/chapters/new_note.md";
    remove(new_path);

    write_text_file(index_path,
        "# Book Overview\n"
        "\n"
        "Start with [Chapter One](chapters/ch1.md) or jump to [Details](chapters/ch1.md#deep-dive).\n");

    write_text_file(ch1_path,
        "# Chapter One\n"
        "\n"
        "Intro paragraph.\n"
        "\n"
        "## Deep Dive\n"
        "\n"
        "Continue to [Chapter Two](ch2.md).\n");

    write_text_file(ch2_path,
        "# Chapter Two\n"
        "\n"
        "Final chapter. Return to [Book Overview](../index.md) or visit [Web](https://example.org).\n");

    // 1. Relative path computation
    char rel[256];
    TEST_ASSERT(editor_make_relative_path(index_path, ch1_path, rel, sizeof(rel)) &&
                strcmp(rel, "chapters/ch1.md") == 0,
                "Relative path to subdirectory is 'chapters/ch1.md'");
    TEST_ASSERT(editor_make_relative_path(ch1_path, index_path, rel, sizeof(rel)) &&
                strcmp(rel, "../index.md") == 0,
                "Relative path to parent directory is '../index.md'");
    TEST_ASSERT(editor_make_relative_path(ch1_path, ch2_path, rel, sizeof(rel)) &&
                strcmp(rel, "ch2.md") == 0,
                "Relative path to sibling file is 'ch2.md'");

    // 2. Editor link insertion, following, #anchor jump & history back
    Editor ed;
    editor_init(&ed, index_path);
    ed.cursor_row = 2;
    ed.cursor_col = 15; // Inside [Chapter One](chapters/ch1.md)

    char url[256] = {0}, anchor[128] = {0};
    TEST_ASSERT(editor_extract_link_at_cursor(&ed, url, sizeof(url), anchor, sizeof(anchor)),
                "Extracted link at cursor");
    TEST_ASSERT(strcmp(url, "chapters/ch1.md") == 0 && anchor[0] == '\0',
                "Extracted URL 'chapters/ch1.md' with empty anchor");

    // Verify editor_link_will_switch_file distinguishes cross-file vs same-file/external links
    char resolved_target[256] = {0};
    TEST_ASSERT(editor_link_will_switch_file(&ed, resolved_target, sizeof(resolved_target)) &&
                strcmp(resolved_target, ch1_path) == 0,
                "Cross-file link reports will_switch_file = true with resolved path");

    // Follow link to ch1.md
    TEST_ASSERT(editor_follow_link(&ed), "Followed link from index.md to chapters/ch1.md");
    TEST_ASSERT(ed.buffer.file_path && strcmp(ed.buffer.file_path, ch1_path) == 0,
                "Active file is now chapters/ch1.md");
    TEST_ASSERT(ed.link_history_count == 1, "Link history pushed previous location");

    // Follow link from ch1.md to ch2.md
    ed.cursor_row = 6; ed.cursor_col = 15;
    TEST_ASSERT(editor_follow_link(&ed), "Followed link from ch1.md to ch2.md");
    TEST_ASSERT(ed.buffer.file_path && strcmp(ed.buffer.file_path, ch2_path) == 0,
                "Active file is now chapters/ch2.md");
    TEST_ASSERT(ed.link_history_count == 2, "Link history depth is 2");

    // External URL on ch2.md row 2 should NOT trigger file switch / save prompt
    ed.cursor_row = 2; ed.cursor_col = 70; // Inside [Web](https://example.org)
    TEST_ASSERT(!editor_link_will_switch_file(&ed, resolved_target, sizeof(resolved_target)),
                "External URL reports will_switch_file = false");

    // Same-file link (#heading or self path) should NOT trigger file switch / save prompt
    editor_move_doc_end(&ed);
    editor_smart_enter(&ed);
    editor_paste_text(&ed, "[Self](ch2.md#chapter-two) and [Local](#chapter-two)", 52);
    ed.cursor_col = 5;
    TEST_ASSERT(!editor_link_will_switch_file(&ed, resolved_target, sizeof(resolved_target)),
                "Same-file link reports will_switch_file = false");
    ed.cursor_col = 35;
    TEST_ASSERT(!editor_link_will_switch_file(&ed, resolved_target, sizeof(resolved_target)),
                "Pure #anchor link reports will_switch_file = false");
    ed.buffer.is_dirty = false;

    // Navigate back twice to index.md and verify cursor restoration
    TEST_ASSERT(editor_navigate_back(&ed), "Navigated back to ch1.md");
    TEST_ASSERT(ed.buffer.file_path && strcmp(ed.buffer.file_path, ch1_path) == 0,
                "Restored chapters/ch1.md");
    TEST_ASSERT(editor_navigate_back(&ed), "Navigated back to index.md");
    TEST_ASSERT(ed.buffer.file_path && strcmp(ed.buffer.file_path, index_path) == 0,
                "Restored index.md");
    TEST_ASSERT(ed.cursor_row == 2 && ed.cursor_col == 15,
                "Restored exact cursor position (2, 15) in index.md");

    // Follow link with #anchor fragment to ## Deep Dive in ch1.md
    ed.cursor_row = 2;
    ed.cursor_col = 55; // Inside [Details](chapters/ch1.md#deep-dive)
    TEST_ASSERT(editor_extract_link_at_cursor(&ed, url, sizeof(url), anchor, sizeof(anchor)) &&
                strcmp(url, "chapters/ch1.md") == 0 && strcmp(anchor, "deep-dive") == 0,
                "Extracted link with #deep-dive anchor");
    TEST_ASSERT(editor_follow_link(&ed), "Followed link with #deep-dive anchor");
    TEST_ASSERT(ed.cursor_row == 4, "Jumped directly to '## Deep Dive' heading on row 4");

    // Insert file link via editor_insert_file_link (derives H1 title & relative path)
    editor_navigate_back(&ed);
    editor_move_doc_end(&ed);
    editor_smart_enter(&ed);
    TEST_ASSERT(editor_insert_file_link(&ed, ch2_path), "Inserted file link to ch2.md");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, ed.cursor_row), "[Chapter Two](chapters/ch2.md)") == 0,
                "Inserted link used H1 title and relative path");

    // 3. Recursive linked-file discovery with cycle prevention
    Buffer root_buf;
    buffer_init(&root_buf);
    TEST_ASSERT(buffer_load(&root_buf, index_path), "Loaded index.md into Buffer");
    TEST_ASSERT(export_count_linked_files(&root_buf) == 2,
                "Discovered 2 unique linked files (ch1.md + ch2.md, cycle to index.md prevented)");

    char used[32] = {0};
    char err[256] = {0};
    size_t linked_cnt = 0;

    // 4. Built-in HTML5 export includes all linked files & rewrites links
    const char *html_out = "/tmp/mde_ht_test/book.html";
    bool ok = export_document_ex(&root_buf, EXPORT_HTML, html_out, EXPORT_ENGINE_BUILTIN,
                                 true, &linked_cnt, used, sizeof(used), err, sizeof(err));
    TEST_ASSERT(ok && linked_cnt == 2, "HTML multi-file export succeeded with 2 linked files");
    char *html = read_entire_file(html_out);
    TEST_ASSERT(html != NULL, "Read multi-file HTML");
    if (html) {
        TEST_ASSERT(strstr(html, "Book Overview") != NULL, "HTML contains root file content");
        TEST_ASSERT(strstr(html, "Chapter One") != NULL, "HTML contains linked ch1.md content");
        TEST_ASSERT(strstr(html, "Chapter Two") != NULL, "HTML contains transitively linked ch2.md content");
        TEST_ASSERT(strstr(html, "<section id=\"mde-doc-1\">") != NULL, "HTML wraps linked doc 1 in section");
        TEST_ASSERT(strstr(html, "<a href=\"#mde-doc-1\">Chapter One</a>") != NULL,
                    "HTML rewrites chapters/ch1.md link to #mde-doc-1");
        TEST_ASSERT(strstr(html, "<a href=\"#deep-dive\">Details</a>") != NULL,
                    "HTML rewrites fragment link to #deep-dive");
        TEST_ASSERT(strstr(html, "<a href=\"#mde-doc-0\">Book Overview</a>") != NULL,
                    "HTML rewrites back-link ../index.md to #mde-doc-0");
        TEST_ASSERT(strstr(html, "<a href=\"https://example.org\">Web</a>") != NULL,
                    "HTML preserves external URLs unchanged");
        free(html);
    }
    remove(html_out);

    // 5. Built-in XML export includes all linked files
    const char *xml_out = "/tmp/mde_ht_test/book.xml";
    ok = export_document_ex(&root_buf, EXPORT_XML, xml_out, EXPORT_ENGINE_BUILTIN,
                            true, &linked_cnt, used, sizeof(used), err, sizeof(err));
    TEST_ASSERT(ok && linked_cnt == 2, "XML multi-file export succeeded with 2 linked files");
    char *xml = read_entire_file(xml_out);
    if (xml) {
        TEST_ASSERT(strstr(xml, "<linked-document id=\"mde-doc-1\"") != NULL, "XML includes linked-document 1");
        TEST_ASSERT(strstr(xml, "<linked-document id=\"mde-doc-2\"") != NULL, "XML includes linked-document 2");
        TEST_ASSERT(strstr(xml, "Chapter Two") != NULL, "XML includes ch2 content");
        TEST_ASSERT(strstr(xml, "<link url=\"#mde-doc-1\">Chapter One</link>") != NULL,
                    "XML rewrites internal file links");
        free(xml);
    }
    remove(xml_out);

    // 6. Built-in LaTeX export combines all linked files into ONE unified LaTeX document
    const char *tex_out = "/tmp/mde_ht_test/book.tex";
    ok = export_document_ex(&root_buf, EXPORT_LATEX, tex_out, EXPORT_ENGINE_BUILTIN,
                            true, &linked_cnt, used, sizeof(used), err, sizeof(err));
    TEST_ASSERT(ok && linked_cnt == 2, "LaTeX multi-file export succeeded with 2 linked files");
    char *tex = read_entire_file(tex_out);
    if (tex) {
        TEST_ASSERT(count_occurrences(tex, "\\documentclass") == 1, "LaTeX has exactly ONE \\documentclass");
        TEST_ASSERT(count_occurrences(tex, "\\begin{document}") == 1, "LaTeX has exactly ONE \\begin{document}");
        TEST_ASSERT(count_occurrences(tex, "\\end{document}") == 1, "LaTeX has exactly ONE \\end{document}");
        TEST_ASSERT(strstr(tex, "\\section{Book Overview}") != NULL, "LaTeX includes root section");
        TEST_ASSERT(strstr(tex, "\\section{Chapter One}") != NULL, "LaTeX includes linked ch1 section");
        TEST_ASSERT(strstr(tex, "\\section{Chapter Two}") != NULL, "LaTeX includes linked ch2 section");
        TEST_ASSERT(strstr(tex, "\\hypertarget{mde-doc-1}{}") != NULL, "LaTeX emits hypertarget for ch1");
        TEST_ASSERT(strstr(tex, "\\hyperlink{mde-doc-1}{Chapter One}") != NULL,
                    "LaTeX converts internal link to \\hyperlink");
        TEST_ASSERT(strstr(tex, "\\href{https://example.org}{Web}") != NULL,
                    "LaTeX keeps external link as \\href");
        free(tex);
    }
    remove(tex_out);

    // 7. Excluding linked files (include_linked = false)
    ok = export_document_ex(&root_buf, EXPORT_LATEX, tex_out, EXPORT_ENGINE_BUILTIN,
                            false, &linked_cnt, used, sizeof(used), err, sizeof(err));
    TEST_ASSERT(ok && linked_cnt == 0, "Export with include_linked=false reports 0 linked files");
    tex = read_entire_file(tex_out);
    if (tex) {
        TEST_ASSERT(strstr(tex, "Chapter One") != NULL && strstr(tex, "\\section{Chapter Two}") == NULL,
                    "Excluded linked files are not bundled when include_linked=false");
        free(tex);
    }
    remove(tex_out);

    // 8. Pandoc multi-file export (EPUB & LaTeX) via mock pandoc verifying all 3 input docs passed
    const char *mock_dir = "/tmp/mde_ht_mock_bin";
    mkdir(mock_dir, 0755);
    const char *mock_pandoc = "/tmp/mde_ht_mock_bin/pandoc";
    write_text_file(mock_pandoc,
        "#!/bin/sh\n"
        "out=\"\"\n"
        "inputs=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  case \"$1\" in\n"
        "    -o) shift; out=\"$1\" ;;\n"
        "    -f|-t|--metadata|--resource-path) shift ;;\n"
        "    -s) ;;\n"
        "    *) inputs=\"$inputs $1\" ;;\n"
        "  esac\n"
        "  shift\n"
        "done\n"
        "cat $inputs > \"$out\"\n");
    chmod(mock_pandoc, 0755);

    const char *old_path = getenv("PATH");
    char mock_path_env[2048];
    snprintf(mock_path_env, sizeof(mock_path_env), "%s:%s", mock_dir, old_path ? old_path : "");
    setenv("PATH", mock_path_env, 1);

    const char *epub_out = "/tmp/mde_ht_test/book.epub";
    ok = export_document_ex(&root_buf, EXPORT_EPUB, epub_out, EXPORT_ENGINE_AUTO,
                            true, &linked_cnt, used, sizeof(used), err, sizeof(err));
    TEST_ASSERT(ok && linked_cnt == 2 && strcmp(used, "pandoc") == 0,
                "EPUB multi-file export via pandoc included all 2 linked files");
    char *epub_concat = read_entire_file(epub_out);
    if (epub_concat) {
        TEST_ASSERT(strstr(epub_concat, "# Book Overview {#mde-doc-0}") != NULL,
                    "Pandoc input 0 has anchor {#mde-doc-0}");
        TEST_ASSERT(strstr(epub_concat, "# Chapter One {#mde-doc-1}") != NULL,
                    "Pandoc input 1 has anchor {#mde-doc-1}");
        TEST_ASSERT(strstr(epub_concat, "# Chapter Two {#mde-doc-2}") != NULL,
                    "Pandoc input 2 has anchor {#mde-doc-2}");
        TEST_ASSERT(strstr(epub_concat, "[Chapter One](#mde-doc-1)") != NULL,
                    "Pandoc input rewrote ch1.md link to #mde-doc-1");
        TEST_ASSERT(strstr(epub_concat, "[Book Overview](#mde-doc-0)") != NULL,
                    "Pandoc input rewrote ../index.md link to #mde-doc-0");
        free(epub_concat);
    }
    remove(epub_out);

    if (old_path) setenv("PATH", old_path, 1);
    remove(mock_pandoc);
    remove(index_path);
    remove(ch1_path);
    remove(ch2_path);
    buffer_free(&root_buf);
    editor_free(&ed);
}

/** @brief Verify atomic saves (symlink & permission preservation) and exact undo dirty tracking. */
static void test_atomic_save_and_exact_dirty(void) {
    printf("--- Testing Atomic Saves (Symlinks/Permissions) & Exact Undo Dirty State ---\n");

    const char *dir = "/tmp/mde_atomic_test";
    mkdir(dir, 0755);
    const char *real_file = "/tmp/mde_atomic_test/real.md";
    const char *sym_file  = "/tmp/mde_atomic_test/link.md";
    remove(sym_file);
    remove(real_file);

    write_text_file(real_file, "Initial line\n");
    chmod(real_file, 0640);
    int sl_ok = symlink(real_file, sym_file);
    TEST_ASSERT(sl_ok == 0, "Created test symlink");

    Buffer buf;
    buffer_init(&buf);
    TEST_ASSERT(buffer_load(&buf, sym_file), "Loaded buffer via symlink");
    TEST_ASSERT(!buf.is_dirty, "Freshly loaded buffer is clean");

    // Edit -> dirty -> undo back to saved state -> clean again!
    uint64_t g1 = buffer_next_group(&buf);
    buffer_insert_string(&buf, 0, 12, " edited", 7, g1);
    TEST_ASSERT(buf.is_dirty, "Buffer is dirty after edit");

    size_t urow = 0, ucol = 0;
    TEST_ASSERT(buffer_undo(&buf, &urow, &ucol), "Undo edit succeeded");
    TEST_ASSERT(!buf.is_dirty, "Undoing back to saved state clears is_dirty");

    TEST_ASSERT(buffer_redo(&buf, &urow, &ucol), "Redo edit succeeded");
    TEST_ASSERT(buf.is_dirty, "Redoing edit marks buffer dirty again");

    // Save via symlink: symlink stays intact, target updated, permissions 0640 preserved
    TEST_ASSERT(buffer_save(&buf, NULL), "Atomic save through symlink succeeded");
    TEST_ASSERT(!buf.is_dirty, "Buffer is clean after save");

    struct stat lst, tst;
    TEST_ASSERT(lstat(sym_file, &lst) == 0 && S_ISLNK(lst.st_mode),
                "Symlink itself was preserved (not replaced by regular file)");
    TEST_ASSERT(stat(real_file, &tst) == 0 && (tst.st_mode & 0777) == 0640,
                "Target file permissions (0640) were preserved across atomic rename");
    char *saved_txt = read_entire_file(real_file);
    TEST_ASSERT(saved_txt != NULL && strcmp(saved_txt, "Initial line edited\n") == 0,
                "Symlink target file contains updated content");
    free(saved_txt);

    // Undo past saved state, then branch with new edit: is_dirty must stay true even at same depth
    TEST_ASSERT(buffer_undo(&buf, &urow, &ucol), "Undo past saved state");
    TEST_ASSERT(buf.is_dirty, "Undoing past saved state marks buffer dirty");
    uint64_t g2 = buffer_next_group(&buf);
    buffer_insert_string(&buf, 0, 0, "X", 1, g2);
    TEST_ASSERT(buf.is_dirty, "Branched history at saved depth remains dirty");

    buffer_free(&buf);
    remove(sym_file);
    remove(real_file);
}

/** @brief Verify Search & Replace, Paragraph Reflow, and Word-by-Word Navigation. */
static void test_replace_reflow_and_word_nav(void) {
    printf("--- Testing Search & Replace, Paragraph Reflow & Word Navigation ---\n");
    Editor ed;
    editor_init(&ed, NULL);

    // 1. Word-by-word navigation
    editor_paste_text(&ed, "alpha beta_two, gamma\ndelta", 27);
    ed.cursor_row = 0; ed.cursor_col = 0;

    editor_move_word_right(&ed);
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 6, "Word right moved to 'beta_two'");
    editor_move_word_right(&ed);
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 14, "Word right moved to ','");
    editor_move_word_right(&ed);
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 16, "Word right moved to 'gamma'");
    editor_move_word_right(&ed);
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 21, "Word right moved to EOL");
    editor_move_word_right(&ed);
    TEST_ASSERT(ed.cursor_row == 1 && ed.cursor_col == 0, "Word right crossed to line 1");

    editor_move_word_left(&ed);
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 21, "Word left crossed back to line 0 EOL");
    editor_move_word_left(&ed);
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 16, "Word left moved to 'gamma'");
    editor_move_word_left(&ed);
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 14, "Word left moved to ','");
    editor_move_word_left(&ed);
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 6, "Word left moved to 'beta_two'");
    editor_free(&ed);

    // 2. Search & Replace (including replacement containing query and single-step undo)
    editor_init(&ed, NULL);
    editor_paste_text(&ed, "foo bar FOO baz foo", 19);
    size_t nrep = editor_replace_all(&ed, "foo", "foobar");
    TEST_ASSERT(nrep == 3, "editor_replace_all replaced all 3 case-insensitive matches");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "foobar bar foobar baz foobar") == 0,
                "All occurrences replaced cleanly without infinite loop");
    editor_undo(&ed);
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "foo bar FOO baz foo") == 0,
                "Single undo reverted entire Replace All atomically");

    // Stepwise replace current match
    TEST_ASSERT(editor_search_find_from(&ed, "baz", 0, 0, false), "Found 'baz' via find_from");
    TEST_ASSERT(editor_replace_current_match(&ed, "qux", 0), "Replaced single match 'baz' -> 'qux'");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "foo bar FOO qux foo") == 0,
                "Single match replacement updated buffer");
    editor_free(&ed);

    // 3. Paragraph Reflow (plain + blockquote + stops at headings)
    editor_init(&ed, NULL);
    editor_set_wrap_col(&ed, 25);
    editor_paste_text(&ed,
        "# Title\n"
        "Short\n"
        "lines that should\n"
        "reflow neatly together.\n"
        "\n"
        "> Quoted\n"
        "> paragraph lines here.", 89);

    ed.cursor_row = 1; ed.cursor_col = 0;
    TEST_ASSERT(editor_reflow_paragraph(&ed), "Reflowed plain paragraph");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 0), "# Title") == 0, "Heading untouched by reflow");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 1), "Short lines that should") == 0,
                "Reflow line 1 wrapped cleanly <= 25 cols");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 2), "reflow neatly together.") == 0,
                "Reflow line 2 wrapped cleanly <= 25 cols");

    // Single undo restores jagged original paragraph
    editor_undo(&ed);
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 1), "Short") == 0, "Undo restored original paragraph");

    // Blockquote reflow preserves "> " prefix
    ed.cursor_row = 5; ed.cursor_col = 2;
    TEST_ASSERT(editor_reflow_paragraph(&ed), "Reflowed blockquote paragraph");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 5), "> Quoted paragraph lines") == 0,
                "Blockquote reflow preserved '> ' prefix on line 1");
    TEST_ASSERT(strcmp(buffer_line(&ed.buffer, 6), "> here.") == 0,
                "Blockquote reflow preserved '> ' prefix on continuation line");

    editor_free(&ed);
}

/** @brief Verify aligned box-drawn tables and directional link indicators in preview renderer. */
static void test_renderer_tables_and_links(void) {
    printf("--- Testing Preview Box Tables & Directional Link Indicators ---\n");
    Buffer buf;
    buffer_init(&buf);

    const char *lines[] = {
        "| Key | Action |",
        "| --- | --- |",
        "| ^G | Help |",
        "| ^X | Exit |",
        "",
        "See [Docs](https://example.com), [Ch1](ch1.md), and [Top](#top)."
    };
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
        if (i > 0) buffer_split_line(&buf, i - 1, buffer_line_len(&buf, i - 1), 0);
        buffer_replace_line(&buf, i, lines[i], strlen(lines[i]), 0);
    }

    RenderedDoc doc;
    rendered_doc_init(&doc);
    render_document(&buf, 60, &doc);

    bool has_top = false, has_mid = false, has_bot = false;
    bool has_ext = false, has_loc = false, has_anc = false;
    for (size_t i = 0; i < doc.count; ++i) {
        for (size_t c = 0; c < doc.lines[i].chunk_count; ++c) {
            const char *t = doc.lines[i].chunks[c].text;
            if (strstr(t, "┌") && strstr(t, "┬") && strstr(t, "┐")) has_top = true;
            if (strstr(t, "├") && strstr(t, "┼") && strstr(t, "┤")) has_mid = true;
            if (strstr(t, "└") && strstr(t, "┴") && strstr(t, "┘")) has_bot = true;
            if (strstr(t, "↗")) has_ext = true;
            if (strstr(t, "→")) has_loc = true;
            if (strstr(t, "↓")) has_anc = true;
        }
    }

    TEST_ASSERT(has_top, "Table preview has top box border ┌─┬─┐");
    TEST_ASSERT(has_mid, "Table preview has header separator ├─┼─┤");
    TEST_ASSERT(has_bot, "Table preview has bottom box border └─┴─┘");
    TEST_ASSERT(has_ext, "External link displays ↗ indicator");
    TEST_ASSERT(has_loc, "Local file link displays → indicator");
    TEST_ASSERT(has_anc, "Same-document #anchor link displays ↓ indicator");

    // Verify cursor mapping across rendered link
    int vcol_end = renderer_get_cursor_vcol(&buf, &doc, 5, buffer_line_len(&buf, 5));
    TEST_ASSERT(vcol_end > 10 && vcol_end < (int)buffer_line_len(&buf, 5),
                "Preview cursor vcol strips raw URL length and accounts for indicators");

    rendered_doc_free(&doc);
    buffer_free(&buf);
}

/** @brief Verify shortcut hint row sizing, file dialog safety checks, and unsaved/overwrite guards. */
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

    // Overwrite-guard path existence checks (used by Write Out confirmation)
    const char *tmp_exist = "/tmp/mde_overwrite_check.md";
    const char *tmp_missing = "/tmp/mde_definitely_does_not_exist_12345.md";
    write_text_file(tmp_exist, "# Existing\n");
    remove(tmp_missing);

    bool is_dir = true;
    TEST_ASSERT(ui_path_exists(tmp_exist, &is_dir) && !is_dir,
                "ui_path_exists detects existing regular file");
    TEST_ASSERT(!ui_path_exists(tmp_missing, &is_dir),
                "ui_path_exists returns false for non-existent file");
    TEST_ASSERT(ui_path_exists("/tmp", &is_dir) && is_dir,
                "ui_path_exists identifies existing directory");
    TEST_ASSERT(!ui_path_exists(NULL, NULL), "ui_path_exists handles NULL path");
    TEST_ASSERT(!ui_confirm_overwrite(NULL, tmp_exist), "ui_confirm_overwrite rejects NULL editor");
    TEST_ASSERT(!ui_confirm_overwrite(&ed, NULL), "ui_confirm_overwrite rejects NULL path");
    TEST_ASSERT(!editor_has_unsaved_changes(&ed), "Fresh editor buffer has no unsaved changes");
    editor_insert_char(&ed, "x", 1);
    TEST_ASSERT(editor_has_unsaved_changes(&ed), "Modified editor buffer reports unsaved changes");
    TEST_ASSERT(editor_save(&ed, tmp_exist), "Saving modified buffer succeeds");
    TEST_ASSERT(!editor_has_unsaved_changes(&ed), "Saved buffer clears unsaved changes flag");
    remove(tmp_exist);

    editor_free(&ed);
}

/** @brief Run all unit test suites and return 0 on 100% pass, 1 on any failure. */
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
    test_export_subsystem();
    test_nano_editing_and_nav();
    test_hypertext_linking_and_export();
    test_atomic_save_and_exact_dirty();
    test_replace_reflow_and_word_nav();
    test_renderer_tables_and_links();
    test_shortcut_hints_and_dialog();

    printf("==========================================\n");
    printf("Test Results: %d / %d tests passed (%.1f%%)\n",
           g_tests_passed, g_tests_run,
           g_tests_run > 0 ? (100.0 * g_tests_passed / g_tests_run) : 0.0);
    printf("==========================================\n");

    return (g_tests_run == g_tests_passed) ? 0 : 1;
}
