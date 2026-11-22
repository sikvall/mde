# mde — Project Description & AI Development Guide

> Purpose of this file: a comprehensive briefing for any AI assistant (or human) working on
> **mde**. It details what the project is, its architectural design, build system, current state,
> recent upgrades, git history, and the remaining roadmap items.
>
> Snapshot basis: **v0.1.1** (2026-11-22), pure C11 codebase compiling warning-free with `gcc` and `ncursesw`.
> Test runner passes **490 / 490 tests (100.0%)**.

---

## 1. What mde is

**mde** ("Markdown Editor") is a full-screen, text-mode (TUI) editor specialized for writing,
navigating, and previewing Markdown (`.md`) documents. It is written in **pure C11** on top of
**`ncursesw`**, with no external runtime library dependencies (pandoc is optional for multi-format export).

- **Author / owner:** Anders Sikvall (Täpp Anders Sikvall).
- **Remote:** `git@github.com:sikvall/mde.git` (branch `main`).
- **License:** MIT.
- **Version:** `0.1.1` (central source of truth: `src/version.h`).
- **Platform:** Linux/Unix terminals (including WSL).

### Core Product Tenets

1. **Distraction-Free Writing & Live Dual-Pane Preview**: Instant feedback without terminal flicker, featuring steady non-flashing cursors (`█` in insert mode, `_` in overwrite mode).
2. **Nano-Compatible Muscle Memory**: Intuitive keybindings mirroring GNU `nano` (`^X`, `^O`, `^R`, `^S`, `^W`, `^K`, `^U`, `^G`, `M-U`/`M-E` undo/redo).
3. **Multi-File Document & Book Authoring**: In-editor hypertext link following (`F7` / `Alt+O`), backward navigation (`F8`), and recursive linked-file export bundling (HTML5, XML/DocBook5, LaTeX, and EPUB).
4. **Data Safety First**: Atomic temp-file saves with symlink resolution, permission preservation, exact dirty-state tracking, save/discard confirmation prompts, and Emacs-style backup/recovery files.
5. **Zero Heavy Runtime Dependencies**: Pure C11 standard library + `libncursesw`.

---

## 2. Quick Reference for Developers

```bash
make                 # Build binary -> bin/mde (objects in obj/ with auto .d header deps)
make test            # Build & execute test runner -> bin/test_runner (395 passing assertions)
make clean           # Remove obj/ and bin/
sudo make install    # Install binary and man page to $(PREFIX) (default: /usr/local)
make deb             # Package Debian .deb package -> bin/mde_<version>_<arch>.deb

# Running mde
./bin/mde examples/demo.md    # Open file with default 78-column auto line break
./bin/mde -w 100 notes.md     # Custom 100-column auto line break
./bin/mde --wrap=off notes.md # Disable auto line break
./bin/mde -h | -v             # Usage instructions / version info
```

- **Compiler & Flags:** `CFLAGS ?= -std=c11 -MMD -MP -Wall -Wextra -pedantic -O2 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_XOPEN_SOURCE_EXTENDED=1`, `LDFLAGS ?= -lncursesw`.
- **Automatic Header Dependencies:** The build system uses `-MMD -MP` and `-include $(OBJS:.o=.d)` to automatically track `.h` changes across builds.
- **Unified Installation:** `PREFIX ?= /usr/local` applies to `make install` (`$(DESTDIR)$(PREFIX)/bin` and `$(DESTDIR)$(PREFIX)/share/man/man1`).

---

## 3. Project Structure

```
mde/
├── Makefile               # Build, test, install, and deb packaging
├── README.md              # User documentation and keybindings guide
├── CHANGELOG.md           # Keep a Changelog document tracking releases (0.1.0, 0.1.1)
├── LICENSE.md             # Software license
├── examples/              # Interactive sample documents demonstrating mde features
│   ├── demo.md            # Comprehensive interactive showcase document
│   └── advanced.md        # Multi-file navigation & linked export showcase
├── man/
│   └── mde.1              # UNIX manual page
├── scripts/
│   └── generate_keybindings.py # Single-source keybinding table generator
├── src/
│   ├── version.h          # Centralized version macros and app metadata
│   ├── config.h / .c      # Configuration file loader (~/.config/mde/config or ~/.mde/config)
│   ├── backup.h / .c      # Emacs-style backup (~) and autosave (#...#) handling
│   ├── buffer.h / .c      # Line-based dynamic text buffer with CRLF detection & atomic saves
│   ├── editor.h / .c      # Core editor loop, cursor, selection, clipboard, auto-wrap
│   ├── editor_link.c      # Hypertext navigation, relative links, and anchor resolution
│   ├── editor_search.c    # Interactive search and replace subsystem
│   ├── export.h / .c      # Master export dispatcher and pandoc integration
│   ├── export_internal.h  # Shared export document structures and parsing helpers
│   ├── export_html.c      # Built-in standalone HTML5 export backend
│   ├── export_xml.c       # Built-in structured XML export backend
│   ├── export_latex.c     # Built-in unified LaTeX export backend
│   ├── markdown.h / .c    # Streaming Markdown lexer and span tokenizer
│   ├── renderer.h / .c    # Formatted preview renderer with responsive soft-wrapping & tables
│   ├── theme.h / .c       # 256-color & 16-color ncurses palette & token styles
│   ├── ui.h / .c          # Core screen layout, ruler, gutter, status and message bar
│   ├── ui_internal.h      # Shared UI box drawing and input helper declarations
│   ├── ui_dialog_file.c   # Modal file browser (Open, Save As, Link)
│   ├── ui_dialog_help.c   # Modal two-tab keyboard shortcut and Markdown help overlay
│   ├── ui_dialog_export.c # Modal interactive export format selector menu
│   ├── ui_dialog_outline.c # Modal document table-of-contents outline navigator
│   ├── ui_dialog_stats.c  # Modal document and selection statistics dialog
│   ├── undo.h / .c        # Multi-level undo/redo action stacks with group IDs
│   ├── utf8.h / .c        # Multi-byte UTF-8 length, boundary, and wcwidth visual column math
│   └── main.c             # Event loop, CLI parsing, signal handling, key & mouse dispatch
└── tests/
    └── test_runner.c      # Comprehensive C11 unit test suite (462 assertions)
```

---

## 4. Key Subsystems & Architecture

### 4.1 Line Endings & Data Safety (`buffer.c`, `backup.c`)
- **Fidelity Preservation:** `Buffer.has_crlf_endings` detects whether a loaded file uses `\r\n`. Saving converts back to `\r\n` if originally CRLF, or `\n` if LF. Visually and internally, lines are normalized cleanly without carriage returns.
- **Emacs-Style Backups:**
  - `backup_create_tilde()` creates a `filename~` backup of the original file prior to the first save of the session.
  - `backup_create_autosave()` writes `#filename#` when the buffer is modified.
  - `backup_remove_autosave()` deletes `#filename#` upon save or clean exit.
- **Atomic Saves:** `buffer_save()` writes to a sibling temporary file (`.mde_tmp_XXXXXX`), flushes, fsyncs, preserves original permissions, and atomically `rename()`s over the destination, ensuring symlink targets are resolved safely without destroying the symlink itself.

### 4.2 Configuration Subsystem (`config.c`)
- Reads and persists configuration options from:
  1. `~/.config/mde/config` (preferred if `~/.config` exists)
  2. `~/.mde/config` (fallback if `.config` directory is absent)
- Supports settings such as `wrap_col`, `view_mode`, `tab_size`, and `make_backup`.

### 4.3 Mouse Input Handling (`main.c`, `ui.c`)
- Enabled via `mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL)`.
- `BUTTON1_PRESSED` anchors the selection mark; `BUTTON1_RELEASED` immediately commits the selection to the internal clipboard and broadcasts it to the system clipboard via ANSI **OSC 52**.
- `BUTTON2_CLICKED` (middle-click) and `BUTTON3_CLICKED` (right-click) paste the clipboard contents into the buffer at the clicked coordinates.
- Mouse wheel (`BUTTON4` / `BUTTON5`) scrolls viewport lines.

### 4.4 Markdown Lexer & Tokenizer (`markdown.c`)
- High-speed streaming lexer parsing lines into discrete `MdSpan` tokens without full AST overhead.
- Supports ATX headers 1–6, blockquotes (`>`), unordered/ordered lists, task checkboxes (`- [ ]`, `- [x]`), fenced code blocks (``` and ~~~), horizontal rules (`---`, `***`), table cells and borders (`|`), bold (`**`, `__`), italic (`*`, `_`), bold-italic (`***`), strikethrough (`~~`), inline code (`` `code` ``), hyperlinks, images, footnotes (`[^note]` / `[^note]:`), nested list indentation tracking (`list_indent`), reference-style links (`[text][ref]` and definitions `[ref]: url`), and bare URL autolinking (`http://`, `https://`).

### 4.5 Responsive Preview Soft-Wrapping & Table Formatting (`renderer.c`)
- `render_document()` renders the preview to match the active pane width (`ed->preview_width`), dynamically adapting whenever the terminal is resized (`KEY_RESIZE`) or the view mode changes (`VIEW_SPLIT`, `VIEW_INPLACE`, `VIEW_PREVIEW`).
- `render_inline_spans_wrapped()` breaks long text lines at word boundaries to fit `width - 4`.
- **Context-Aware Indentation:**
  - Continuation lines for list and task items automatically indent under the body text rather than snapping back to column 0.
  - Continuation lines for blockquotes preserve the `▎ ` bar indicator.
  - Multi-level nested lists cycle bullets: Level 1 (`•`), Level 2 (`◦`), Level 3+ (`▪`), with matching continuation margins.
- **Table Column Alignment:**
  - Separator lines (`| :--- | :---: | ---: |`) configure column alignment (`ALIGN_LEFT`, `ALIGN_CENTER`, `ALIGN_RIGHT`).
  - Cells are rendered with padded box borders adhering to per-column alignment while preserving multi-byte UTF-8 sequences.
- **Clean Attribute State:**
  - Uses `attrset(rc->attr)` to ensure formatting styles (such as inline code backticks) transition cleanly without residual attribute leakage into subsequent text chunks.
- Preview cursor mapping (`renderer_get_cursor_vcol`) and line lookup (`rendered_doc_find_line`) maintain exact line synchronization between editor and preview.

---

## 5. Summary of Recent Improvements (v0.1.1)

1. **CR+LF vs LF Preservation:** Added `has_crlf_endings` to `Buffer`; files originally with CRLF are saved with CRLF, while LF files remain LF.
2. **Build System & Dependencies:** Added `-MMD -MP` and `.d` include rules in `Makefile`; standardized `PREFIX ?= /usr/local`.
3. **Backup & Autosave:** Added `backup.h` / `backup.c` implementing Emacs-style `filename~` on save and `#filename#` autosave recovery.
4. **Configuration Engine:** Added `config.h` / `config.c` parsing `~/.config/mde/config` with fallback to `~/.mde/config`.
5. **Mouse Selection & Paste:** Added mouse drag selection with immediate clipboard commit and right/middle-click paste.
6. **Documentation & Changelog:** Created `CHANGELOG.md`, fixed local links in `README.md`, added `scripts/generate_keybindings.py`, and updated manual page and tests to version `0.1.1`.
7. **Responsive Preview Soft-Wrapping:** Preview dynamically follows window width, wrapping long lines cleanly with proper continuation margins for lists and quotes.
8. **Markdown Extensions:** Added footnotes (`[^ref]` and `[^ref]:`), nested lists with bullet cycling (`•`, `◦`, `▪`), reference links (`[text][ref]` and definitions `[ref]: url`), bare URL autolinking, and table column alignment (`:---`, `:---:`, `---:`).
9. **Doxygen Documentation:** Fully documented all public and internal interfaces across `src/*.h` and `src/*.c` adhering to standard Doxygen format.
10. **Interactive UX & Navigation (6.5):** Dual-pane mouse routing in `VIEW_SPLIT`, live document & selection statistics popover (`Alt+D`), multi-line bracket and delimiter pair matching (`Ctrl+]` / `Alt+]`).
11. **Expanded Shortcut Hints & F-Key Toggle:** 4th row of bottom shortcut hints (`F9 Outline`, `M-D Stats`, `^] Jump`, `F7 Follow`, `M-J Reflow`, `F10 Hints`) and dedicated toggle key (`F10` / `F12` / `Alt+K`) to hide/show the bottom bar and maximize editing space.

---

## 6. What Remains / Roadmap

The following tasks from your requirements and project roadmap are identified for subsequent implementation:

### 6.1 Markdown Lexer & Renderer Enhancements
- [x] **Footnotes:**
  - Lex inline references `[^ref]` as `MD_FOOTNOTE_REF` and definition blocks `[^ref]: definition text` as `MD_FOOTNOTE_DEF`.
  - In preview mode, render footnote definitions styled with distinctive anchor markers (`⚓`).
  - Wire export handlers in HTML (`<sup><a href="#fn-...">[id]</a></sup>` and `<div class="footnote">`), XML, and LaTeX (`\footnotemark` and `\footnotetext`).
- [x] **Nested Lists:**
  - Track list indentation depth (`list_indent`) to support multi-level lists (`  - level 2`, `    * level 3`).
  - Cycle bullet glyphs in preview (`•`, `◦`, `▪`) based on indentation level with matching continuation indentation.
- [x] **Reference-Style Links:**
  - Support `[text][ref]` inline links and matching definitions `[ref]: https://...`.
- [x] **Auto-Linking Bare URLs:**
  - Automatically detect standalone `http://` and `https://` URLs in plain paragraph text, rendering them clickable/underlined as `MD_AUTO_LINK`.
- [x] **Inline Code Transition Cleanliness:**
  - Ensure formatting markers before and after backticks (e.g. `` `code style` then normal text ``) transition styles cleanly without leaving residual active attributes on subsequent plain text spans.
- [x] **Table Column Alignment:**
  - Parse delimiter rows (`| :--- | :---: | ---: |`) to detect column alignment (Left, Center, Right).
  - Format rendered table cells with left, center, or right padding in `renderer.c`.

### 6.2 Performance & Rendering Optimization
- [x] **Incremental / View-Bound Preview Rendering:**
  - Implemented `render_document_range()` with code-fence lookback context and automatic table encapsulation.
  - In `editor_refresh_rendered()`, documents exceeding 1,000 lines only render the active visible viewport with a lookahead margin.
  - Dynamically updates whenever viewport scrolling approaches the margin boundaries.

### 6.3 Project & Repository Cleanliness
- [x] **LICENSE.md:** Placed the MIT License file in the repository root.
- [x] **Untracked Workspace Artifacts:** Cleaned leftover manual test outputs and updated `.gitignore` (`*.epub`, `*.tex`, `*.aux`, `*.log`, `*.d`, `*~`, `#*#`, `local`).

### 6.4 Codebase Modularization & Structural Refactoring
- [x] **Export Subsystem Modularization (`export.c` -> 960 lines):**
  - Extracted built-in HTML5 exporter into `src/export_html.c`.
  - Extracted built-in structured XML exporter into `src/export_xml.c`.
  - Extracted built-in unified LaTeX exporter into `src/export_latex.c`.
  - Created `src/export_internal.h` declaring shared document node models and parser utilities.
- [x] **Editor Subsystem Modularization (`editor.c` -> 1,681 lines):**
  - Extracted hypertext link resolution, navigation history, and relative path calculation into `src/editor_link.c`.
  - Extracted interactive substring search and replace into `src/editor_search.c`.
- [x] **UI & Dialog Subsystem Modularization (`ui.c` -> 938 lines):**
  - Extracted modal file browser dialogue (Open, Save As, Link) into `src/ui_dialog_file.c`.
  - Extracted two-tab modal help screen into `src/ui_dialog_help.c`.
  - Extracted modal export format selector into `src/ui_dialog_export.c`.
  - Extracted modal document outline table-of-contents into `src/ui_dialog_outline.c`.
  - Created `src/ui_internal.h` declaring shared box border drawing and UTF-8 modal input functions.
- [x] **Zero Warning & Full Regression Verification:**
  - Compiles cleanly under C11 with `-Wall -Wextra -pedantic -Werror`.
  - All 418 unit tests in `bin/test_runner` passing (100.0%).

### 6.5 Interactive UX & Dual-Pane Navigation Enhancements
- [x] **Dual-Pane Mouse Click Routing (`VIEW_SPLIT`):**
  - When in split view mode, detect whether a mouse click occurred in the left editor pane or the right preview pane (`click_x > split_col`).
  - Route clicks in the preview pane to synchronize editor cursor to the corresponding source line, and allow direct single-click following of Markdown hyperlinks.
- [x] **Live Document & Selection Statistics Modal (`Alt+D` / `M-D`):**
  - Display an interactive popover showing comprehensive document metrics: line count, word count, total characters, characters excluding whitespace, active selection word/character counts, and estimated reading time.
- [x] **Bracket & Delimiter Pair Matching Navigation (`Ctrl+]` / `Alt+]`):**
  - Quickly jump the cursor between matching pairs of parentheses `()`, brackets `[]`, braces `{}`, angle brackets `<>`, and Markdown formatting delimiters (`**`, `~~`, `` ` ``, ```` ``` ````, `*`/`_`).
- [x] **Expanded 4th Shortcut Hint Row & F-Key Toggle (`F10` / `F12` / `Alt+K`):**
  - Added a 4th row of Nano-style bottom shortcut hints (`F9 Outline`, `M-D Stats`, `^] Jump`, `F7 Follow`, `M-J Reflow`, `F10 Hints`).
  - Added dedicated function key toggle (`F10` / `F12`) to instantly hide/show bottom shortcut hints and maximize editable screen area.
  - Added persistent configuration setting `show_keybindings = true/false` in `~/.config/mde/config`.

### 6.6 Markdown Authoring, Typography & Diagnostic Tools
- [ ] **Link Integrity & Dead Link Validator (`F9` / Alt+L):**
  - Provide a diagnostic scan of all inline `[text](target)` and reference `[ref]: target` links in the current document.
  - Verify existence of local target files on disk and validate `#anchor` fragments against heading slugs.
  - Display missing or broken targets in a navigable modal list with instant jump to the offending line.
- [ ] **Interactive Table of Contents (`[TOC]`) Generator & Refresher:**
  - Command to insert or update an indented Markdown hierarchy of all document headings (H1–H6) at the cursor or inside `<!-- toc -->...<!-- /toc -->` comment delimiters.
- [ ] **Smart Typographical Punctuation Rendering:**
  - Render curly quotes `“”` / `‘’`, en-dash `--` (`–`), em-dash `---` (`—`), and ellipsis `...` (`…`) in the preview renderer and export backends without modifying raw text source.
- [ ] **Fenced Code Block Syntax Highlighting:**
  - Tokenize and colorize keywords, strings, comments, and numbers for common languages (`c`, `python`, `sh`, `json`, `markdown`, `html`) within fenced code blocks.

### 6.7 Export & Template Customization
- [ ] **Custom CSS Stylesheet Option for HTML Export:**
  - Add support for custom external or inlined CSS stylesheets via `--css=<file>` command line argument or config file option (`export_css = <path>`).
- [ ] **Direct PDF Export Backend:**
  - Expand `ui_dialog_export.c` and master export dispatcher to include PDF as an export target, delegating to Pandoc with `pdflatex`, `xelatex`, or `weasyprint` engines.

### 6.8 Quality Assurance, Memory Safety & Fuzzing
- [ ] **AddressSanitizer & UndefinedBehaviorSanitizer Target (`make sanitize`):**
  - Add a dedicated make target compiling the test runner with `-fsanitize=address,undefined -g` to verify absence of memory leaks, buffer overflows, or undefined behavior.
- [ ] **Automated Fuzz Testing Harness (`make fuzz`):**
  - Add a lightweight fuzz test target feeding randomized and degenerate byte sequences into `md_parse_line()` and UTF-8 utilities to guarantee crash immunity against pathological input.

### 6.9 Color Themes & Interactive Theme Customization
- [ ] **Expanded Built-in Color Themes:**
  - Introduce a curated collection of built-in dark and light themes (e.g., Default/Monokai, Solarized Dark, Solarized Light, Nord, Dracula, Gruvbox, and High-Contrast / Accessible).
  - Add interactive theme selection menu and persistent configuration option (`theme = <name>` in `~/.config/mde/config`).
  - Graceful palette adaptation between 256-color and standard 16-color terminal modes.
- [ ] **Interactive User Theme Editor:**
  - Interactive visual theme editor modal allowing users to inspect and customize color assignments and text attributes (bold, dim, underline, reverse) for all UI chrome (status bar, ruler, gutter, borders) and Markdown syntax elements (H1–H6, bold, italic, code, links, blockquotes).
  - Live side-by-side Markdown snippet preview showing instant visual feedback while editing color palettes.
  - Export and import custom user themes to `~/.config/mde/themes/<theme_name>.conf`.

---

## 7. Recent Session Decisions & Conversation Highlights (2026-11-22)

### 7.1 Interactive UX & Navigation (Roadmap 6.5) Implementation Details
- **Dual-Pane Mouse Click Routing (`VIEW_SPLIT`):**
  - Clicks with `click_x > split_col` in split view are routed directly to the preview pane.
  - The clicked preview visual row is converted to the corresponding buffer line via `g_editor.rendered_doc.lines[rend_idx].source_row`.
  - Single-clicking directly on a Markdown link in the preview pane invokes `handle_follow_link()`, providing instant hypertext navigation without requiring keyboard cursor repositioning.
- **Live Document & Selection Statistics Modal (`Alt+D` / `M-D`):**
  - Implemented in `src/ui_dialog_stats.c` and declared in `src/ui.h`.
  - Computes buffer totals (lines, word count, character count with and without whitespace, estimated reading time at 200 words/min) as well as active selection counts if a mark is set.
- **Bracket & Delimiter Pair Matching Navigation (`Ctrl+]` / `Alt+]`):**
  - Implemented in `src/editor.c` (`editor_jump_matching_pair`).
  - Supports multi-line nested jumping for standard brackets `()`, `[]`, `{}`, `<>`, as well as Markdown formatting spans (`**`, `~~`, inline code `` ` ``, code fences ```` ``` ````, and italic `*`/`_`).

### 7.2 Bottom Shortcut Hint Bar Expansion & Toggle Controls
- **4th Shortcut Hint Row:**
  - Expanded `ShortcutHint` rendering in `src/ui.c` to 4 rows (`s_hints_r1` through `s_hints_r4`) displaying 24 keybindings in total.
  - Row 4 adds: `F9 Outline`, `M-D Stats`, `^] Jump`, `F7 Follow`, `M-J Reflow`, and `F10 Hints`.
  - Responsive thresholds in `ui_get_shortcut_hint_rows()`: 4 rows (≥ 24 screen lines), 3 rows (≥ 18 lines), 2 rows (≥ 12 lines), 1 row (≥ 6 lines), 0 rows (< 6 lines).
- **Function Key Toggle (`F10` / `F12` / `Alt+K`):**
  - Implemented `editor_toggle_keybindings(Editor *ed)` to let users hide/show the bottom shortcut bar at will.
  - When hidden, the status bar moves to the bottom screen row (`rows - 1`) and the editor/preview panes expand to occupy the entire terminal height, gaining 4 full rows of editable screen area.
  - Mouse coordinate boundary checks dynamically adjust `text_max_y` when hints are hidden.
  - User preference persists via `show_keybindings = true/false` in `~/.config/mde/config`.

### 7.3 Keybindings Inventory & Architecture Audit
- **Bottom Field Coverage:** 24 keybindings are surfaced on the bottom screen badges.
- **Non-Surfaced Keybindings (24 distinct actions):**
  - *Markdown & Tool Actions (7):* `F6 / Alt+H` (Insert file link), `F8 / Alt+-` (Link history back), `F4 / Alt+L` (Wrap column), `Ctrl+\ / Alt+R` (Search & Replace), `F3 / Alt+W` (Repeat search forward), `Alt+Q` (Repeat search backward), `Ctrl+Q` (Search backward prompt).
  - *Document & Line Navigation (7):* `Ctrl+A / Home`, `Ctrl+E / End`, `Ctrl+← / Ctrl+→` (word jump), `Ctrl+Y / PgUp`, `Ctrl+V / PgDn`, `Alt+\` (doc start), `Alt+/` (doc end).
  - *Editing & Formatting (7):* `Tab / Alt+}` (Indent), `Shift+Tab / Alt+{` (Unindent), `Enter` (Smart continuation for lists/tasks/quotes), `Ins` (Insert/Overwrite cursor toggle), `Esc` (Clear selection mark / Cancel dialog), `Ctrl+D / Del`, `Ctrl+L` (Redraw screen).
  - *Alternative Aliases (10):* `F1` (`^G`), `Alt+P` (`F2`), `Alt+X` (`F5`), `Alt+O` (`F7`), `Alt+T` (`F9`), `Alt+G` (`^/`), `Alt+]` (`^]`), `F12 / Alt+K` (`F10`), `Ctrl+Space / Ctrl+6` (`Alt+A`).

### 7.4 Verification & Quality Assurance
- **Full Test Suite:** 490 / 490 assertions passing (100.0%) in `bin/test_runner`.
- **Compiler Compliance:** Compiles warning-free with `gcc -std=c11 -Wall -Wextra -pedantic -Werror`.
- **Documentation Parity:** Kept in sync across `README.md`, `CHANGELOG.md`, `man/mde.1`, `scripts/generate_keybindings.py`, `ui_dialog_help.c`, and `project-description.md`.

### 7.5 Document Navigation & Dual-Pane Preview Cursor Synchronization (2026-11-22)
- **Top / Bottom Document Navigation (`Ctrl+Home` / `Ctrl+End`):**
  - Bound `Ctrl+Home` (escape sequence `\033[1;5H` / `\033[7;5~` or curses `kHOM5`/`kHOM3`) to `editor_move_doc_start()` jumping to line 0, column 0.
  - Bound `Ctrl+End` (escape sequence `\033[1;5F` / `\033[8;5~` or curses `kEND5`/`kEND3`) to `editor_move_doc_end()` jumping to the last line and final character column.
- **Synchronized Preview Cursor Indicator (`VIEW_SPLIT`):**
  - In split view mode, the right-hand preview pane renders a visual cursor pointer (`▶ `) in the left margin on the rendered line matching the editor's active source line.
  - The exact character/cell at the matching visual column is highlighted with `A_REVERSE | A_BOLD`, giving users an immediate visual cue of where their cursor is positioned in the rendered document.
- **Split-View Preview Overscroll Beyond Last Line:**
  - In `VIEW_SPLIT`, rendered Markdown documents are frequently taller than raw source text due to heading margins, table formatting, and responsive wrapping.
  - Allowed `scroll_row` in the editor pane to scroll beyond the last buffer line (displaying standard empty line markers `~`) until the preview pane has fully scrolled to its bottom (`preview_scroll_row + view_rows >= rendered_doc.count`).
  - Scrolling up seamlessly scrolls the code pane back into view.

### 7.6 Footnote Hypertext Navigation, Config Persistence & Keyboard Ergonomics (2026-11-22)
- **Configuration Persistence for Keybindings Toggle:**
  - `editor_toggle_keybindings()` automatically persists the user's preference to `~/.config/mde/config` (or `$XDG_CONFIG_HOME/mde/config`).
  - Next editor launch honors `show_keybindings` setting automatically.
- **Footnote Bi-Directional Hypertext Jumping (`Alt+O` / `F7`):**
  - Pressing `Alt+O` / `F7` on a footnote reference `[^note]` jumps the cursor directly to its definition `[^note]:`.
  - Pressing `Alt+O` / `F7` on a footnote definition line `[^note]:` scans from the top of the buffer and jumps back to the *first* reference in the text (when multiple references cite the same footnote).
  - Pushes jump history onto the navigation stack so `F8` / `Alt+-` returns to the exact previous location within the same buffer without disk reloads.
- **Keyboard Ergonomics for International Layouts:**
  - **Jump matching delimiter:** Bound `Alt+M` (`M-M` for Match) and `F11` (alongside `Ctrl+]` / `Alt+]`). Updated bottom badge from `^]` to `M-M Jump` so international users (where `]` requires AltGr) have an easily accessible single-layer shortcut.
  - **Go to Line:** Updated bottom badge from `^/` to `M-G Go Line` (GNU Nano standard `M-G` which does not require Shift/AltGr). Also bound `Alt+:` and added CSI escape sequence parsers for terminal `Ctrl+/`.

### 7.7 Showcase Examples Organization (`examples/` Directory)
- **Directory Migration:**
  - Moved root `demo.md` to `examples/demo.md`.
  - Updated relative hypertext links (`../README.md`, `../LICENSE.md`) and added cross-reference to `advanced.md`.
- **Added `examples/advanced.md`:**
  - Secondary showcase document demonstrating multi-file authoring, relative chapter jumping, link history back-navigation (`F8` / `Alt+-`), and recursive linked export bundling (`F5` / `Alt+X`).
- **Updated Project Documentation:**
  - Updated paths and directory trees in `README.md`, `project-description.md`, and `CHANGELOG.md`.

---

## 8. AI Developer Handover & Continuation Guide

> **Notice for any AI assistant or developer picking up this codebase:**
> Read this section first when resuming development on `mde`.

### 8.1 Repository & Current Snapshot
- **Git Branch:** `main` (commit `e694d4d`).
- **Working Tree:** Clean, no uncommitted changes or untracked temporary files.
- **Test Suite Status:** **490 / 490 assertions passing (100.0%)** in `bin/test_runner`.
- **Compiler Compliance:** Compiles with zero warnings on `gcc -std=c11 -Wall -Wextra -pedantic -Werror`.
- **Primary Headers:**
  - `src/editor.h`: Editor structure and buffer operations.
  - `src/markdown.h`: Lexer, AST spans, inline and block parsers.
  - `src/renderer.h`: Formatted dual-pane preview renderer.
  - `src/theme.h`: Terminal color themes and attributes.
  - `src/ui.h` & `src/ui_internal.h`: Screen layout, status bar, and modular dialogs.
  - `src/config.h`: Persistent configuration in `~/.config/mde/config` (or `$XDG_CONFIG_HOME`).

### 8.2 Build & Verification Workflow
Always run the full build and test suite after making edits:
```bash
make clean && make && make test
```
- Ensure zero warnings emitted under `-Wall -Wextra -pedantic -Werror`.
- Ensure 100% test pass rate across all suites.
- If adding new features, add corresponding unit tests in `tests/test_runner.c`.

### 8.3 Recommended Next Tasks (in Priority Order)

#### Task 1: Link Integrity & Dead Link Validator (Roadmap 6.6)
- **Objective:** Provide an in-editor diagnostic scan of all links in the document.
- **Shortcut:** `F9` (or `Alt+L` / menu command).
- **Behavior:**
  - Scan all inline links `[text](target)` and reference links `[ref]: target`.
  - For local targets (`file.md`, `../doc.md`), check disk existence via `stat()`.
  - For `#anchor` fragments, verify that a corresponding heading slug exists in the target buffer.
  - Display broken/missing links in a navigable modal popup listing line numbers and errors.
  - Pressing `Enter` on an entry jumps the cursor directly to the offending link.

#### Task 2: Interactive Table of Contents (`[TOC]`) Generator (Roadmap 6.6)
- **Objective:** Automatically insert or refresh an indented list of document headings.
- **Behavior:**
  - Scan the active buffer for H1–H6 headings.
  - Generate an indented Markdown list with anchor links: `  - [Heading Title](#heading-slug)`.
  - If cursor is placed on a line with `[TOC]`, replace it with the generated hierarchy.
  - If document contains `<!-- toc -->...<!-- /toc -->`, update the contents between the delimiters in-place without touching surrounding text.

#### Task 3: Color Themes & Interactive User Theme Editor (Roadmap 6.9)
- **Objective:** Expand built-in themes and allow users to create/edit custom themes.
- **Themes to add:** Solarized Dark, Solarized Light, Nord, Dracula, Gruvbox, High-Contrast.
- **Theme Editor:**
  - Modal dialogue inspecting and adjusting color pairs and text attributes (bold, dim, reverse).
  - Side-by-side snippet preview showing instant visual feedback.
  - Save custom themes to `~/.config/mde/themes/<theme_name>.conf`.

#### Task 4: AddressSanitizer & UndefinedBehaviorSanitizer Target (Roadmap 6.8)
- **Objective:** Add `make sanitize` target compiling with
    `-fsanitize=address,undefined -g`.
- **Target:** Verify zero memory leaks, buffer overflows, or undefined
    behavior across all 490+ tests.

### 8.4 Inviolable Quality Criteria & Project Constraints
1. **Pure C11 Standard Library:** Do not introduce external library
   dependencies beyond `ncursesw`.
2. **Warning-Free Compilation:** `-Wall -Wextra -pedantic -Werror` must pass
   cleanly at all times.
3. **Commit Protocol:** Always propose git commit messages in the chat and
   wait for user's explicit confirmation before running `git commit`. Never
   commit unprompted.
4. **Checkin Protocol:** The user is the only one who is allowed to push to
   repo with a `git push` command.
5. **License Safety:** Never quote or output verbatim MIT license text in
   thoughts or responses.
6. **Documentation Synchronization:** Whenever keybindings, features, or CLI
   options change, synchronously update:
   - `README.md`
   - `man/mde.1`
   - `CHANGELOG.md`
   - `scripts/generate_keybindings.py`
   - `src/ui_dialog_help.c`
   - `project-description.md`
