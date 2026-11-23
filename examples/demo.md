# Welcome to mde: Modern Text-Mode Markdown Editor

`mde` is a fast, full-screen Markdown editor running in your terminal, built in **pure C11** with `ncursesw`[^c11-note]. It offers distraction-free writing, dual-pane live preview, rich syntax highlighting, and hypertext navigation.

---

## 1. Dual-Pane Live Preview & Mouse Interaction

Press **`F2`** or **`Alt+P`** to cycle between the three view modes:
1. **Dual-Pane Split View**: Real-time rendered preview on the right while editing on the left.
2. **In-Place Rich Mode**: Single pane with syntax colors, dimmed markdown tokens, and soft wrapping.
3. **Rendered Reader Mode**: Full-screen formatted document viewer with aligned box-drawn tables.

### Interactive Mouse Features
- **Preview Click Routing**: In split view, click anywhere in the right preview pane to immediately synchronize the editor cursor to that exact source line.
- **Single-Click Link Navigation**: Click directly on any hyperlink in the preview pane to follow it instantly!
- **Mouse Selection & Paste**: Click and drag to select text (commits to clipboard and ANSI OSC 52). Right-click or middle-click to paste text at cursor.

---

## 2. Hypertext Links, References & Bare URLs

`mde` includes a versatile link engine supporting five distinct linking styles:

### A. Inline External Hyperlinks
- Standard web links display with an external indicator (`↗`): [CommonMark Official Specification](https://spec.commonmark.org/).
- Press **`F7`** or **`Alt+O`** with the cursor on the link (or single-click in the preview pane) to open it.

### B. Local File Links
- Local document links display with a local pointer (`→`): [Advanced Multi-File Editing](advanced.md), [Open Project README](../README.md), or [View Software License](../LICENSE.md).
- Following a local file link switches to that file (prompting to save unsaved edits if dirty). If the file does not yet exist, a fresh buffer is initialized for it!
- Press **`F8`** or **`Alt+-`** at any time to navigate back along your link history.
- Press **`F6`** or **`Alt+H`** to open the file picker and insert a relative link to any file.

### C. Same-Document Anchor Links
- Internal section links display with a downward jump indicator (`↓`): [Jump to Keyboard Shortcuts](#keyboard-shortcuts-reference) or [Jump to Footnotes](#footnotes).
- Anchors automatically match Markdown heading slugs (e.g. `#1-dual-pane-live-preview`).

### D. Reference-Style Links
- Link text and URLs can be separated cleanly using reference labels:
  - Read more about [Markdown Best Practices][guide-ref] or visit the [C11 Standard Overview][c11-ref].
  - Reference definitions are placed at the bottom of the document (see definitions below).

### E. Bare Autolink URLs
- Raw URLs without Markdown formatting brackets are automatically recognized, styled, and clickable:
  - Repository: https://github.com/sikvall/mde
  - Documentation: http://spec.commonmark.org
  - Secure portal: https://www.kernel.org

---

## 3. Bracket & Delimiter Pair Matching (`Ctrl+]` / `Alt+]`)

Place your cursor on any delimiter below and press **`Ctrl+]`** (or **`Alt+]`**) to jump back and forth to its counterpart:

- **Standard Brackets**:
  - Parentheses: (Try jumping between the opening and closing parenthesis)
  - Square brackets: [Cursor will jump across this label]
  - Curly braces: { Multi-line braces also
                    jump smoothly across rows }
  - Angle brackets: <https://www.gnu.org/>
- **Markdown Delimiters**:
  - Bold: **jump between the opening and closing double asterisks**
  - Strikethrough: ~~jump between the opening and closing tildes~~
  - Inline Code: `jump between the opening and closing backticks`
  - Fenced Code Blocks: jump between opening and closing code fences (see below)

---

## 4. Multi-Level Nested Lists & Bullet Cycling

Indented list items automatically cycle bullet glyphs and format indentation margins:

- Top-level item (bullet: `•`)
  - Second-level item indented by 2 spaces (bullet: `◦`)
    - Third-level nested item indented by 4 spaces (bullet: `▪`)
    - Another third-level detail
  - Back to second level
- Top-level continuation

---

## 5. Task Checklist (`Ctrl+T`)

Place your cursor on any task item and press **`Ctrl+T`** to toggle state:

- [x] Implement line buffer with multi-level atomic undo stack
- [x] Build UTF-8 visual column math and multi-byte navigation engine
- [x] Add dual-pane split view with synchronized scrolling
- [x] Add live document statistics modal (`Alt+D`)
- [x] Add 4th shortcut hint row and bottom bar toggle (`F10`)
- [ ] Write your next Markdown document with `mde`!

---

## 6. Table Column Alignment & Box Drawing

`mde` parses column alignments (`:---` Left, `:---:` Center, `---:` Right) and renders clean Unicode box borders (`┌─┬─┐`, `├─┼─┤`, `└─┴─┘`):

| Feature | Alignment Mode | Shortcut Key | Status |
| :--- | :---: | :---: | ---: |
| Dual-Pane Split | Center | `F2` / `Alt+P` | Ready |
| Document Stats | Center | `Alt+D` | Active |
| Jump Pair Match | Right | `Ctrl+]` | Working |
| Outline Navigator | Left | `F9` / `Alt+T` | Built-in |
| Toggle Bottom Bar | Center | `F10` / `F12` | Enabled |

---

## 7. Document & Selection Statistics (`Alt+D` / `M-D`)

Press **`Alt+D`** to open the live statistics modal dialog:
- View total lines, word count, total character count, and non-whitespace character count.
- See estimated reading time calculated at 200 words per minute[^reading-note].
- Select text with **`Alt+A`** (or mouse drag) to see real-time statistics for the active selection.

---

## 8. Bottom Keybindings Bar & F-Key Toggle (`F10` / `F12`)

The bottom bar displays 4 rows of 24 handy shortcut badges.
- Press **`F10`** (or **`F12`** / **`Alt+K`**) to hide the bottom bar and maximize your editing canvas by 4 full lines!
- Press **`F10`** again to restore the keybinding hints.
- Your preference is automatically saved to `~/.config/mde/config`.

---

## 9. Code Example

```c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>

int main(void) {
    printf("Welcome to mde — pure C11 Markdown editor!\n");
    return 0;
}
```

> "Simplicity is prerequisite for reliability."
> — Edsger W. Dijkstra

---

## Keyboard Shortcuts Reference

| Shortcut | Action Description |
| :--- | :--- |
| **`F1` / `Ctrl+G`** | Open two-tab interactive Help & Reference overlay |
| **`F2` / `Alt+P`** | Cycle View Modes (**Split** → **In-Place** → **Preview**) |
| **`F3` / `Alt+W`** | Repeat search forward (`Alt+Q` repeats backward) |
| **`F4` / `Alt+L`** | Set automatic line break column width (`20`–`255` or `off`) |
| **`F5` / `Alt+X`** | Open Export Menu (**HTML5**, **XML**, **EPUB**, **LaTeX**, **Man**) |
| **`F6` / `Alt+H`** | Insert relative hypertext link via modal file picker |
| **`F7` / `Alt+O`** | Follow link or `#heading` anchor under cursor |
| **`F8` / `Alt+-`** | Navigate back to previously visited linked file |
| **`F9` / `Alt+T`** | Open Document Outline modal to jump to any heading |
| **`F10` / `F12`** | Toggle bottom shortcut hint rows (hide / show) |
| **`Alt+D`** | Live Document & Selection Statistics modal dialog |
| **`Ctrl+]` / `Alt+]`** | Jump to matching bracket `()`, `[]`, `{}`, `<>`, or delimiter pair |
| **`Ctrl+S`** | Direct atomic save to disk (preserves permissions & symlinks) |
| **`Ctrl+O`** | Write Out (Save As dialog with overwrite guard) |
| **`Ctrl+R`** | Read / Open file dialog |
| **`Ctrl+X`** | Exit editor (prompts to save unsaved modifications) |
| **`Ctrl+W` / `Ctrl+Q`** | Where Is (search forward) / Search backward prompt |
| **`Ctrl+\` / `Alt+R`** | Interactive Search & Replace (**Y**es / **N**o / **A**ll / Cancel) |
| **`Ctrl+T`** | Toggle task checkbox `[ ]` <-> `[x]` |
| **`Alt+U` / `Alt+E`** | Multi-level Undo / Redo with exact dirty tracking |
| **`Alt+J`** | Reflow current paragraph or blockquote to wrap margin |
| **`Alt+B`** | Insert Bold markers (`**...**`) or wrap active selection |
| **`Ins`** | Toggle Insert mode (`█` steady block) / Overwrite mode (`_`) |

---

## Footnotes

[^c11-note]: Built strictly according to ISO/IEC 9899:2011 (C11) standard with zero external runtime dependencies beyond ncursesw.
[^reading-note]: Reading speed is estimated based on standard adult silent reading rate of 200 words per minute.

---

<!-- Reference Link Definitions -->
[guide-ref]: https://www.markdownguide.org/
[c11-ref]: https://en.wikipedia.org/wiki/C11_(C_standard_revision)
