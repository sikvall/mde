# Changelog

All notable changes to mde (Markdown Editor) will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

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

## [0.1.0] - 2026-11-21

### Added
- Initial release with dual-pane preview, in-place formatting, and reader mode.
- Line buffer with UTF-8 awareness and multi-level undo/redo.
- Column ruler, automatic line break, and paragraph reflow.
- Hypertext link navigation and outline jump modal.
- Multi-file export to HTML, XML, LaTeX, and EPUB.
