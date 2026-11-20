# Welcome to mde: Markdown Text Editor

`mde` is a modern, fast, full-screen Markdown editor running in text mode (TUI) built in **pure C11** with `ncursesw`.

---

## Key Features

- **Dual-Pane Split View**: Live rendered Markdown preview on the right side while editing on the left!
- **In-Place Rich Highlighting**: Single-screen mode with inline colors, styled headings, and dimmed markup.
- **Rendered Reader Mode**: Full-screen formatted viewing.
- **Smart Markdown Editing**:
  - Hitting `Enter` on lists auto-continues the bullet or numbering.
  - Hitting `Enter` on an empty bullet removes it.
  - `Ctrl+T` instantly toggles task checkboxes `[ ]` <-> `[x]`.
  - `Ctrl+B` and `Ctrl+K` insert bold and inline code markers.
- **Full Undo / Redo**: `Ctrl+Z` and `Ctrl+Y` with group tracking.
- **Interactive Find**: `Ctrl+F` with live highlighting and navigation.

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
| `Ctrl+P` or `F2` | Toggle View Mode (Split / In-place / Preview) |
| `Ctrl+S` | Save current file |
| `Ctrl+O` | Open file |
| `Ctrl+Q` | Quit editor |
| `Ctrl+T` | Toggle checkbox `[ ]` <-> `[x]` |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo |
| `Ctrl+F` | Find / Search text |
| `Ins` | Toggle Insert / Overwrite mode |
| `F1` or `Ctrl+H` | Show Help Dialog |

Happy Markdown editing!
