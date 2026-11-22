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

## [0.1.0] - 2026-11-21

### Added
- Initial release with dual-pane preview, in-place formatting, and reader mode.
- Line buffer with UTF-8 awareness and multi-level undo/redo.
- Column ruler, automatic line break, and paragraph reflow.
- Hypertext link navigation and outline jump modal.
- Multi-file export to HTML, XML, LaTeX, and EPUB.
