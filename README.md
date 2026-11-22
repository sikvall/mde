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
│  ^G Help      ^O Write Out ^W Where Is  ^K Cut       ^T Task      ^C Location               │
│  ^X Exit      ^R Read File ^S Save      ^U Paste     ^/ Go Line   F2 View                   │
└─────────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Highlights & Features

- **Multiple View Modes (Toggle with `F2` or `Alt+P`)**:
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
  - Press **`F4` / `Alt+L`** inside the editor or pass **`-w <20-255|off>`** / **`--wrap=<20-255|off>`** on the command line to set an arbitrary wrap column between **20 and 255**, or turn it **off** (`0`, `off`, or `none`).
  - The active setting is displayed on the status bar (`W:78` or `W:OFF`) and reflected by the wrap guide (`┆`) on the column ruler.
- **Interactive File Dialogue (`Ctrl+R` Read / Open, `Ctrl+O` Write Out / Save As)**:
  - Browse directories and files visually with directory navigation (`..` parent, subdirectories).
  - Shows file names, directories with `<DIR>`, and file sizes formatted in B, KB, or MB.
  - In Write Out / Save As mode (`Ctrl+O`), an editable filename input field is available with `Tab` switching between list and input box. If the target file already exists, an explicit overwrite warning (`(Y)es / (N)o`) must be answered before the file is overwritten.
  - Arrow keys (`↑`/`↓`/`PgUp`/`PgDn`), `Backspace` to navigate up a directory, `Enter` to open or save, and `Esc` or `Ctrl+C` to cancel.
- **Hypertext File Linking & Navigation (`F6` / `Alt+H`, `F7` / `Alt+O`, `F8` / `Alt+-`)**:
  - **Insert File Link (`F6` / `Alt+H`)**: Opens an interactive file picker to choose an existing file (or type a new target filename), automatically computes a portable relative path, derives a label from the selection or the target's H1 heading, and inserts `[Label](relative/path.md)`.
  - **Follow Links & Footnotes (`F7` / `Alt+O`)**: Jump directly into any local Markdown link `[Label](file.md)`, `#heading` anchor, or footnote reference/definition under the cursor (or on the active line). Pressing `F7` / `Alt+O` on a footnote reference `[^note]` jumps to its definition; pressing it on a definition `[^note]:` jumps back to the first reference in the text.
  - **Link & Footnote History Back (`F8` / `Alt+-`)**: Return to previously visited linked documents or footnote jump points with your exact cursor line and column restored. Prompts to save if a different file is loaded and the current buffer has unsaved edits.
- **Multi-File Linked Export Menu (`F5` / `Alt+X`) — HTML, XML, EPUB, LaTeX**:
  - Export the current Markdown buffer directly to **HTML5** (`.html`), **XML / DocBook 5** (`.xml`), **EPUB 3** (`.epub`), or **LaTeX** (`.tex`).
  - **Automatic Linked-File Bundling**: Recursively discovers all locally linked Markdown files (with cycle protection) and includes them in the export (toggleable with `L` inside the Export Menu):
    - **HTML5 & EPUB 3**: Bundles all linked documents and rewrites internal `.md` links into working hypertext section/chapter links.
    - **LaTeX**: Combines the root document and all linked Markdown sources into **one unified `.tex` document** (single preamble and `\begin{document}...\end{document}` with internal `\hypertarget`/`\hyperlink` cross-references).
    - **XML**: Includes all linked sources as structured `<linked-document>` / DocBook sections with rewritten internal links.
  - Automatically detects and uses **`pandoc`** (invoked safely via `fork`/`execvp`, never a shell) when installed, with full built-in C11 exporters for HTML5, structured XML, and LaTeX as zero-dependency fallbacks (EPUB requires `pandoc`).
  - Pre-fills the output filename based on the current file and updates extensions automatically as you choose a format; press `Tab` to customize the destination path, `L` to toggle linked-file inclusion, or `E` to toggle between Pandoc and Built-in engines. If the destination file already exists, an explicit `(Y)es / (N)o` overwrite confirmation is required before writing.
- **Document Outline (`F9` / `Alt+T`), Search & Replace (`Ctrl+\` / `Alt+R`) & Paragraph Reflow (`Alt+J`)**:
  - **Document Outline (`F9` / `Alt+T`)**: Opens a scrollable heading navigator listing all H1–H6 headings in the active file; press `Enter` to jump directly to any section.
  - **Search & Replace (`Ctrl+\` / `Alt+R`)**: Interactive Nano-style replacement (`Y` Replace, `N` Skip, `A` Replace All, `Esc`/`^C` Cancel) with atomic undo.
  - **Paragraph Reflow (`Alt+J`)**: Re-wraps the current Markdown paragraph (preserving indentation and blockquote `> ` prefixes) to the configured wrap column.
  - **Rich Table & Link Preview**: Markdown tables render with aligned Unicode box-drawing borders (`┌─┬─┐`), and links display directional cues (`↗` external URL, `→` local file, `↓` `#anchor`).
  - **Safe Atomic Saves & Exact Undo Tracking**: File saves resolve symlinks, preserve permissions, and atomically rename temporary files into place; undoing back to the last saved state accurately clears the dirty `[*]` indicator.
- **Nano-Compatible Control Keys & Persistent Bottom Shortcut Hint Rows**:
  - Control and Meta (`Alt`) keybindings are synchronized with GNU `nano` (`^G` Help, `^X` Exit, `^O` Write Out, `^R` Read File, `^S` Save, `^W`/`^Q` Search, `^K` Cut, `^U` Paste, `M-6` Copy, `M-A` Mark, `M-U`/`M-E` Undo/Redo, `^C` Location, `^/` Go to Line, `^Y`/`^V` Page Up/Down, `^B`/`^F`/`^P`/`^N` Navigation).
  - Displays two rows of prominent nano-style shortcut key hints at the bottom of the screen (or three on taller displays $\ge 28$ rows), styled with reverse-video key badges and clear labels.
- **Selection & Clipboard Editing**:
  - **`Alt+A` / `Ctrl+6` / `Ctrl+Space`**: Set or unset selection mark. Move cursor (arrows, `Ctrl+A`, `Ctrl+E`, mouse click) to extend the visual selection highlight (`A_REVERSE`).
  - **`Ctrl+K` / `Alt+6` / `Ctrl+U`**: Cut or copy the active selection (or the entire current line when no mark is set), and paste with undo/redo support. Clipboard is synchronized with terminal/system clipboard via ANSI OSC 52 escape sequences.
  - **`Esc`**: Clear active selection mark (or cancel prompts/dialogs alongside `Ctrl+C`).
- **Smart Markdown Editing Helpers**:
  - **List Auto-Continuation**: Pressing `Enter` on a list item (`-`, `*`, `+`, `1.`) or task box (`- [ ]`) automatically inserts the next item with the correct indentation or incremented numbering. Hitting `Enter` on an empty bullet cancels the list.
  - **Task Checkbox Toggle (`Ctrl+T`)**: Instantly toggle `- [ ]` $\leftrightarrow$ `- [x]` under the cursor, or convert any line into a task item.
  - **Quick Formatting (`Alt+B`)**: Insert bold markers (`**...**`) or wrap the currently selected text.
  - **Quick Navigation (`Ctrl+A` / `Ctrl+E` / `Ctrl+/`)**: Jump to beginning (`Home`) or end (`End`) of current line, or jump to any line/column with `Ctrl+/` (`Alt+G`).
- **Full UTF-8 & Unicode Support**:
  - Accurate terminal column calculation via `wcwidth`.
  - Multi-byte character navigation and wide characters (emojis, box drawing, accented characters).
- **Steady Non-Flashing Cursor & Mode Switching (`Ins`)**:
  - The cursor remains steady and non-flashing at all times for distraction-free writing.
  - Insert mode (default) features a steady solid block cursor (`█`).
  - Pressing **`Ins`** toggles Overwrite mode with a steady underscore cursor (`_`).
- **Two-Tab Interactive Help Overlay (`Ctrl+G` / `F1`)**:
  - **Tab 1: Editor Cheat Sheet**: Quick reference for all editor keybindings, navigation, and controls.
  - **Tab 2: Markdown Syntax Guide**: Learn about all Markdown functions (headings, styles, lists, tasks, blockquotes, code blocks, tables, rules, and smart features) with scrollable reference.
  - Switch tabs with `Tab`, `1`, `2`, or `←`/`→`, scroll with `↑`/`↓` or `PgUp`/`PgDn`, and close with `Esc`, `q`, `Ctrl+G`, or `Ctrl+X`.

---

## Centralized Versioning

All application metadata, title, version, and build date are defined centrally in [`src/version.h`](src/version.h):

```c
#define MDE_APP_NAME        "mde"
#define MDE_VERSION_STRING  "0.1.1"
#define MDE_BUILD_DATE      __DATE__
#define MDE_BUILD_TIME      __TIME__
```

Updating this single file automatically updates the CLI output (`mde -v`), the help overlay, and the Debian package build name!

---

## Command-Line Options

| Option | Description |
|---|---|
| **`-h`, `--help`** | Display command-line usage and keybindings summary |
| **`-v`, `--version`** | Print all version and build metadata from `src/version.h` (app name, title, version string & major.minor.patch, description, build date/time, author, maintainer) |
| **`-w <cols>`, `--wrap <cols>`, `--wrap=<cols>`** | Set automatic line break column width (`20`–`255`, or `off`/`0` to disable; default `78`) |

---

## Keybindings Reference (Nano-Compatible)

| Shortcut | Description |
|---|---|
| **`Ctrl+G` / `F1`** | Get Help overlay (Tab 1: Editor Cheat Sheet, Tab 2: Markdown Guide) |
| **`Ctrl+X`** | Exit editor (prompts to save unsaved changes) |
| **`Ctrl+S`** | Save current file directly (opens Write Out dialogue if untitled) |
| **`Ctrl+O`** | Write Out (Save As file dialogue to choose directory & name) |
| **`Ctrl+R`** | Read / Open file dialogue to browse and open a file |
| **`F5` / `Alt+X`** | Open Export Menu (**HTML**, **XML**, **EPUB**, **LaTeX**; bundles linked `.md` files) |
| **`F6` / `Alt+H`** | Insert hypertext file link `[Label](relative/file.md)` via file picker |
| **`F7` / `Alt+O`** | Follow Markdown file link or `#heading` anchor under cursor |
| **`F8` / `Alt+-`** | Navigate back to previous linked file & cursor position |
| **`F9` / `Alt+T`** | Open Document Outline modal to jump to any H1–H6 heading |
| **`Alt+D`** | Live document and selection statistics modal (words, chars, reading time) |
| **`M-M` / `F11` / `^]`** | Jump to matching bracket `()`, `[]`, `{}`, `<>`, or delimiter pair (`**`, `~~`, `` ` ``, ```` ``` ````; also `Alt+]`) |
| **`F10` / `F12` / `Alt+K`** | Toggle bottom shortcut hint rows (hide / show) |
| **`F2` / `Alt+P`** | Cycle View Modes (**Split** → **In-Place** → **Preview**) |
| **`F4` / `Alt+L`** | Set automatic line break column (`20`–`255` or `off`; default `78`) |
| **`Ctrl+L`** | Refresh / redraw screen |
| **`Ctrl+W`** | Where Is — Search forward for text |
| **`Ctrl+Q`** | Where Was — Search backward for text |
| **`Ctrl+\` / `Alt+R`** | Search and Replace (Yes / No / All) |
| **`Alt+W` / `F3`** | Repeat search forward (`Alt+Q` repeats backward) |
| **`M-G` / `Alt+G` / `^_`** | Go to line number (and optional column; also `Ctrl+/`) |
| **`Ctrl+C`** | Display current cursor location (also cancels prompts/dialogs) |
| **`Alt+A` / `Ctrl+6` / `Ctrl+Space`** | Set or unset selection mark anchor (move cursor to select) |
| **`Ctrl+K`** | Cut active selection, or cut entire current line if no mark is set |
| **`Alt+6`** | Copy active selection, or copy entire current line if no mark is set |
| **`Ctrl+U`** | Paste (Uncut) text from clipboard |
| **`Esc`** | Clear active selection mark (Cancel in prompts/dialogs) |
| **`Alt+U`** | Undo last change |
| **`Alt+E`** | Redo last undone change |
| **`Alt+J`** | Reflow (re-wrap) current Markdown paragraph to wrap column |
| **`Ctrl+T`** | Toggle checkbox `[ ]` <-> `[x]` on current line |
| **`Alt+B`** | Insert Bold markers (`**...**`) or wrap selection |
| **`Ctrl+A` / `Home`** | Jump to beginning of line |
| **`Ctrl+E` / `End`** | Jump to end of line |
| **`Ctrl+←` / `Ctrl+→`** | Jump backward / forward by one word |
| **`Ctrl+Y` / `PgUp`** | Previous Page (scroll viewport up) |
| **`Ctrl+V` / `PgDn`** | Next Page (scroll viewport down) |
| **`Ctrl+Home` / `Alt+\`** | Jump to first line of document |
| **`Ctrl+End` / `Alt+/`** | Jump to last line of document |
| **`Ctrl+B` / `F` / `P` / `N`** | Move cursor Left / Right / Up / Down (or Arrow keys) |
| **`Ctrl+H` / `Backspace`** | Delete character before cursor |
| **`Ctrl+D` / `Del`** | Delete character under cursor |
| **`Enter`** | Smart list continuation & auto-indent |
| **`Tab` / `Alt+}`** | Indent line by 2 spaces |
| **`Shift+Tab` / `Alt+{`** | Dedent line by 2 spaces |
| **`Ins`** | Toggle Insert / Overwrite mode (steady block vs. steady underscore cursor) |

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
sudo dpkg -i bin/mde_0.1.1_amd64.deb
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
mde examples/demo.md

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
├── examples/           # Sample Markdown documents demonstrating features
│   ├── demo.md         # Comprehensive interactive showcase document
│   └── advanced.md     # Multi-file navigation & linked export showcase
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
    └── test_runner.c   # Comprehensive C11 unit test suite (395 tests)
```

---

## License

MIT License.
