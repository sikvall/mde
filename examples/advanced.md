# Advanced Multi-File Editing with mde

Welcome to the secondary example document in the `examples/` directory!

This file demonstrates **multi-file authoring**, **cross-document link navigation**, and **linked-file export bundling** in `mde`.

---

## 1. Hypertext Link Following & Navigation History

- You reached this document from `examples/demo.md` (or directly via the command line).
- **Return to Previous Document (`F8` / `Alt+-`)**:
  Press **`F8`** or **`Alt+-`** to pop the navigation history stack and return directly to `demo.md` with your exact previous cursor line and column restored!
- **Follow Link Directly (`F7` / `Alt+O`)**:
  You can also place the cursor on this link and press **`F7`** / **`Alt+O`** to open: [Return to Demo](demo.md).
- **Navigate to Project Root**:
  - [Open Project README](../README.md)
  - [View Software License](../LICENSE.md)

---

## 2. Multi-File Export Bundling (`F5` / `Alt+X`)

When exporting from `demo.md` to **HTML5**, **XML**, **EPUB**, **LaTeX**, or **Man (`.1`)**:
1. Press **`F5`** or **`Alt+X`** to open the Export Format Dialog (`1`–`5` selects format; `Tab` customizes the path).
2. Press **`L`** inside the dialog to toggle linked-file bundling (`M` edits document metadata, `E` toggles Built-in vs. Pandoc when installed).
3. When linked-file bundling is enabled, `mde` recursively traverses all local links (`advanced.md`, etc.) and bundles them into a coherent export:
   - **HTML5 & EPUB**: Converts local `.md` links into working internal chapter anchor links.
   - **LaTeX**: Combines all linked documents into a single `.tex` document with `\hypertarget` and `\hyperlink`.
   - **Man (`.1`)**: Combines all linked documents into a unified UNIX/Linux `man(7)` roff manual with `.SH` sections and cross-references (preview with `man -l output.1`).
   - **XML**: Structures all linked files into `<linked-document>` sections.

---

## 3. Footnote Bi-Directional Jumping

Footnotes also work seamlessly within any document[^adv-note]:
- Put your cursor on `[^adv-note]` and press **`F7`** or **`Alt+O`** to jump to the definition below.
- From the definition, press **`F7`** or **`Alt+O`** to jump right back to this reference!

---

## Footnotes

[^adv-note]: Footnotes in mde support arbitrary alphanumeric labels and bi-directional jumping via Alt+O / F7.
