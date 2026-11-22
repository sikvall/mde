# mde — Project Description & AI Development Guide

> Purpose of this file: a comprehensive briefing for any AI assistant (or human) working on
> **mde**. It details what the project is, its architectural design, build system, current state,
> recent upgrades, git history, and the remaining roadmap items.
>
> Snapshot basis: **v0.1.1** (2026-11-22), pure C11 codebase compiling warning-free with `gcc` and `ncursesw`.
> Test runner passes **395 / 395 tests (100.0%)**.

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
./bin/mde demo.md             # Open file with default 78-column auto line break
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
├── demo.md                # Interactive sample document demonstrating mde features
├── man/
│   └── mde.1              # UNIX manual page
├── scripts/
│   └── generate_keybindings.py # Single-source keybinding table generator
├── src/
│   ├── version.h          # Centralized version macros and app metadata
│   ├── config.h / .c      # Configuration file loader (~/.config/mde/config or ~/.mde/config)
│   ├── backup.h / .c      # Emacs-style backup (~) and autosave (#...#) handling
│   ├── buffer.h / .c      # Line-based dynamic text buffer with CRLF detection & atomic saves
│   ├── editor.h / .c      # Editor state, cursor, selection, clipboard, wrap, links, search
│   ├── export.h / .c      # HTML5, XML, EPUB, LaTeX multi-file export & pandoc backend
│   ├── markdown.h / .c    # Streaming Markdown lexer and span tokenizer
│   ├── renderer.h / .c    # Formatted preview renderer with responsive soft-wrapping & tables
│   ├── theme.h / .c       # 256-color & 16-color ncurses palette & token styles
│   ├── ui.h / .c          # Screen layout, ruler, file browser, status bar, dialogs
│   ├── undo.h / .c        # Multi-level undo/redo action stacks with group IDs
│   ├── utf8.h / .c        # Multi-byte UTF-8 length, boundary, and wcwidth visual column math
│   └── main.c             # Event loop, CLI parsing, signal handling, key & mouse dispatch
└── tests/
    └── test_runner.c      # Comprehensive C11 unit test suite (395 assertions)
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

