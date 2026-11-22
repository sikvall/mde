# Changelog

All notable changes to mde (Markdown Editor) will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.1.2] - 2026-11-22

### Added
- Interactive Document Metadata screen (`Alt+I` / Export Menu `[M]`) with a dedicated Cover Picture path/filename field (`meta.cover_path`) to specify and embed the book cover image in EPUB exports.
- Optional interactive file browser trigger (`^R` / `F6`) within the Metadata screen to browse and select cover picture files directly from the filesystem.
- Flexible YAML front-matter parsing recognizing `cover`, `cover-image`, `cover_image`, `cover-picture`, and `cover_picture` keys.
- Atomic document front-matter updates (`editor_update_front_matter`) synchronizing metadata changes to the active buffer with dirty state tracking and undo support.
- Standalone verification script (`scripts/verify_no_pandoc.sh`) validating all export formats (HTML5, XML/DocBook 5, EPUB 2/3, LaTeX) in pandoc-free environments.
- Expanded test suite with comprehensive EPUB 2/3, pandoc-free export, and metadata editing verification in `tests/test_runner.c` (576 / 576 passing tests, 100% pass rate).

## [0.1.1] - 2026-11-22

### Added
- Preserved file line endings (CR+LF vs LF).
- Configuration file support (`~/.config/mde/config` or `~/.mde/config`).
- Emacs-style backup file (`~`) and recovery auto-save file (`#...#`).
- Traditional mouse selection and right/middle-click paste.
- Makefile auto-dependencies (`-MMD -MP`) and configurable `PREFIX`.
- Markdown extensions: footnotes (`[^note]` & `[^note]:`), nested lists with bullet cycling (`•`, `◦`, `▪`), reference links (`[text][ref]` & `[ref]: url`), bare URL autolinking, and table column alignment (`:---`, `:---:`, `---:`).
- View-bound range preview rendering (`render_document_range`) with lookahead margins for large documents.
- Responsive preview soft-wrapping adhering to window width and indentation.
- Single-source keybindings documentation generator (`scripts/generate_keybindings.py`).
- MIT License (`LICENSE.md`).
- Comprehensive Doxygen documentation across all C source and header files.
- Dual-pane mouse click routing in `VIEW_SPLIT` to synchronize editor cursor and single-click follow Markdown links.
- Live document and selection statistics modal dialog (`Alt+D` / `M-D`).
- Bracket and Markdown delimiter pair matching navigation (`Ctrl+]` / `Alt+]`).
- 4th bottom shortcut hint row with 6 additional keybindings.
- Function key toggle (`F10` / `F12` / `Alt+K`) to hide or show bottom shortcut hints to maximize editor screen area.
- Persistent `show_keybindings` configuration option in `~/.config/mde/config`.
- `Ctrl+Home` (`C-Home`) and `Ctrl+End` (`C-End`) shortcuts to navigate directly to the top (line 0, col 0) and bottom (last line, end of text) of the document.
- Synchronized preview cursor indicator (`▶ ` margin pointer and cell highlight) in `VIEW_SPLIT` tracking editor cursor position.
- Split view preview overscroll support allowing navigation beyond the last line of the code pane until the preview pane reaches the bottom.
- Automatic persistence of `show_keybindings` in user configuration when toggled via `F10` / `F12` / `Alt+K`.
- Bi-directional footnote hypertext navigation via `Alt+O` / `F7` (jumping between `[^note]` and `[^note]:`, jumping to first reference if multiple exist, with history back on `F8` / `Alt+-`).
- Ergonomic keybindings for Jump (`Alt+M` / `M-M`, `F11`) and Go to Line (`Alt+G` / `M-G`, `Alt+:`), with updated bottom badges for accessibility across international keyboard layouts.
- Organized sample documents into dedicated `examples/` directory (`examples/demo.md` and `examples/advanced.md`).
- Defaulted application export engine to native built-in C11 converters for all available formats (HTML5, XML/DocBook 5, EPUB 2/3, LaTeX), using pandoc only when explicitly selected by the user.
- Standalone CLI tool `bin/md2epub` (`src/md2epub_main.c`) with support for EPUB 2/3 target formats, chapter splitting, metadata overrides, cover image embedding, and progress reporting.
- Standard UNIX man page for the converter (`man/md2epub.1`).
- Compliant Open Container Format (OCF) packaging with uncompressed STORE `mimetype` at offset 0, automated chapter splitting on H1 (`# `) headings and linked files, YAML front-matter extraction, and local image asset bundling.
- Interactive Document Metadata editor dialog (`Alt+I` / `M-I` and Export Menu `[M]` shortcut) supporting 10 editable metadata fields (Title, Author, Language, Date, Publisher, Identifier, Rights, Description, Cover Image, and Custom CSS) with immediate front-matter synchronization.
- Atomic document front-matter updates (`editor_update_front_matter`) preserving document contents, marking buffers dirty, and participating in undo/redo history.
- Standalone verification script (`scripts/verify_no_pandoc.sh`) validating all export formats (HTML5, XML/DocBook 5, EPUB 2/3, LaTeX) in pandoc-free environments.
- Expanded test suite with comprehensive EPUB 2/3, pandoc-free export, and metadata editing verification in `tests/test_runner.c` (575 / 575 passing tests, 100% pass rate).

## [0.1.0] - 2026-11-21

### Added
- Initial release with dual-pane preview, in-place formatting, and reader mode.
- Line buffer with UTF-8 awareness and multi-level undo/redo.
- Column ruler, automatic line break, and paragraph reflow.
- Hypertext link navigation and outline jump modal.
- Multi-file export to HTML, XML, LaTeX, and EPUB.
