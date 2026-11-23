# Changelog

## 0.1.3 - 2026-11-24
- Added a cross-platform prerequisite installer for major Linux distributions and macOS.
- Upgraded the build system to GNU-style `./configure` and `make` with standard installation paths and out-of-tree build support.
- Added multi-distribution Linux packaging support for Debian, Ubuntu, RHEL/Fedora/openSUSE, Arch, Alpine, Void, Gentoo, Nix, Snap, and portable release tarballs.
- Enabled strict C11 compile-time warnings across the codebase.
- Hardened string, buffer, UTF-8, path, and exporter bounds handling to prevent potential buffer overruns and over-reads.

## 0.1.2 - 2026-11-22
- Added a native EPUB 2/3 e-book publishing engine with chapter splitting, multi-file book bundling, local image embedding, and table of contents generation without requiring Pandoc.
- Added the standalone `md2epub` command-line converter, manual page, and multi-chapter book showcase.
- Added an interactive Document Metadata editor with cover image selection and YAML front-matter synchronization.
- Added three-mode line wrapping (Soft by default, Hard, and Off) plus context-sensitive paragraph reflow and unflow.
- Defaulted HTML, XML, LaTeX, and EPUB exports to the built-in C11 converters while keeping Pandoc as an optional backend.

## 0.1.1 - 2026-11-22
- Added extended Markdown support including footnotes with bidirectional navigation, nested lists, reference links, autolinks, and table column alignment.
- Added document statistics, bracket/delimiter matching, dual-pane mouse navigation, and a synchronized preview cursor indicator.
- Added persistent configuration, a toggleable shortcut bar, CRLF line-ending preservation, and Emacs-style backup and recovery files.
- Improved performance and readability on large files with viewport-scoped rendering and responsive preview soft-wrapping.
- Added MIT licensing and project documentation.

## 0.1.0 - 2026-11-21
- Initial release with dual-pane split preview, in-place rich syntax styling, and fullscreen reader mode.
- Added a UTF-8 text buffer with multi-level undo/redo and nano-compatible controls.
- Added the column ruler, outline navigator, hypertext file linking, and multi-file export to HTML, XML, LaTeX, and EPUB.
