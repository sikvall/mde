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
#include <limits.h>

#include "../src/version.h"
#include "../src/utf8.h"
#include "../src/buffer.h"
#include "../src/markdown.h"
#include "../src/renderer.h"
#include "../src/theme.h"
#include "../src/editor.h"
#include "../src/export.h"
#include "../src/epub.h"
#include "../src/ui.h"
#include "../src/config.h"
#include <zip.h>

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
    TEST_ASSERT(strcmp(MDE_VERSION_STRING, "0.1.2") == 0, "Version string is '0.1.2'");
    TEST_ASSERT(MDE_VERSION_MAJOR == 0 && MDE_VERSION_MINOR == 1 && MDE_VERSION_PATCH == 2,
                "Numeric version macros match 0.1.2");
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

    // 4. Native EPUB built-in export
    const char *epub_out = "/tmp/test_mde_export.epub";
    ok = export_document(&buf, EXPORT_EPUB, epub_out, EXPORT_ENGINE_BUILTIN,
                         used, sizeof(used), err, sizeof(err));
    TEST_ASSERT(ok, "EPUB export succeeds with native built-in engine");
    TEST_ASSERT(strcmp(used, "built-in") == 0, "EPUB engine reported as built-in");
    struct stat epub_st;
    TEST_ASSERT(stat(epub_out, &epub_st) == 0 && epub_st.st_size > 0, "Native EPUB file generated on disk");
    remove(epub_out);

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

        // 5a. EXPORT_ENGINE_AUTO uses native built-in engine even when pandoc is on PATH
        ok = export_document(&buf, EXPORT_EPUB, epub_out, EXPORT_ENGINE_AUTO,
                             used, sizeof(used), err, sizeof(err));
        TEST_ASSERT(ok, "EPUB export succeeds with EXPORT_ENGINE_AUTO");
        TEST_ASSERT(strcmp(used, "built-in") == 0, "EXPORT_ENGINE_AUTO prefers native built-in converter");
        remove(epub_out);

        // 5b. EXPORT_ENGINE_PANDOC explicitly routes to pandoc
        ok = export_document(&buf, EXPORT_EPUB, epub_out, EXPORT_ENGINE_PANDOC,
                             used, sizeof(used), err, sizeof(err));
        TEST_ASSERT(ok, "EPUB export succeeds via pandoc when EXPORT_ENGINE_PANDOC selected");
        TEST_ASSERT(strcmp(used, "pandoc") == 0, "EPUB engine reported as pandoc");
        char *epub_txt = read_entire_file(epub_out);
        TEST_ASSERT(epub_txt != NULL && strstr(epub_txt, "MOCK_PANDOC_OUTPUT") != NULL,
                    "Pandoc wrote destination EPUB file");
        free(epub_txt);
        remove(epub_out);

        if (old_path) setenv("PATH", old_path, 1);
        remove(mock_pandoc);
    }

    // 6. Complete verification of ALL export formats WITHOUT pandoc on PATH
    const char *saved_path = getenv("PATH");
    const char *empty_bin_dir = "/tmp/mde_test_no_pandoc";
    mkdir(empty_bin_dir, 0755);
    setenv("PATH", empty_bin_dir, 1);

    TEST_ASSERT(!export_has_pandoc(), "export_has_pandoc() reports false when pandoc is absent from PATH");

    char used_no_pan[32] = {0};
    char err_no_pan[256] = {0};

    // 6a. HTML export without pandoc
    const char *html_no_pan = "/tmp/test_no_pandoc.html";
    ok = export_document(&buf, EXPORT_HTML, html_no_pan, EXPORT_ENGINE_AUTO,
                         used_no_pan, sizeof(used_no_pan), err_no_pan, sizeof(err_no_pan));
    TEST_ASSERT(ok, "HTML export succeeds without pandoc");
    TEST_ASSERT(strcmp(used_no_pan, "built-in") == 0, "HTML engine reported as built-in without pandoc");
    char *h_out = read_entire_file(html_no_pan);
    TEST_ASSERT(h_out != NULL && strstr(h_out, "<!DOCTYPE html>") != NULL, "HTML output without pandoc is valid HTML5");
    free(h_out);
    remove(html_no_pan);

    // 6b. XML export without pandoc
    const char *xml_no_pan = "/tmp/test_no_pandoc.xml";
    ok = export_document(&buf, EXPORT_XML, xml_no_pan, EXPORT_ENGINE_AUTO,
                         used_no_pan, sizeof(used_no_pan), err_no_pan, sizeof(err_no_pan));
    TEST_ASSERT(ok, "XML export succeeds without pandoc");
    TEST_ASSERT(strcmp(used_no_pan, "built-in") == 0, "XML engine reported as built-in without pandoc");
    char *x_out = read_entire_file(xml_no_pan);
    TEST_ASSERT(x_out != NULL && strstr(x_out, "<?xml") != NULL, "XML output without pandoc has XML declaration");
    free(x_out);
    remove(xml_no_pan);

    // 6c. EPUB export without pandoc
    const char *epub_no_pan = "/tmp/test_no_pandoc.epub";
    ok = export_document(&buf, EXPORT_EPUB, epub_no_pan, EXPORT_ENGINE_AUTO,
                         used_no_pan, sizeof(used_no_pan), err_no_pan, sizeof(err_no_pan));
    TEST_ASSERT(ok, "EPUB export succeeds without pandoc");
    TEST_ASSERT(strcmp(used_no_pan, "built-in") == 0, "EPUB engine reported as built-in without pandoc");
    int ep_err = 0;
    struct zip *ep_za = zip_open(epub_no_pan, ZIP_RDONLY, &ep_err);
    TEST_ASSERT(ep_za != NULL, "EPUB output without pandoc is a valid zip archive");
    if (ep_za) {
        const char *mname = zip_get_name(ep_za, 0, 0);
        TEST_ASSERT(mname && strcmp(mname, "mimetype") == 0, "First entry in EPUB without pandoc is mimetype");
        zip_close(ep_za);
    }
    remove(epub_no_pan);

    // 6d. LaTeX export without pandoc
    const char *tex_no_pan = "/tmp/test_no_pandoc.tex";
    ok = export_document(&buf, EXPORT_LATEX, tex_no_pan, EXPORT_ENGINE_AUTO,
                         used_no_pan, sizeof(used_no_pan), err_no_pan, sizeof(err_no_pan));
    TEST_ASSERT(ok, "LaTeX export succeeds without pandoc");
    TEST_ASSERT(strcmp(used_no_pan, "built-in") == 0, "LaTeX engine reported as built-in without pandoc");
    char *t_out = read_entire_file(tex_no_pan);
    TEST_ASSERT(t_out != NULL && strstr(t_out, "\\begin{document}") != NULL, "LaTeX output without pandoc is valid");
    free(t_out);
    remove(tex_no_pan);

    // 6e. Multi-file linked export without pandoc
    size_t multi_linked = 0;
    const char *multi_epub = "/tmp/test_no_pandoc_multi.epub";
    Buffer root_test;
    buffer_init(&root_test);
    buffer_load(&root_test, "examples/demo.md");
    ok = export_document_ex(&root_test, EXPORT_EPUB, multi_epub, EXPORT_ENGINE_AUTO,
                            true, &multi_linked, used_no_pan, sizeof(used_no_pan), err_no_pan, sizeof(err_no_pan));
    TEST_ASSERT(ok, "Multi-file EPUB export succeeds without pandoc on demo.md");
    TEST_ASSERT(strcmp(used_no_pan, "built-in") == 0, "Multi-file EPUB engine reported as built-in without pandoc");
    remove(multi_epub);
    buffer_free(&root_test);

    // Restore PATH
    if (saved_path) setenv("PATH", saved_path, 1);
    rmdir(empty_bin_dir);

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
    ok = export_document_ex(&root_buf, EXPORT_EPUB, epub_out, EXPORT_ENGINE_PANDOC,
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
    TEST_ASSERT(ui_get_shortcut_hint_rows(24) == 4, "Standard 24-row terminal gets 4 shortcut hint rows");
    TEST_ASSERT(ui_get_shortcut_hint_rows(25) == 4, "25-row terminal gets 4 shortcut hint rows");
    TEST_ASSERT(ui_get_shortcut_hint_rows(28) == 4, "28-row terminal gets 4 shortcut hint rows");
    TEST_ASSERT(ui_get_shortcut_hint_rows(40) == 4, "40-row terminal gets 4 shortcut hint rows");
    TEST_ASSERT(ui_get_shortcut_hint_rows(20) == 3, "20-row terminal gets 3 shortcut hint rows");
    TEST_ASSERT(ui_get_shortcut_hint_rows(15) == 2, "Small 15-row terminal gets 2 shortcut hint rows");
    TEST_ASSERT(ui_get_shortcut_hint_rows(10) == 1, "10-row terminal gets 1 shortcut hint row");

    // File dialog safe parameter validation
    Editor ed;
    editor_init(&ed, NULL);

    TEST_ASSERT(ed.show_keybindings == true, "Keybindings visible by default");
    editor_toggle_keybindings(&ed);
    TEST_ASSERT(ed.show_keybindings == false, "Keybindings toggled to hidden");
    editor_toggle_keybindings(&ed);
    TEST_ASSERT(ed.show_keybindings == true, "Keybindings toggled back to visible");

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

/** @brief Verify Markdown extensions: footnotes, nested lists, reference links, autolinks, table alignment. */
static void test_markdown_extensions(void) {
    printf("--- Testing Markdown Extensions (v0.1.1) ---\n");

    bool in_code = false;
    char lang[32] = {0};
    MdParsedLine pl;

    // 1. Footnotes
    const char *fn_line = "A paragraph with [^note1] reference.";
    md_parse_line(fn_line, strlen(fn_line), &in_code, lang, sizeof(lang), &pl);
    bool found_fn_ref = false;
    for (size_t s = 0; s < pl.span_count; ++s) {
        if (pl.spans[s].type == MD_FOOTNOTE_REF) {
            found_fn_ref = true;
            size_t slen = pl.spans[s].end - pl.spans[s].start;
            TEST_ASSERT(strncmp(fn_line + pl.spans[s].start, "[^note1]", slen) == 0,
                        "Footnote ref span matches [^note1]");
            break;
        }
    }
    TEST_ASSERT(found_fn_ref, "Found MD_FOOTNOTE_REF span");
    md_parsed_line_free(&pl);

    const char *fn_def = "[^note1]: Explanation of footnote";
    md_parse_line(fn_def, strlen(fn_def), &in_code, lang, sizeof(lang), &pl);
    TEST_ASSERT(pl.is_footnote_def, "Line identified as footnote definition");
    TEST_ASSERT(pl.span_count >= 1 && pl.spans[0].type == MD_FOOTNOTE_DEF,
                "First span of footnote definition is MD_FOOTNOTE_DEF");
    md_parsed_line_free(&pl);

    // 2. Nested Lists
    const char *list_l2 = "  - Level two item";
    md_parse_line(list_l2, strlen(list_l2), &in_code, lang, sizeof(lang), &pl);
    TEST_ASSERT(pl.is_list_item, "Level 2 item recognized as list item");
    TEST_ASSERT(pl.list_indent == 2, "Level 2 item has list_indent == 2");
    md_parsed_line_free(&pl);

    const char *list_l3 = "    * Level three item";
    md_parse_line(list_l3, strlen(list_l3), &in_code, lang, sizeof(lang), &pl);
    TEST_ASSERT(pl.is_list_item, "Level 3 item recognized as list item");
    TEST_ASSERT(pl.list_indent == 4, "Level 3 item has list_indent == 4");
    md_parsed_line_free(&pl);

    Buffer list_buf;
    buffer_init(&list_buf);
    buffer_replace_line(&list_buf, 0, "- L1", 4, 0);
    buffer_split_line(&list_buf, 0, 4, 0);
    buffer_replace_line(&list_buf, 1, "  - L2", 6, 0);
    buffer_split_line(&list_buf, 1, 6, 0);
    buffer_replace_line(&list_buf, 2, "    * L3", 8, 0);

    RenderedDoc list_doc;
    rendered_doc_init(&list_doc);
    render_document(&list_buf, 80, &list_doc);
    TEST_ASSERT(list_doc.count >= 3, "Rendered at least 3 list lines");
    bool found_bullet_circle = false;
    bool found_bullet_square = false;
    for (size_t l = 0; l < list_doc.count; ++l) {
        for (size_t c = 0; c < list_doc.lines[l].chunk_count; ++c) {
            RenderChunk *rc = &list_doc.lines[l].chunks[c];
            if (strstr(rc->text, "◦")) found_bullet_circle = true;
            if (strstr(rc->text, "▪")) found_bullet_square = true;
        }
    }
    TEST_ASSERT(found_bullet_circle, "Preview cycles bullet to ◦ for level 2");
    TEST_ASSERT(found_bullet_square, "Preview cycles bullet to ▪ for level 3");
    rendered_doc_free(&list_doc);
    buffer_free(&list_buf);

    // 3. Reference-style links
    const char *ref_inline = "Link to [Search Engine][se1] now.";
    md_parse_line(ref_inline, strlen(ref_inline), &in_code, lang, sizeof(lang), &pl);
    bool found_ref_label = false;
    bool found_ref_target = false;
    for (size_t s = 0; s < pl.span_count; ++s) {
        if (pl.spans[s].type == MD_LINK_TEXT) {
            found_ref_label = true;
            TEST_ASSERT(strncmp(ref_inline + pl.spans[s].start, "Search Engine", 13) == 0,
                        "Reference link label text matches");
        }
        if (pl.spans[s].type == MD_LINK_URL) {
            found_ref_target = true;
            TEST_ASSERT(strncmp(ref_inline + pl.spans[s].start, "se1", 3) == 0,
                        "Reference link target ref matches");
        }
    }
    TEST_ASSERT(found_ref_label && found_ref_target, "Reference inline link parsed correctly");
    md_parsed_line_free(&pl);

    const char *ref_def = "[se1]: https://duckduckgo.com";
    md_parse_line(ref_def, strlen(ref_def), &in_code, lang, sizeof(lang), &pl);
    bool found_def_url = false;
    for (size_t s = 0; s < pl.span_count; ++s) {
        if (pl.spans[s].type == MD_LINK_URL) {
            found_def_url = true;
            TEST_ASSERT(strncmp(ref_def + pl.spans[s].start, "https://duckduckgo.com", 22) == 0,
                        "Reference definition URL matches");
        }
    }
    TEST_ASSERT(found_def_url, "Reference definition parsed correctly");
    md_parsed_line_free(&pl);

    // 4. Autolink bare URLs
    const char *auto_line = "Visit https://kernel.org and http://example.com for info";
    md_parse_line(auto_line, strlen(auto_line), &in_code, lang, sizeof(lang), &pl);
    int auto_count = 0;
    for (size_t s = 0; s < pl.span_count; ++s) {
        if (pl.spans[s].type == MD_AUTO_LINK) auto_count++;
    }
    TEST_ASSERT(auto_count == 2, "Found both https:// and http:// bare autolinks");
    md_parsed_line_free(&pl);

    // 5. Table Column Alignment
    Buffer tbl_buf;
    buffer_init(&tbl_buf);
    buffer_replace_line(&tbl_buf, 0, "| Left | Center | Right |", 25, 0);
    buffer_split_line(&tbl_buf, 0, 25, 0);
    buffer_replace_line(&tbl_buf, 1, "| :--- | :---: | ---: |", 23, 0);
    buffer_split_line(&tbl_buf, 1, 23, 0);
    buffer_replace_line(&tbl_buf, 2, "| A | B | C |", 13, 0);

    RenderedDoc tbl_doc;
    rendered_doc_init(&tbl_doc);
    render_document(&tbl_buf, 80, &tbl_doc);
    TEST_ASSERT(tbl_doc.count >= 5, "Table renders box borders and rows");
    rendered_doc_free(&tbl_doc);
    buffer_free(&tbl_buf);

    // 6. View-bound range rendering (large document optimization)
    Buffer range_buf;
    buffer_init(&range_buf);
    for (size_t i = 0; i < 30; ++i) {
        char line_str[64];
        if (i == 4) snprintf(line_str, sizeof(line_str), "```c");
        else if (i == 10) snprintf(line_str, sizeof(line_str), "```");
        else snprintf(line_str, sizeof(line_str), "Line %zu content", i);
        if (i > 0) buffer_split_line(&range_buf, i - 1, buffer_line_len(&range_buf, i - 1), 0);
        buffer_replace_line(&range_buf, i, line_str, strlen(line_str), 0);
    }
    RenderedDoc range_doc;
    rendered_doc_init(&range_doc);
    render_document_range(&range_buf, 80, 6, 9, &range_doc);
    TEST_ASSERT(range_doc.count >= 3, "Range rendering produced lines for [6, 9)");
    bool found_code_in_range = false;
    for (size_t l = 0; l < range_doc.count; ++l) {
        TEST_ASSERT(range_doc.lines[l].source_row >= 6 && range_doc.lines[l].source_row < 9,
                    "Rendered lines within requested source row slice");
        for (size_t c = 0; c < range_doc.lines[l].chunk_count; ++c) {
            if (range_doc.lines[l].chunks[c].attr & COLOR_PAIR(CP_CODE_BLOCK)) {
                found_code_in_range = true;
            }
        }
    }
    TEST_ASSERT(found_code_in_range, "Range rendering accurately inherited code-fence state");
    rendered_doc_free(&range_doc);
    buffer_free(&range_buf);
}

/** @brief Verify bracket/delimiter pair matching, document statistics, and dual-pane mouse routing. */
static void test_interactive_ux_and_navigation(void) {
    printf("--- Testing Interactive UX & Dual-Pane Navigation (6.5) ---\n");
    Editor ed;
    editor_init(&ed, NULL);

    // 1. Bracket and delimiter pair matching
    const char *doc_lines[] = {
        "function compute(alpha, [beta, gamma]) {",
        "    if (x != y) return { result: true };",
        "    return false;",
        "}",
        "Some **bold text** and ~~strike text~~ and `inline code` here.",
        "See <https://example.com> for docs.",
        "```c",
        "int value = 123;",
        "```",
        "Unmatched bracket (test only"
    };

    for (size_t i = 0; i < sizeof(doc_lines) / sizeof(doc_lines[0]); ++i) {
        if (i > 0) buffer_split_line(&ed.buffer, i - 1, buffer_line_len(&ed.buffer, i - 1), 0);
        buffer_replace_line(&ed.buffer, i, doc_lines[i], strlen(doc_lines[i]), 0);
    }

    // Match nested bracket: '(' at (0, 16) jumps to ')' at (0, 37)
    ed.cursor_row = 0; ed.cursor_col = 16;
    TEST_ASSERT(editor_jump_matching_pair(&ed), "Jump matching forward ( -> ) succeeded");
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 37, "Jumped to matching closing ')'");

    // Match nested bracket backward: ')' at (0, 37) jumps to '(' at (0, 16)
    TEST_ASSERT(editor_jump_matching_pair(&ed), "Jump matching backward ) -> ( succeeded");
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 16, "Jumped back to opening '('");

    // Match square brackets '[' at (0, 24) -> ']' at (0, 36)
    ed.cursor_row = 0; ed.cursor_col = 24;
    TEST_ASSERT(editor_jump_matching_pair(&ed), "Jump matching [ -> ] succeeded");
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 36, "Jumped to matching ']'");

    // Match braces across lines: '{' at (0, 39) -> '}' at (3, 0)
    ed.cursor_row = 0; ed.cursor_col = 39;
    TEST_ASSERT(editor_jump_matching_pair(&ed), "Jump multi-line { -> } succeeded");
    TEST_ASSERT(ed.cursor_row == 3 && ed.cursor_col == 0, "Jumped to closing '}' on line 3");

    // Match braces backward across lines: '}' at (3, 0) -> '{' at (0, 39)
    TEST_ASSERT(editor_jump_matching_pair(&ed), "Jump multi-line backward } -> { succeeded");
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 39, "Jumped back to opening '{' on line 0");

    // Angle brackets '<' at (5, 4) -> '>' at (5, 24)
    ed.cursor_row = 5; ed.cursor_col = 4;
    TEST_ASSERT(editor_jump_matching_pair(&ed), "Jump matching < -> > succeeded");
    TEST_ASSERT(ed.cursor_row == 5 && ed.cursor_col == 24, "Jumped to matching '>'");
    TEST_ASSERT(editor_jump_matching_pair(&ed), "Jump matching backward > -> < succeeded");
    TEST_ASSERT(ed.cursor_row == 5 && ed.cursor_col == 4, "Jumped back to opening '<'");

    // Markdown bold '**'
    ed.cursor_row = 4; ed.cursor_col = 5; // at first '**'
    TEST_ASSERT(editor_jump_matching_pair(&ed), "Jump matching bold ** -> ** succeeded");
    TEST_ASSERT(ed.cursor_row == 4 && ed.cursor_col == 16, "Jumped to closing '**'");
    TEST_ASSERT(editor_jump_matching_pair(&ed), "Jump matching bold backward ** -> ** succeeded");
    TEST_ASSERT(ed.cursor_row == 4 && ed.cursor_col == 5, "Jumped back to opening '**'");

    // Markdown strikethrough '~~'
    ed.cursor_row = 4; ed.cursor_col = 23; // at first '~~'
    TEST_ASSERT(editor_jump_matching_pair(&ed), "Jump matching ~~ -> ~~ succeeded");
    TEST_ASSERT(ed.cursor_row == 4 && ed.cursor_col == 36, "Jumped to closing '~~'");

    // Markdown inline code '`'
    ed.cursor_row = 4; ed.cursor_col = 43; // at first '`'
    TEST_ASSERT(editor_jump_matching_pair(&ed), "Jump matching ` -> ` succeeded");
    TEST_ASSERT(ed.cursor_row == 4 && ed.cursor_col == 55, "Jumped to closing '`'");

    // Code fence ``` across lines: line 6 -> line 8
    ed.cursor_row = 6; ed.cursor_col = 0;
    TEST_ASSERT(editor_jump_matching_pair(&ed), "Jump code fence ``` -> ``` succeeded");
    TEST_ASSERT(ed.cursor_row == 8 && ed.cursor_col == 0, "Jumped to closing code fence on line 8");
    TEST_ASSERT(editor_jump_matching_pair(&ed), "Jump code fence backward ``` -> ``` succeeded");
    TEST_ASSERT(ed.cursor_row == 6 && ed.cursor_col == 0, "Jumped back to opening code fence on line 6");

    // Unmatched bracket on line 9 returns false
    ed.cursor_row = 9; ed.cursor_col = 18;
    TEST_ASSERT(!editor_jump_matching_pair(&ed), "Unmatched bracket gracefully reports false");

    // 2. Document & Selection Statistics
    DocumentStats stats;
    compute_document_stats(&ed, &stats);
    TEST_ASSERT(stats.total_lines == 10, "Document stats reports 10 total lines");
    TEST_ASSERT(stats.total_words > 20, "Document stats reports word count > 20");
    TEST_ASSERT(stats.total_chars > 150, "Document stats reports total character count > 150");
    TEST_ASSERT(stats.total_chars_no_spaces > 100, "Document stats reports non-whitespace chars > 100");
    TEST_ASSERT(!stats.has_selection, "No selection active initially in stats");

    // Active selection stats
    ed.cursor_row = 4; ed.cursor_col = 5;
    editor_set_mark(&ed);
    ed.cursor_row = 4; ed.cursor_col = 18; // selects "**bold text**"
    compute_document_stats(&ed, &stats);
    TEST_ASSERT(stats.has_selection, "Selection detected in stats");
    TEST_ASSERT(stats.sel_lines == 1, "Selected lines is 1");
    TEST_ASSERT(stats.sel_words == 2, "Selected words count is 2 ('bold', 'text')");
    TEST_ASSERT(stats.sel_chars == 13, "Selected chars count is 13");
    editor_clear_mark(&ed);

    // 3. Dual-pane preview click routing math
    ed.view_mode = VIEW_SPLIT;
    ed.screen_cols = 80;
    ed.screen_rows = 24;
    int split_col = ed.screen_cols / 2; // 40
    TEST_ASSERT(split_col == 40, "Split col is 40 for 80-col terminal");
    int click_editor_x = 15;
    int click_preview_x = 55;
    bool in_preview_1 = (click_editor_x > split_col);
    bool in_preview_2 = (click_preview_x > split_col);
    TEST_ASSERT(!in_preview_1, "Click at x=15 routed to editor pane");
    TEST_ASSERT(in_preview_2, "Click at x=55 routed to preview pane");

    editor_free(&ed);
}

/** @brief Verify C-Home / C-End navigation and split view overscroll & preview sync. */
static void test_doc_navigation_and_preview_sync(void) {
    printf("--- Testing Doc Navigation & Split Preview Sync ---\n");

    Editor ed;
    editor_init(&ed, NULL);

    buffer_replace_line(&ed.buffer, 0, "# Header", 8, 0);
    buffer_split_line(&ed.buffer, 0, 8, 0);
    buffer_replace_line(&ed.buffer, 1, "First paragraph text with extra words that soft wrap onto multiple lines in preview.", 85, 0);
    buffer_split_line(&ed.buffer, 1, 85, 0);
    buffer_replace_line(&ed.buffer, 2, "Second paragraph line.", 22, 0);
    buffer_split_line(&ed.buffer, 2, 22, 0);
    buffer_replace_line(&ed.buffer, 3, "Third line here.", 16, 0);
    buffer_split_line(&ed.buffer, 3, 16, 0);
    buffer_replace_line(&ed.buffer, 4, "Final row of document.", 22, 0);

    // C-Home (editor_move_doc_start) and C-End (editor_move_doc_end)
    ed.cursor_row = 2; ed.cursor_col = 5;
    editor_move_doc_end(&ed);
    TEST_ASSERT(ed.cursor_row == 4 && ed.cursor_col == 22, "C-End jumps to last row and column");

    editor_move_doc_start(&ed);
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 0, "C-Home jumps to row 0, col 0");

    // Split View Overscroll & Bottom Alignment
    ed.view_mode = VIEW_SPLIT;
    ed.screen_rows = 10;
    ed.screen_cols = 80;
    ed.preview_width = 25; // narrow preview causes long text to wrap into many lines
    ed.editor_width = 40;

    render_document(&ed.buffer, ed.preview_width, &ed.rendered_doc);
    TEST_ASSERT(ed.rendered_doc.count > buffer_line_count(&ed.buffer), "Preview has more lines than code buffer due to wrapping/headers");

    size_t preview_total = ed.rendered_doc.count;
    int view_rows = 4; // small viewport to trigger scrolling
    editor_update_scroll(&ed, view_rows);
    TEST_ASSERT(ed.preview_scroll_row == 0, "Initial preview scroll is 0");

    editor_move_doc_end(&ed);
    editor_update_scroll(&ed, view_rows);
    TEST_ASSERT(ed.cursor_row == 4, "Cursor at last row of buffer");

    // Scroll down beyond last row until preview panel reaches bottom
    size_t max_p_scroll = preview_total > (size_t)view_rows ? (preview_total - (size_t)view_rows) : 0;
    for (int i = 0; i < 30; ++i) {
        editor_move_down(&ed);
        editor_update_scroll(&ed, view_rows);
    }

    TEST_ASSERT(ed.preview_scroll_row == max_p_scroll, "Preview panel reached the very bottom");
    TEST_ASSERT(ed.scroll_row > 0, "Editor pane overscrolled beyond last row to allow bottom preview");

    // Scroll back up
    for (int i = 0; i < 30; ++i) {
        editor_move_up(&ed);
        editor_update_scroll(&ed, view_rows);
    }
    TEST_ASSERT(ed.scroll_row == 0, "Editor pane scrolled back to 0");
    TEST_ASSERT(ed.preview_scroll_row == 0, "Preview pane scrolled back to 0");

    editor_free(&ed);
}

static void test_footnote_navigation_and_config_persistence(void) {
    printf("--- Testing Footnote Navigation & Config Persistence ---\n");

    // Set isolated XDG_CONFIG_HOME for testing config persistence in sandbox
    const char *old_xdg = getenv("XDG_CONFIG_HOME");
    char orig_xdg[PATH_MAX] = {0};
    if (old_xdg) {
        strncpy(orig_xdg, old_xdg, sizeof(orig_xdg) - 1);
    }
    setenv("XDG_CONFIG_HOME", "/tmp/mde_test_config", 1);
    unlink("/tmp/mde_test_config/mde/config");

    Editor ed;
    editor_init(&ed, NULL);

    // 1. Config persistence of keybindings toggle
    ed.show_keybindings = true;
    editor_toggle_keybindings(&ed);
    TEST_ASSERT(!ed.show_keybindings, "Keybindings toggled to hidden in editor");

    MdeConfig loaded_cfg;
    bool loaded = config_load(&loaded_cfg);
    TEST_ASSERT(loaded, "Config file loaded successfully after toggle");
    TEST_ASSERT(!loaded_cfg.show_keybindings, "Config file persisted show_keybindings=false");

    editor_toggle_keybindings(&ed);
    TEST_ASSERT(ed.show_keybindings, "Keybindings toggled back to shown in editor");
    config_load(&loaded_cfg);
    TEST_ASSERT(loaded_cfg.show_keybindings, "Config file persisted show_keybindings=true");

    // Cleanup test config
    unlink("/tmp/mde_test_config/mde/config");
    rmdir("/tmp/mde_test_config/mde");
    rmdir("/tmp/mde_test_config");
    if (orig_xdg[0]) {
        setenv("XDG_CONFIG_HOME", orig_xdg, 1);
    } else {
        unsetenv("XDG_CONFIG_HOME");
    }

    // 2. Footnote jumping (reference -> definition, and definition -> first reference)
    buffer_replace_line(&ed.buffer, 0, "First paragraph with footnote [^note1] reference.", 48, 0);
    buffer_split_line(&ed.buffer, 0, 48, 0);
    buffer_replace_line(&ed.buffer, 1, "Second paragraph also citing [^note1] again.", 45, 0);
    buffer_split_line(&ed.buffer, 1, 45, 0);
    buffer_replace_line(&ed.buffer, 2, "Third paragraph citing another [^note2] footnote.", 49, 0);
    buffer_split_line(&ed.buffer, 2, 49, 0);
    buffer_replace_line(&ed.buffer, 3, "Middle body text without any footnotes.", 39, 0);
    buffer_split_line(&ed.buffer, 3, 39, 0);
    buffer_replace_line(&ed.buffer, 4, "[^note1]: Explanation for the first footnote.", 45, 0);
    buffer_split_line(&ed.buffer, 4, 45, 0);
    buffer_replace_line(&ed.buffer, 5, "[^note2]: Explanation for the second footnote.", 46, 0);

    // Position on first reference [^note1] (starts at col 30)
    ed.cursor_row = 0;
    ed.cursor_col = 32;
    TEST_ASSERT(editor_is_footnote_at_cursor(&ed), "Detected footnote ref [^note1] at cursor");

    bool followed = editor_follow_link(&ed);
    TEST_ASSERT(followed, "Follow link handled footnote jump");
    TEST_ASSERT(ed.cursor_row == 4, "Jumped from reference [^note1] to definition on line 4");

    // Cursor is now on definition line 4 ([^note1]: ...). Jumping from definition must jump to FIRST reference
    TEST_ASSERT(editor_is_footnote_at_cursor(&ed), "Detected footnote definition [^note1]: on line 4");
    followed = editor_follow_link(&ed);
    TEST_ASSERT(followed, "Follow link handled jump from definition");
    TEST_ASSERT(ed.cursor_row == 0, "Jumped from definition [^note1]: back to the FIRST reference on line 0");

    // History back (F8 / Alt+-) returns to definition
    bool backed = editor_navigate_back(&ed);
    TEST_ASSERT(backed, "Navigate back succeeded within same file");
    TEST_ASSERT(ed.cursor_row == 4, "Navigated back to definition on line 4");

    // Jumping from second reference [^note1] on line 1 jumps to line 4
    ed.cursor_row = 1;
    ed.cursor_col = 31;
    followed = editor_follow_link(&ed);
    TEST_ASSERT(followed, "Follow link on second reference succeeded");
    TEST_ASSERT(ed.cursor_row == 4, "Jumped from second reference to definition on line 4");

    // Jumping from second footnote [^note2] on line 2 jumps to line 5
    ed.cursor_row = 2;
    ed.cursor_col = 33;
    followed = editor_follow_link(&ed);
    TEST_ASSERT(followed, "Follow link on [^note2] succeeded");
    TEST_ASSERT(ed.cursor_row == 5, "Jumped from [^note2] to definition on line 5");

    // Jumping back from [^note2]: jumps to line 2
    followed = editor_follow_link(&ed);
    TEST_ASSERT(followed, "Follow link on [^note2]: succeeded");
    TEST_ASSERT(ed.cursor_row == 2, "Jumped from [^note2]: back to reference on line 2");

    editor_free(&ed);
}

/** @brief Helper to append a line of text to a test Buffer. */
static void test_buf_add_line(Buffer *b, const char *text) {
    size_t count = buffer_line_count(b);
    if (count == 1 && buffer_line_len(b, 0) == 0) {
        buffer_replace_line(b, 0, text, strlen(text), 0);
    } else {
        buffer_split_line(b, count - 1, buffer_line_len(b, count - 1), 0);
        buffer_replace_line(b, count, text, strlen(text), 0);
    }
}

/** @brief Helper to read an entire entry from an open libzip archive. */
static char *zip_test_read_entry(struct zip *za, const char *name, zip_uint64_t *out_size) {
    struct zip_stat st;
    zip_stat_init(&st);
    if (zip_stat(za, name, 0, &st) != 0) return NULL;
    struct zip_file *zf = zip_fopen(za, name, 0);
    if (!zf) return NULL;
    char *buf = (char *)malloc(st.size + 1);
    if (!buf) { zip_fclose(zf); return NULL; }
    zip_int64_t n = zip_fread(zf, buf, st.size);
    zip_fclose(zf);
    if (n < 0) { free(buf); return NULL; }
    buf[n] = '\0';
    if (out_size) *out_size = st.size;
    return buf;
}

/** @brief Test suite for native EPUB 2/3 conversion, front matter, chapter splitting, and OCF packaging. */
static void test_epub_system(void) {
    printf("--- Testing Native EPUB 2/3 System ---\n");

    // 1. YAML Front Matter parsing
    Buffer buf;
    buffer_init(&buf);
    const char *front_matter_md =
        "---\n"
        "title: \"The Great Book\"\n"
        "author: Jane Austen\n"
        "language: en\n"
        "date: 2026-11-22\n"
        "publisher: Classic Press\n"
        "identifier: urn:uuid:12345678-1234-5678-1234-567812345678\n"
        "rights: Public Domain\n"
        "description: A masterpiece of English literature.\n"
        "---\n"
        "# Chapter 1: Introduction\n"
        "\n"
        "It is a truth universally acknowledged...\n";

    const char *p = front_matter_md;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line_buf[512];
        if (len >= sizeof(line_buf)) len = sizeof(line_buf) - 1;
        memcpy(line_buf, p, len);
        line_buf[len] = '\0';
        test_buf_add_line(&buf, line_buf);
        p = nl ? nl + 1 : p + len;
    }

    EpubMetadata meta;
    memset(&meta, 0, sizeof(meta));
    size_t body_start = 0;
    bool has_front = epub_parse_front_matter(&buf, &meta, &body_start);
    TEST_ASSERT(has_front, "epub_parse_front_matter successfully parsed YAML block");
    TEST_ASSERT(strcmp(meta.title, "The Great Book") == 0, "Parsed title from YAML");
    TEST_ASSERT(strcmp(meta.author, "Jane Austen") == 0, "Parsed author from YAML");
    TEST_ASSERT(strcmp(meta.language, "en") == 0, "Parsed language from YAML");
    TEST_ASSERT(strcmp(meta.date, "2026-11-22") == 0, "Parsed date from YAML");
    TEST_ASSERT(strcmp(meta.publisher, "Classic Press") == 0, "Parsed publisher from YAML");
    TEST_ASSERT(strcmp(meta.identifier, "urn:uuid:12345678-1234-5678-1234-567812345678") == 0, "Parsed identifier from YAML");
    TEST_ASSERT(strcmp(meta.rights, "Public Domain") == 0, "Parsed rights from YAML");
    TEST_ASSERT(strcmp(meta.description, "A masterpiece of English literature.") == 0, "Parsed description from YAML");
    TEST_ASSERT(body_start == 10, "body_start points to line 10 after front matter delimiter");

    // 2. EPUB 3 conversion and OCF compliance verification
    const char *epub3_path = "/tmp/test_book_epub3.epub";
    char err[256] = {0};
    EpubOptions opts3;
    epub_options_init(&opts3);
    opts3.version = EPUB_VERSION_3;

    bool ok = epub_convert_buffer(&buf, epub3_path, &opts3, err, sizeof(err));
    TEST_ASSERT(ok, "epub_convert_buffer succeeds for EPUB 3");

    int errcode = 0;
    struct zip *za = zip_open(epub3_path, ZIP_RDONLY, &errcode);
    TEST_ASSERT(za != NULL, "Generated EPUB 3 is a valid zip archive");
    if (za) {
        // First entry must be mimetype
        const char *e0 = zip_get_name(za, 0, 0);
        TEST_ASSERT(e0 != NULL && strcmp(e0, "mimetype") == 0, "First archive entry is 'mimetype'");

        zip_uint64_t mt_size = 0;
        char *mt_content = zip_test_read_entry(za, "mimetype", &mt_size);
        TEST_ASSERT(mt_content != NULL && strcmp(mt_content, "application/epub+zip") == 0,
                    "mimetype entry contains exact 'application/epub+zip'");
        TEST_ASSERT(mt_size == 20, "mimetype entry is exactly 20 bytes uncompressed");
        free(mt_content);

        // container.xml
        char *cnt = zip_test_read_entry(za, "META-INF/container.xml", NULL);
        TEST_ASSERT(cnt != NULL && strstr(cnt, "full-path=\"EPUB/package.opf\"") != NULL,
                    "container.xml points to EPUB/package.opf");
        free(cnt);

        // package.opf (EPUB 3)
        char *opf = zip_test_read_entry(za, "EPUB/package.opf", NULL);
        TEST_ASSERT(opf != NULL, "EPUB/package.opf exists in archive");
        if (opf) {
            TEST_ASSERT(strstr(opf, "version=\"3.0\"") != NULL, "OPF specifies version='3.0'");
            TEST_ASSERT(strstr(opf, "<dc:title>The Great Book</dc:title>") != NULL, "OPF contains title metadata");
            TEST_ASSERT(strstr(opf, "Jane Austen") != NULL && strstr(opf, "<dc:creator") != NULL, "OPF contains creator metadata");
            TEST_ASSERT(strstr(opf, "properties=\"nav\"") != NULL, "OPF manifest marks nav.xhtml with properties='nav'");
            free(opf);
        }

        // nav.xhtml (EPUB 3 navigation document)
        char *nav = zip_test_read_entry(za, "EPUB/nav.xhtml", NULL);
        TEST_ASSERT(nav != NULL, "EPUB/nav.xhtml exists in archive");
        if (nav) {
            TEST_ASSERT(strstr(nav, "<nav epub:type=\"toc\"") != NULL, "nav.xhtml contains epub:type='toc'");
            TEST_ASSERT(strstr(nav, "Chapter 1: Introduction") != NULL, "nav.xhtml contains chapter heading in TOC");
            free(nav);
        }

        // ch001.xhtml
        char *ch1 = zip_test_read_entry(za, "EPUB/ch001.xhtml", NULL);
        TEST_ASSERT(ch1 != NULL, "EPUB/ch001.xhtml exists in archive");
        if (ch1) {
            TEST_ASSERT(strstr(ch1, "It is a truth universally acknowledged") != NULL,
                        "Chapter content contains body paragraph text");
            TEST_ASSERT(strstr(ch1, "Jane Austen") == NULL,
                        "YAML front matter is stripped from chapter body text");
            free(ch1);
        }

        // stylesheet.css
        char *css = zip_test_read_entry(za, "EPUB/stylesheet.css", NULL);
        TEST_ASSERT(css != NULL && strstr(css, "font-family") != NULL,
                    "EPUB/stylesheet.css exists and contains CSS rules");
        free(css);

        zip_close(za);
    }
    remove(epub3_path);
    buffer_free(&buf);

    // 3. EPUB 2 conversion and backward compatibility
    Buffer buf2;
    buffer_init(&buf2);
    const char *ch_split_md =
        "# Part One\n"
        "Paragraph in part one.\n"
        "## Section A\n"
        "Section A text.\n"
        "# Part Two\n"
        "Paragraph in part two.\n";

    p = ch_split_md;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line_buf[512];
        if (len >= sizeof(line_buf)) len = sizeof(line_buf) - 1;
        memcpy(line_buf, p, len);
        line_buf[len] = '\0';
        test_buf_add_line(&buf2, line_buf);
        p = nl ? nl + 1 : p + len;
    }

    const char *epub2_path = "/tmp/test_book_epub2.epub";
    EpubOptions opts2;
    epub_options_init(&opts2);
    opts2.version = EPUB_VERSION_2;
    snprintf(opts2.meta.title, sizeof(opts2.meta.title), "Split Book");
    snprintf(opts2.meta.author, sizeof(opts2.meta.author), "Author Two");

    ok = epub_convert_buffer(&buf2, epub2_path, &opts2, err, sizeof(err));
    TEST_ASSERT(ok, "epub_convert_buffer succeeds for EPUB 2 with chapter splitting");

    za = zip_open(epub2_path, ZIP_RDONLY, &errcode);
    TEST_ASSERT(za != NULL, "Generated EPUB 2 is a valid zip archive");
    if (za) {
        // package.opf (EPUB 2)
        char *opf = zip_test_read_entry(za, "EPUB/package.opf", NULL);
        TEST_ASSERT(opf != NULL, "EPUB/package.opf exists in EPUB 2 archive");
        if (opf) {
            TEST_ASSERT(strstr(opf, "version=\"2.0\"") != NULL, "OPF specifies version='2.0'");
            TEST_ASSERT(strstr(opf, "toc=\"ncx\"") != NULL, "Spine references toc='ncx'");
            free(opf);
        }

        // toc.ncx (EPUB 2 NCX navigation)
        char *ncx = zip_test_read_entry(za, "EPUB/toc.ncx", NULL);
        TEST_ASSERT(ncx != NULL, "EPUB/toc.ncx exists in EPUB 2 archive");
        if (ncx) {
            TEST_ASSERT(strstr(ncx, "<ncx") != NULL, "toc.ncx contains <ncx root element");
            TEST_ASSERT(strstr(ncx, "<navMap>") != NULL, "toc.ncx contains <navMap>");
            TEST_ASSERT(strstr(ncx, "Part One") != NULL, "toc.ncx includes Part One");
            TEST_ASSERT(strstr(ncx, "Section A") != NULL, "toc.ncx includes nested Section A");
            TEST_ASSERT(strstr(ncx, "Part Two") != NULL, "toc.ncx includes Part Two");
            free(ncx);
        }

        // Multi-chapter splitting verification
        char *ch1 = zip_test_read_entry(za, "EPUB/ch001.xhtml", NULL);
        TEST_ASSERT(ch1 != NULL && strstr(ch1, "Paragraph in part one") != NULL,
                    "ch001.xhtml contains first chapter");
        free(ch1);

        char *ch2 = zip_test_read_entry(za, "EPUB/ch002.xhtml", NULL);
        TEST_ASSERT(ch2 != NULL && strstr(ch2, "Paragraph in part two") != NULL,
                    "ch002.xhtml contains second chapter");
        free(ch2);

        zip_close(za);
    }
    remove(epub2_path);

    // 4. Single-chapter mode (--no-split)
    const char *nosplit_path = "/tmp/test_book_nosplit.epub";
    EpubOptions opts_nosplit;
    epub_options_init(&opts_nosplit);
    opts_nosplit.split_h1 = false;
    ok = epub_convert_buffer(&buf2, nosplit_path, &opts_nosplit, err, sizeof(err));
    TEST_ASSERT(ok, "epub_convert_buffer succeeds with split_h1 = false");

    za = zip_open(nosplit_path, ZIP_RDONLY, &errcode);
    if (za) {
        char *ch1 = zip_test_read_entry(za, "EPUB/ch001.xhtml", NULL);
        TEST_ASSERT(ch1 != NULL && strstr(ch1, "Paragraph in part one") != NULL &&
                    strstr(ch1, "Paragraph in part two") != NULL,
                    "Single chapter mode keeps all headings in ch001.xhtml");
        free(ch1);
        struct zip_stat st_ch2;
        zip_stat_init(&st_ch2);
        TEST_ASSERT(zip_stat(za, "EPUB/ch002.xhtml", 0, &st_ch2) != 0,
                    "No ch002.xhtml created when splitting is disabled");
        zip_close(za);
    }
    remove(nosplit_path);

    // 5. Image bundling into EPUB
    const char *dummy_img_path = "/tmp/mde_test_diagram.png";
    FILE *img_f = fopen(dummy_img_path, "wb");
    if (img_f) {
        // Write standard 8-byte PNG header + dummy payload
        const unsigned char png_hdr[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x01};
        fwrite(png_hdr, 1, sizeof(png_hdr), img_f);
        fclose(img_f);

        Buffer img_buf;
        buffer_init(&img_buf);
        test_buf_add_line(&img_buf, "# Book with Image");
        char img_line[256];
        snprintf(img_line, sizeof(img_line), "Here is the diagram: ![Diagram](%s)", dummy_img_path);
        test_buf_add_line(&img_buf, img_line);

        const char *img_epub = "/tmp/test_book_image.epub";
        ok = epub_convert_buffer(&img_buf, img_epub, NULL, err, sizeof(err));
        TEST_ASSERT(ok, "EPUB conversion succeeds with local image reference");

        za = zip_open(img_epub, ZIP_RDONLY, &errcode);
        if (za) {
            char *ch = zip_test_read_entry(za, "EPUB/ch001.xhtml", NULL);
            TEST_ASSERT(ch != NULL && strstr(ch, "src=\"images/img_001.png\"") != NULL,
                        "Chapter XHTML rewrites image source to images/ relative path");
            free(ch);

            struct zip_stat img_st;
            zip_stat_init(&img_st);
            TEST_ASSERT(zip_stat(za, "EPUB/images/img_001.png", 0, &img_st) == 0,
                        "Image is bundled inside EPUB/images/ in the zip archive");

            char *opf = zip_test_read_entry(za, "EPUB/package.opf", NULL);
            TEST_ASSERT(opf != NULL && strstr(opf, "media-type=\"image/png\"") != NULL,
                        "OPF manifest declares image with image/png media type");
            free(opf);
            zip_close(za);
        }
        remove(img_epub);
        buffer_free(&img_buf);
        remove(dummy_img_path);
    }

    buffer_free(&buf2);

    // 6. Standalone CLI binary invocation
    const char *cli_md_path = "/tmp/test_md2epub_cli.md";
    const char *cli_epub_path = "/tmp/test_md2epub_cli.epub";
    FILE *cf = fopen(cli_md_path, "wb");
    if (cf) {
        fputs("# CLI Chapter\nCLI Body paragraph.\n", cf);
        fclose(cf);

        char cmd[512];
        snprintf(cmd, sizeof(cmd), "./bin/md2epub -q -o %s --title \"CLI Book\" --author \"CLI Author\" %s",
                 cli_epub_path, cli_md_path);
        int rc = system(cmd);
        TEST_ASSERT(rc == 0, "Standalone bin/md2epub CLI command exited with code 0");

        struct stat cli_st;
        TEST_ASSERT(stat(cli_epub_path, &cli_st) == 0 && cli_st.st_size > 0,
                    "Standalone bin/md2epub generated destination .epub file");

        remove(cli_epub_path);
        remove(cli_md_path);
    }

    // 7. Interactive Metadata editing & front-matter synchronization
    Editor ed_meta;
    editor_init(&ed_meta, NULL);
    editor_paste_text(&ed_meta, "# Great Adventures\nThis is chapter body text.", 45);

    EpubMetadata new_meta;
    memset(&new_meta, 0, sizeof(new_meta));
    snprintf(new_meta.title, sizeof(new_meta.title), "Great Adventures");
    snprintf(new_meta.author, sizeof(new_meta.author), "Arthur Conan Doyle");
    snprintf(new_meta.language, sizeof(new_meta.language), "en");
    snprintf(new_meta.date, sizeof(new_meta.date), "2026-11-22");
    snprintf(new_meta.publisher, sizeof(new_meta.publisher), "Strand Magazine");
    snprintf(new_meta.identifier, sizeof(new_meta.identifier), "urn:isbn:9780123456789");
    snprintf(new_meta.rights, sizeof(new_meta.rights), "Public Domain");
    snprintf(new_meta.description, sizeof(new_meta.description), "Classic adventures.");
    snprintf(new_meta.cover_path, sizeof(new_meta.cover_path), "cover.jpg");
    snprintf(new_meta.css_path, sizeof(new_meta.css_path), "book.css");

    bool updated = editor_update_front_matter(&ed_meta, &new_meta);
    TEST_ASSERT(updated, "editor_update_front_matter succeeds inserting new front matter");
    TEST_ASSERT(ed_meta.buffer.is_dirty, "Buffer marked dirty after metadata update");

    EpubMetadata read_m;
    memset(&read_m, 0, sizeof(read_m));
    size_t bstart = 0;
    bool has_fm = epub_parse_front_matter(&ed_meta.buffer, &read_m, &bstart);
    TEST_ASSERT(has_fm, "Inserted front matter successfully parsed by epub_parse_front_matter");
    TEST_ASSERT(strcmp(read_m.title, "Great Adventures") == 0, "Title preserved in front matter");
    TEST_ASSERT(strcmp(read_m.author, "Arthur Conan Doyle") == 0, "Author preserved in front matter");
    TEST_ASSERT(strcmp(read_m.publisher, "Strand Magazine") == 0, "Publisher preserved in front matter");
    TEST_ASSERT(strcmp(read_m.identifier, "urn:isbn:9780123456789") == 0, "Identifier preserved in front matter");
    TEST_ASSERT(strcmp(read_m.cover_path, "cover.jpg") == 0, "Cover path preserved in front matter");

    // Test updating existing front matter
    snprintf(new_meta.author, sizeof(new_meta.author), "Sir Arthur Conan Doyle");
    snprintf(new_meta.title, sizeof(new_meta.title), "The Hound of the Baskervilles");
    snprintf(new_meta.cover_path, sizeof(new_meta.cover_path), "images/hound_cover.png");
    updated = editor_update_front_matter(&ed_meta, &new_meta);
    TEST_ASSERT(updated, "editor_update_front_matter succeeds updating existing front matter");

    memset(&read_m, 0, sizeof(read_m));
    has_fm = epub_parse_front_matter(&ed_meta.buffer, &read_m, &bstart);
    TEST_ASSERT(has_fm, "Updated front matter parsed successfully");
    TEST_ASSERT(strcmp(read_m.title, "The Hound of the Baskervilles") == 0, "Updated title reflected");
    TEST_ASSERT(strcmp(read_m.author, "Sir Arthur Conan Doyle") == 0, "Updated author reflected");
    TEST_ASSERT(strcmp(read_m.cover_path, "images/hound_cover.png") == 0, "Updated cover picture path reflected");

    // Verify EPUB export immediately uses the updated metadata
    const char *meta_epub_out = "/tmp/test_meta_updated.epub";
    char meta_err[256] = {0};
    ok = epub_convert_buffer(&ed_meta.buffer, meta_epub_out, NULL, meta_err, sizeof(meta_err));
    TEST_ASSERT(ok, "EPUB export succeeds on buffer with updated metadata");
    int m_err = 0;
    struct zip *m_za = zip_open(meta_epub_out, ZIP_RDONLY, &m_err);
    TEST_ASSERT(m_za != NULL, "EPUB archive with updated metadata opens");
    if (m_za) {
        char *m_opf = zip_test_read_entry(m_za, "EPUB/package.opf", NULL);
        TEST_ASSERT(m_opf != NULL && strstr(m_opf, "The Hound of the Baskervilles") != NULL,
                    "Package OPF contains updated title");
        TEST_ASSERT(m_opf != NULL && strstr(m_opf, "Sir Arthur Conan Doyle") != NULL,
                    "Package OPF contains updated author");
        free(m_opf);
        zip_close(m_za);
    }
    remove(meta_epub_out);

    editor_free(&ed_meta);
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
    test_markdown_extensions();
    test_interactive_ux_and_navigation();
    test_doc_navigation_and_preview_sync();
    test_footnote_navigation_and_config_persistence();
    test_epub_system();

    printf("==========================================\n");
    printf("Test Results: %d / %d tests passed (%.1f%%)\n",
           g_tests_passed, g_tests_run,
           g_tests_run > 0 ? (100.0 * g_tests_passed / g_tests_run) : 0.0);
    printf("==========================================\n");

    return (g_tests_run == g_tests_passed) ? 0 : 1;
}
