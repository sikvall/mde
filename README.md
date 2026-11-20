# mde - Markdown Editor (TUI)

**mde** is a high-performance, full-screen text-mode (TUI) editor specialized for editing and displaying Markdown (`.md`) files. Written in **pure C11** using `ncursesw`.

```
┌──────────────────────────────────────────────┬──────────────────────────────────────────────┐
│ COL │ ····+···10···+···20···+···30···+···40  │   PREVIEW                                    │
│ ▶ 1 │ # Project Roadmap                      │   Project Roadmap                            │
│   2 │                                        │   ═══════════════════════════════════════    │
│   3 │ ## Current Sprint                      │   Current Sprint                             │
│   4 │ - [x] Core line buffer & UTF-8 engine  │   ───────────────────────────────────────    │
│   5 │ - [x] Markdown lexer & parser          │   ☑  Core line buffer & UTF-8 engine         │
│   6 │ - [ ] Export to HTML / PDF             │   ☑  Markdown lexer & parser                 │
│   7 │                                        │   ☐  Export to HTML / PDF                    │
│   8 │ ```c                                   │   ┌── [ c ] ─────────────────────────────    │
│   9 │ printf("Hello mde!\n");                │   │ printf("Hello mde!\n");                  │
│  10 │ ```                                    │   └──                                        │
│   ~ │                                        │                                              │
├─────┴────────────────────────────────────────┴──────────────────────────────────────────────┤
│ [ SPLIT ]  demo.md [*]                    INS | W:78 | Ln 1, Col 1 | 38 words | UTF-8       │
│  ^S Save      ^O Open      ^W Save As   ^P View      ^F Find      ^Q Quit                   │
│  ^X Cut       ^C Copy      ^V Paste     ^K Kill      ^Z Undo      ^H Help                   │
└─────────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Highlights & Features

- **Multiple View Modes (Toggle with `Ctrl+P` or `F2`)**:
  - **Dual-Pane Split View**: Live rendered Markdown preview on the right side while editing on the left, with synchronized vertical scrolling.
  - **In-Place Rich Mode**: Full-screen editor featuring syntax-highlighted headers, formatted inlines, and dimmed Markdown markers.
  - **Fullscreen Rendered Editor**: Formatted Markdown document view with live rendering and full interactive editing with a visible cursor.
- **Active Line Highlighting & Column Ruler**:
  - **Active Line Gutter Marker (`▶`)**: Visually indicates the currently active line in the gutter.
  - **Prominent Active Markers**: Syntax markers (`#`, `**`, `*`, `` ` ``, `~~`, `[ ]`, `[x]`, `>`) on the active line are highlighted vibrantly in bold accent color so you can clearly see and edit Markdown delimiters.
  - **Column Ruler**: A top-aligned ruler displaying ticks (`·`), 5-column markers (`+`), 10-column tens numbers, a dynamic wrap guide (`┆`) at the active line break column, and an interactive cursor column pointer (`▼`).
- **Automatic Line Break (Default: 78 Columns)**:
  - Automatically wraps lines at word boundaries when typing past the configured column limit, stripping break whitespace and moving the wrapped word and cursor seamlessly onto the next line.
  - Falls back to a UTF-8 boundary hard break for unbroken tokens exceeding the column limit, and preserves leading indentation and Markdown blockquote (`> `) prefixes on continuation lines.
  - Press **`Ctrl+L` / `F4`** inside the editor or pass **`-w <20-255|off>`** / **`--wrap=<20-255|off>`** on the command line to set an arbitrary wrap column between **20 and 255**, or turn it **off** (`0`, `off`, or `none`).
  - The active setting is displayed on the status bar (`W:78` or `W:OFF`) and reflected by the wrap guide (`┆`) on the column ruler.
- **Interactive File Dialogue (`Ctrl+O` Open / `Ctrl+W` Save As)**:
  - Browse directories and files visually with directory navigation (`..` parent, subdirectories).
  - Shows file names, directories with `<DIR>`, and file sizes formatted in B, KB, or MB.
  - In Save As mode (`Ctrl+W`), an editable filename input field is available with `Tab` switching between list and input box.
  - Arrow keys (`↑`/`↓`/`PgUp`/`PgDn`), `Backspace` to navigate up a directory, `Enter` to open or save, and `Esc` to cancel.
- **Export Menu (`F5` / `Ctrl+D`) — HTML, XML, EPUB, LaTeX**:
  - Export the current Markdown buffer directly to **HTML5** (`.html`), **XML / DocBook 5** (`.xml`), **EPUB 3** (`.epub`), or **LaTeX** (`.tex`).
  - Automatically detects and uses **`pandoc`** (invoked safely via `fork`/`execvp`, never a shell) when installed, with full built-in C11 exporters for HTML5, structured XML, and LaTeX as zero-dependency fallbacks (EPUB requires `pandoc`).
  - Pre-fills the output filename based on the current file and updates extensions automatically as you choose a format; press `Tab` to customize the destination path or `E` to toggle between Pandoc and Built-in engines.
- **Persistent Bottom Shortcut Hint Rows**:
  - Displays two rows of prominent shortcut key hints at the bottom of the screen (or three on taller displays $\ge 28$ rows), styled with reverse-video key badges and clear labels.
- **Selection & Clipboard Editing**:
  - **`Ctrl+Space`**: Set mark start. Move cursor (arrows, `Ctrl+A`, `Ctrl+E`, mouse click) to extend the visual selection highlight (`A_REVERSE`).
  - **`Ctrl+X` / `Ctrl+C` / `Ctrl+V`**: Cut, copy, and paste text with undo/redo support. Clipboard is synchronized with terminal/system clipboard via ANSI OSC 52 escape sequences.
  - **`Ctrl+K`**: Kill text from cursor to end of line, or join with next line if at end of line.
  - **`Esc` / `Ctrl+G`**: Clear active selection mark.
- **Smart Markdown Editing Helpers**:
  - **List Auto-Continuation**: Pressing `Enter` on a list item (`-`, `*`, `+`, `1.`) or task box (`- [ ]`) automatically inserts the next item with the correct indentation or incremented numbering. Hitting `Enter` on an empty bullet cancels the list.
  - **Task Checkbox Toggle (`Ctrl+T`)**: Instantly toggle `- [ ]` $\leftrightarrow$ `- [x]` under the cursor, or convert any line into a task item.
  - **Quick Formatting (`Ctrl+B`)**: Insert bold markers (`**...**`) or wrap the currently selected text.
  - **Quick Navigation (`Ctrl+A` / `Ctrl+E`)**: Jump to beginning (`Home`) or end (`End`) of current line.
- **Full UTF-8 & Unicode Support**:
  - Accurate terminal column calculation via `wcwidth`.
  - Multi-byte character navigation and wide characters (emojis, box drawing, accented characters).
- **Steady Non-Flashing Cursor & Mode Switching (`Ins`)**:
  - The cursor remains steady and non-flashing at all times for distraction-free writing.
  - Insert mode (default) features a steady solid block cursor (`█`).
  - Pressing **`Ins`** toggles Overwrite mode with a steady underscore cursor (`_`).
- **Two-Tab Interactive Help Overlay (`F1` / `Ctrl+H`)**:
  - **Tab 1: Editor Cheat Sheet**: Quick reference for all editor keybindings, navigation, and controls.
  - **Tab 2: Markdown Syntax Guide**: Learn about all Markdown functions (headings, styles, lists, tasks, blockquotes, code blocks, tables, rules, and smart features) with scrollable reference.
  - Switch tabs with `Tab`, `1`, `2`, or `←`/`→`, scroll with `↑`/`↓` or `PgUp`/`PgDn`, and close with `Esc` or `q`.

---

## Centralized Versioning

All application metadata, title, version, and build date are defined centrally in [`src/version.h`](file:///home/anders/mde/src/version.h):

```c
#define MDE_APP_NAME        "mde"
#define MDE_VERSION_STRING  "0.1.0"
#define MDE_BUILD_DATE      __DATE__
#define MDE_BUILD_TIME      __TIME__
```

Updating this single file automatically updates the CLI output (`mde -v`), the help overlay, and the Debian package build name!

---

## Command-Line Options

| Option | Description |
|---|---|
| **`-h`, `--help`** | Display command-line usage and keybindings summary |
| **`-v`, `--version`** | Print version string and build date/time |
| **`-w <cols>`, `--wrap <cols>`, `--wrap=<cols>`** | Set automatic line break column width (`20`–`255`, or `off`/`0` to disable; default `78`) |

---

## Keybindings Reference

| Shortcut | Description |
|---|---|
| **`Ctrl+S`** | Save file (opens Save As dialogue if untitled) |
| **`Ctrl+O`** | Open File dialogue to browse and open a file |
| **`Ctrl+W`** | Save As file dialogue to choose directory & name |
| **`F5` / `Ctrl+D`** | Open Export Menu (**HTML**, **XML**, **EPUB**, **LaTeX**) |
| **`Ctrl+Q`** | Quit editor (prompts to save unsaved changes) |
| **`Ctrl+P` / `F2`** | Cycle View Modes (**Split** → **In-Place** → **Preview**) |
| **`Ctrl+L` / `F4`** | Set automatic line break column (`20`–`255` or `off`; default `78`) |
| **`Ctrl+T`** | Toggle checkbox `[ ]` <-> `[x]` on current line |
| **`Ctrl+Space`** | Set selection mark start (move cursor to select) |
| **`Ctrl+X`** | Cut selected text to clipboard |
| **`Ctrl+C`** | Copy selected text to clipboard |
| **`Ctrl+V`** | Paste text from clipboard |
| **`Ctrl+K`** | Kill to end of line / join with next line |
| **`Ctrl+A` / `Home`** | Jump to beginning of line |
| **`Ctrl+E` / `End`** | Jump to end of line |
| **`Esc` / `Ctrl+G`** | Clear active selection mark |
| **`Ctrl+B`** | Insert Bold markers (`**...**`) or wrap selection |
| **`Ctrl+Z`** | Undo last change |
| **`Ctrl+Y`** | Redo last undone change |
| **`Ctrl+F`** | Search / Find text |
| **`F3`** | Jump to next search match |
| **`F1` / `Ctrl+H`** | Show Help overlay (Tab 1: Editor Cheat Sheet, Tab 2: Markdown Guide) |
| **`Enter`** | Smart list continuation & auto-indent |
| **`Tab`** | Indent line by 2 spaces |
| **`Shift+Tab`** | Dedent line by 2 spaces |
| **`Ins`** | Toggle Insert / Overwrite mode (steady block vs. steady underscore cursor) |
| **Arrows** | Cursor navigation |
| **PageUp / PageDown** | Scroll viewport by page |

---

## Building from Source

### Prerequisites

- A C11-compliant compiler (`gcc` or `clang`)
- `make`
- `ncursesw` development headers (`libncursesw5-dev` on Debian/Ubuntu, `ncurses-devel` on Fedora/RHEL, `ncurses` on Arch/macOS)

### Compile

```bash
make
```

The compiled binary will be placed at `bin/mde`.

### Run Test Suite

```bash
make test
```

### Build Debian Package (.deb)

```bash
make deb
```

Builds an installable Debian package at `bin/mde_<version>_<arch>.deb` (includes binary, man page `man1/mde.1.gz`, documentation, and control file).

To install the built package:

```bash
sudo dpkg -i bin/mde_0.1.0_amd64.deb
```

### Install Directly

```bash
sudo make install
```

---

## Usage

```bash
# Open an existing Markdown document (default 78-column auto line break)
mde notes.md

# Open with a custom automatic line break at 100 columns
mde -w 100 notes.md

# Open with automatic line break disabled
mde --wrap=off notes.md

# Open sample demo file
mde demo.md

# View help and version
mde --help
mde --version
```

---

## Project Structure

```
mde/
├── Makefile            # Build system (all, test, clean, install, deb)
├── README.md           # Documentation
├── demo.md             # Sample Markdown document demonstrating features
├── man/
│   └── mde.1           # Standard UNIX man page
├── src/
│   ├── version.h       # Centralized name, version, and build date
│   ├── buffer.h / .c   # Text buffer with UTF-8 & dynamic reallocation
│   ├── editor.h / .c   # Editor state, auto line break, selection & Markdown helpers
│   ├── export.h / .c   # Export to HTML, XML, EPUB, LaTeX (pandoc + built-in C11)
│   ├── markdown.h / .c # Markdown lexer, AST spans & block/inline tokenizer
│   ├── renderer.h / .c # Formatted preview renderer with synchronized line mapping
│   ├── theme.h / .c    # 256-color & 16-color ncurses palette & token styles
│   ├── ui.h / .c       # Screen layout, column ruler, file dialog & status bar
│   ├── undo.h / .c     # Multi-level undo & redo action stacks
│   ├── utf8.h / .c     # UTF-8 boundary checks, visual column conversions (wcwidth)
│   └── main.c          # Event loop, CLI parsing, signals, key & mouse dispatch
└── tests/
    └── test_runner.c   # Comprehensive C11 unit test suite (237 tests)
```

---

## License

MIT License.
