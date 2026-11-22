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
  - Automatically detects and uses **`pandoc`** (invoked safely via `fork`/`execvp`, never a shell) when installed, with full built-in C11 exporters for HTML5, structured XML, EPUB 2/3, and LaTeX as zero-dependency native fallbacks.
  - **Native EPUB 2/3 & Standalone `md2epub` CLI Tool**: EPUB export is powered natively in C11 using `libzip-dev`, creating compliant Open Container Format (OCF) archives with uncompressed `mimetype` at offset 0, automatic chapter splitting on `# ` headings and linked files, YAML front-matter metadata parsing, local image bundling, and multi-level table-of-contents navigation (`nav.xhtml` and `toc.ncx`). You can also use the standalone `md2epub` command directly from the shell (`md2epub book.md -o book.epub`).
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
#define MDE_VERSION_STRING  "0.1.2"
#define MDE_BUILD_DATE      __DATE__
#define MDE_BUILD_TIME      __TIME__
```

Updating this single file automatically updates the CLI output (`mde -v`), the help overlay, and the Debian package build name!

---

## Native EPUB Conversion & Book Publishing

`mde` features a native, pure C11 publishing pipeline that converts Markdown documents and linked multi-file document trees into standard, publication-grade **EPUB 3** and **EPUB 2** electronic books. It requires **no external runtimes** such as Pandoc, Python, Java, or Node.js—everything is built directly into `mde` and the accompanying `md2epub` CLI tool using `libzip`.

### Key Capabilities

- **Modern EPUB 3 & Legacy EPUB 2**: Produces modern EPUB 3.2 e-books featuring semantic HTML5 `<nav epub:type="toc">` navigation by default, or EPUB 2.0.1 files with XML `<navMap>` (`toc.ncx`) for older e-readers and hardware devices.
- **Strict Open Container Format (OCF) Compliance**: The resulting `.epub` file is a valid ZIP archive containing an uncompressed `mimetype` entry stored at byte offset 0 (`ZIP_CM_STORE`), followed by `META-INF/container.xml`, `EPUB/package.opf`, chapter XHTML files, and stylesheets.
- **Automated Chapter Splitting**: Automatically divides single large documents into clean, standalone XHTML chapter files at every top-level H1 (`# `) heading boundary.
- **Multi-File Document & Book Bundling**: Seamlessly discovers all locally linked Markdown documents (`[Chapter 1](chap01.md)`), traverses the link graph, and packages each file into an individual chapter document (`ch001.xhtml`, `ch002.xhtml`, etc.) with internal relative links automatically rewritten into working intra-archive chapter anchors.
- **Interactive Metadata Editing (`Alt+I`)**: Full in-editor modal dialogue to inspect and update common book metadata, synchronized directly with YAML front matter.
- **Cover Image & Asset Bundling**: Automatically embeds cover pictures and all referenced local images (`![alt](images/photo.png)`) directly into `EPUB/images/`, declaring their MIME types and properties in `package.opf`.
- **Typographic Stylesheet**: Ships with a built-in clean typography CSS designed for high legibility on e-ink readers and mobile screens, or accepts a custom CSS file.

---

### In-Editor Export Workflow (`F5` / `Alt+X`)

You can export any Markdown buffer directly from the editor:

1. Press **`F5`** (or **`Alt+X`**) to open the interactive **Export Menu**.
2. Press **`3`** to select **EPUB (`.epub`)**.
3. **Options within the menu**:
   - **`L`**: Toggle recursive linked-file inclusion (`ON` / `OFF`).
   - **`E`**: Toggle export engine between **Built-in** (native C11) and **Pandoc**.
   - **`M`**: Jump directly into the **Document Metadata** screen to edit author, title, cover image, and identifiers before exporting.
   - **`Tab`**: Edit the destination `.epub` file path.
4. Press **`Enter`** to generate the e-book.

---

### Document Metadata Screen (`Alt+I`)

Press **`Alt+I`** (or **`M`** inside the Export Menu) to open the interactive Metadata Editor.

| Field | Description | Default Fallback |
|---|---|---|
| **Title** | The book's title | First H1 heading in file, or filename stem |
| **Author** | Author or creator name | Current login user (`$USER`) |
| **Language** | BCP 47 language code (e.g., `en`, `sv`, `de`, `fr`) | `en` |
| **Date** | Publication date in `YYYY-MM-DD` or ISO 8601 | Today's date |
| **Publisher** | Publishing entity or imprint | *(optional)* |
| **Identifier** | Publication ID (UUID URN, ISBN, or URI) | Auto-generated UUID URN |
| **Rights** | Copyright notice or license terms | *(optional)* |
| **Description** | Book abstract, summary, or blurb | *(optional)* |
| **Cover Picture** | Path/filename to book cover image | *(optional; press `^R` or `F6` to browse)* |
| **Custom CSS** | Path to custom stylesheet override | *(optional; defaults to built-in typography)* |

#### Metadata Screen Shortcuts
- **`Tab` / `↓`**: Move to next field.
- **`Shift+Tab` / `↑`**: Move to previous field.
- **`^R` / `F6`**: Open the interactive file browser to browse directories and pick a cover picture or CSS file.
- **`^U`**: Clear active input field.
- **`Ctrl+S`** or select **`[ Save to Document ]`**: Atomically updates or inserts YAML front matter at the top of your document and marks the buffer modified.
- **`Esc`** or select **`[ Cancel ]`**: Discard edits and close modal.

---

### Multi-File Book Authoring

`mde` makes authoring multi-chapter books effortless. Rather than managing one massive monolithic file, you can organize your book as a set of modular Markdown documents.

#### Recommended Directory Layout

Look at the included showcase book in `examples/book/`:

```
examples/book/
├── main.md        # Master book file: front matter, introduction & table of contents
├── chap01.md      # Chapter 1: The Physics of Stubborn Birches
├── chap02.md      # Chapter 2: Space Weather and the 14 MHz Silence
├── chap03.md      # Chapter 3: The Ghost in the Morse Code
└── cover.jpg      # Optional cover picture
```

#### The Master File (`main.md`)

The root document contains your book's metadata front matter and links to individual chapter files:

```markdown
---
title: "The Reluctant Antenna"
author: "Täpp Anders Sikvall"
language: en
date: 2026-11-22
publisher: "Fika & Wire Press"
identifier: "urn:uuid:sm5uei-antenna-saga-2026"
rights: "CC BY-SA 4.0"
cover: "cover.jpg"
---

# The Reluctant Antenna

## Table of Contents

1. [Chapter 1: The Physics of Stubborn Birches](chap01.md)
2. [Chapter 2: Space Weather and the 14 MHz Silence](chap02.md)
3. [Chapter 3: The Ghost in the Morse Code](chap03.md)
```

#### Navigating Between Chapters
- While writing or reading in `mde`, position your cursor on `[Chapter 1](chap01.md)` and press **`F7`** (or **`Alt+O`**) to follow the link and open the chapter immediately.
- Press **`F8`** (or **`Alt+-`**) to jump back to your previous location in `main.md`.

#### Compiling the Book
When you export `main.md` to EPUB via **`F5`** or via `md2epub`:
1. `mde` parses `main.md` and reads the YAML front matter.
2. It detects the links to `chap01.md`, `chap02.md`, and `chap03.md`.
3. It recursively compiles each file into its own clean XHTML document (`ch001.xhtml` through `ch004.xhtml`).
4. Internal Markdown links between chapters (`[← Back](chap01.md)`) are automatically converted into relative intra-e-book anchors (`ch002.xhtml`).
5. A comprehensive Table of Contents is generated for the e-reader's navigation pane.

---

### YAML Front-Matter Reference

You can configure all book properties directly in your Markdown file using a YAML block delimited by `---` at line 0:

```yaml
---
title: "The Reluctant Antenna"
author: "Täpp Anders Sikvall"
language: en
date: 2026-11-22
publisher: "Fika & Wire Press"
identifier: "urn:isbn:9780123456789"
rights: "CC BY-SA 4.0"
description: "A short, dryly humorous chronicle of wire antennas and solar tantrums."
cover: "images/cover.png"
css: "styles/custom.css"
---
```

Recognized keys (case-insensitive):
- **`title`**: Book title.
- **`author`** / **`creator`**: Author name.
- **`language`** / **`lang`**: Language code (default: `en`).
- **`date`**: Publication date.
- **`publisher`**: Publisher or organization.
- **`identifier`** / **`id`** / **`isbn`** / **`uuid`**: Unique book identifier.
- **`rights`** / **`copyright`**: Copyright / licensing statement.
- **`description`** / **`summary`**: Short abstract or blurb.
- **`cover`** / **`cover-picture`** / **`cover_picture`** / **`cover-image`**: Path to cover picture.
- **`css`** / **`stylesheet`**: Path to custom CSS file.

---

### Standalone CLI Converter: `md2epub`

In addition to the interactive editor, `mde` builds a high-performance command-line binary `bin/md2epub`.

```bash
md2epub [OPTIONS] input.md [output.epub]
```

#### CLI Options Reference

| Option | Long Option | Description |
|---|---|---|
| `-o <file>` | `--output <file>` | Destination `.epub` path (defaults to `<input_stem>.epub`) |
| `-3` | `--epub3` | Generate modern EPUB 3 e-book (default) |
| `-2` | `--epub2` | Generate legacy EPUB 2.0.1 e-book with NCX navigation |
| `-t <str>` | `--title <str>` | Override book title |
| `-a <str>` | `--author <str>` | Override author name |
| `-l <code>`| `--lang <code>` | Override BCP 47 language code (`en`, `sv`, etc.) |
| | `--date <str>` | Set publication date (`YYYY-MM-DD`) |
| | `--publisher <str>`| Set publisher name |
| | `--identifier <str>`| Set publication ID (ISBN, UUID, etc.) |
| | `--rights <str>` | Set copyright or license statement |
| | `--description <str>`| Set abstract or summary |
| | `--cover <file>` | Embed image file as the book cover |
| `-c <file>`| `--css <file>` | Use custom stylesheet instead of built-in CSS |
| | `--no-split` | Do not split chapters on H1 headings |
| | `--no-linked` | Do not follow or bundle locally linked `.md` files |
| `-q` | `--quiet` | Suppress progress output |
| `-v` | `--version` | Display version and build information |
| `-h` | `--help` | Display options summary |

#### Usage Examples

```bash
# Convert a single document to EPUB 3
md2epub notes.md

# Compile a multi-file book with cover picture
md2epub examples/book/main.md -o /tmp/The_Reluctant_Antenna.epub

# Build an EPUB 2 e-book for older e-readers with custom metadata
md2epub -2 --cover cover.jpg --title "My Novel" --author "Jane Doe" manuscript.md

# Custom output with quiet mode for build scripts
md2epub -q -o release/handbook.epub handbook.md
```

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
| **`F5` / `Alt+X`** | Open Export Menu (**HTML**, **XML**, **EPUB**, **LaTeX**; press `M` to edit metadata; bundles linked `.md` files) |
| **`Alt+I`** | Document Metadata editor (Title, Author, Language, Date, Publisher, ID, Rights, Cover, CSS) |
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
- `libzip` development headers (`libzip-dev` on Debian/Ubuntu, `libzip-devel` on Fedora/RHEL, `libzip` on Arch/macOS)

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
sudo dpkg -i bin/mde_0.1.2_amd64.deb
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
mde examples/demo.md
mde --help
mde --version

# Running md2epub standalone converter
md2epub examples/demo.md               # Creates examples/demo.epub (EPUB 3)
md2epub -2 --cover cover.jpg book.md   # Creates book.epub (EPUB 2 with cover)
md2epub -o dist/book.epub --title "Title" --author "Author" book.md
md2epub --help
```

---

## Project Structure

```
mde/
├── Makefile            # Build system (all, test, clean, install, deb)
├── README.md           # Documentation
├── examples/           # Sample Markdown documents demonstrating features
│   ├── demo.md         # Comprehensive interactive showcase document
│   ├── advanced.md     # Multi-file navigation & linked export showcase
│   └── book/           # Multi-file book showcase (main.md & chap01-03.md)
├── man/
│   ├── mde.1           # Standard UNIX man page for editor
│   └── md2epub.1       # UNIX man page for EPUB converter
├── src/
│   ├── version.h       # Centralized name, version, and build date
│   ├── buffer.h / .c   # Text buffer with UTF-8 & dynamic reallocation
│   ├── editor.h / .c   # Editor state, auto line break, selection & Markdown helpers
│   ├── epub.h          # Native EPUB2/EPUB3 converter API
│   ├── export.h / .c   # Export dispatcher (HTML, XML, EPUB, LaTeX)
│   ├── export_epub.c   # Native C11 EPUB 2/3 builder using libzip
│   ├── export_html.c   # Built-in HTML5 exporter
│   ├── export_latex.c  # Built-in unified LaTeX exporter
│   ├── export_xml.c    # Built-in structured XML exporter
│   ├── md2epub_main.c  # Standalone CLI binary entry point
│   ├── markdown.h / .c # Markdown lexer, AST spans & block/inline tokenizer
│   ├── renderer.h / .c # Formatted preview renderer with synchronized line mapping
│   ├── theme.h / .c    # 256-color & 16-color ncurses palette & token styles
│   ├── ui.h / .c       # Screen layout, column ruler, file dialog & status bar
│   ├── ui_dialog_metadata.c # Interactive document & book metadata editor modal
│   ├── undo.h / .c     # Multi-level undo & redo action stacks
│   ├── utf8.h / .c     # UTF-8 boundary checks, visual column conversions (wcwidth)
│   └── main.c          # Event loop, CLI parsing, signals, key & mouse dispatch
└── tests/
    └── test_runner.c   # Comprehensive C11 unit test suite (576 tests)
```

---

## License

MIT License.
