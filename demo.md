# Welcome to mde: Markdown Text Editor

`mde` is a modern, fast, full-screen Markdown editor running in text mode (TUI) built in **pure C11** with `ncursesw`.

---

## Key Features

- **Dual-Pane Split View**: Live rendered Markdown preview on the right side while editing on the left!
- **In-Place Rich Highlighting**: Single-screen mode with inline colors, styled headings, and dimmed markup.
- **Rendered Reader Mode**: Full-screen formatted viewing with aligned box-drawn tables and directional link indicators.
- **Smart Markdown Editing**:
  - Hitting `Enter` on lists auto-continues the bullet or numbering.
  - Hitting `Enter` on an empty bullet removes it.
  - `Ctrl+T` instantly toggles task checkboxes `[ ]` <-> `[x]`.
  - `Alt+B` inserts bold (`**`) markers or wraps the active selection.
  - `Alt+J` reflows/justifies paragraphs (including `> ` blockquotes) to your configured wrap width.
- **Full Undo / Redo**: `Alt+U` and `Alt+E` with group tracking and exact dirty-state awareness.
- **Search, Replace & Outline**: `Ctrl+W` / `Ctrl+Q` to search, `Ctrl+\` / `Alt+R` for interactive replace, and `F9` / `Alt+T` for the Document Outline jump modal.
- **Hypertext Links**: Try [External Docs](https://commonmark.org/), [Local README](README.md), or jump to [Keyboard Shortcuts](#keyboard-shortcuts) with `F7` / `Alt+O`!

---

## Task Checklist

Try placing the cursor on any task below and pressing `Ctrl+T`:

- [x] Implement line buffer with multi-level undo stack
- [x] Build UTF-8 visual column and navigation engine
- [x] Write streaming Markdown syntax parser and highlighter
- [x] Implement dual-pane split view with synchronized scrolling
- [x] Implement in-place rich editor mode
- [ ] Add your own markdown notes here!

---

## Code Example

```c
#include <stdio.h>

int main(void) {
    printf("Hello from mde in C11!\n");
    return 0;
}
```

> "Simplicity is prerequisite for reliability."
> — Edsger W. Dijkstra

---

## Keyboard Shortcuts

| Shortcut | Action |
| --- | --- |
| `F2` / `Alt+P` | Cycle View Mode (Split / In-place / Preview) |
| `Ctrl+S` | Save current file (atomic & symlink-safe) |
| `Ctrl+O` | Write Out (Save As with overwrite warning) |
| `Ctrl+R` | Read / Open File |
| `Ctrl+X` | Exit editor (prompts if unsaved) |
| `Ctrl+T` | Toggle checkbox `[ ]` <-> `[x]` |
| `Alt+U` / `Alt+E` | Undo / Redo |
| `Ctrl+W` / `Ctrl+Q` | Search Forward / Backward |
| `Ctrl+\` / `Alt+R` | Search & Replace (Y / N / A / Esc) |
| `Alt+J` | Reflow / Justify Paragraph |
| `Ctrl+←` / `Ctrl+→` | Move Backward / Forward by Word |
| `F9` / `Alt+T` | Document Outline (Jump to Heading) |
| `F6` / `F7` / `F8` | Insert Link / Follow Link / Go Back |
| `F5` / `Alt+X` | Export Menu (HTML, XML, EPUB, LaTeX) |
| `Ins` | Toggle Insert / Overwrite mode |
| `F1` / `Ctrl+G` | Show Help Dialog |

Happy Markdown editing!
