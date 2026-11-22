#!/usr/bin/env python3
"""
Single-source keybinding table generator for mde.
Generates documentation tables for README.md, man/mde.1, and src/main.c
"""

KEYBINDINGS = [
    ("Ctrl+G / F1", "Get Help overlay (Cheat sheet & Markdown guide)"),
    ("Ctrl+X", "Exit editor (prompts if unsaved changes)"),
    ("Ctrl+S", "Save current file directly to disk"),
    ("Ctrl+O", "Write Out (Save As file dialogue)"),
    ("Ctrl+R", "Read / Open file dialogue to browse and open a file"),
    ("F5 / Alt+X", "Open Export Menu (HTML, XML, EPUB, LaTeX)"),
    ("F6 / Alt+H", "Insert hypertext file link [Label](file.md)"),
    ("F7 / Alt+O", "Follow Markdown link or jump between footnote & definition"),
    ("F8 / Alt+-", "Navigate back in link & footnote history"),
    ("F9 / Alt+T", "Open Document Outline modal (jump to H1-H6 heading)"),
    ("Alt+D", "Live document and selection statistics modal"),
    ("M-M / F11 / ^]", "Jump to matching bracket (), [], {}, <>, or delimiter pair"),
    ("F10 / F12", "Toggle bottom shortcut hint rows (hide / show)"),
    ("F2 / Alt+P", "Cycle View Modes (Split -> In-Place -> Preview)"),
    ("F4 / Alt+L", "Set automatic line break column (20-255 or off)"),
    ("Ctrl+L", "Refresh / redraw screen"),
    ("Ctrl+W", "Where Is - Search forward for text"),
    ("Ctrl+Q", "Where Was - Search backward for text"),
    ("Ctrl+\\ / Alt+R", "Search and Replace (Yes / No / All)"),
    ("Alt+W / F3", "Repeat search forward (Alt+Q repeats backward)"),
    ("M-G / Alt+G / ^_", "Go to line number (and optional column)"),
    ("Ctrl+C", "Display current cursor location (Cancel in prompts)"),
    ("Alt+A / Ctrl+6", "Set or unset selection mark anchor (also Ctrl+Space)"),
    ("Ctrl+K", "Cut active selection (or current line) to clipboard"),
    ("Alt+6", "Copy active selection (or current line) to clipboard"),
    ("Ctrl+U", "Paste (Uncut) text from clipboard"),
    ("Esc", "Clear active selection mark (Cancel in prompts)"),
    ("Alt+U", "Undo last change"),
    ("Alt+E", "Redo last undone change"),
    ("Alt+J", "Reflow (re-wrap) current paragraph to wrap column"),
    ("Ctrl+T", "Toggle checkbox [- [ ] <-> - [x]] on current line"),
    ("Alt+B", "Insert Bold markers (**) or wrap selection"),
    ("Ctrl+A / Home", "Jump to beginning of current line"),
    ("Ctrl+E / End", "Jump to end of current line"),
    ("Ctrl+Left/Right", "Jump backward / forward by one word"),
    ("Ctrl+Y / PgUp", "Previous Page (scroll viewport up)"),
    ("Ctrl+V / PgDn", "Next Page (scroll viewport down)"),
    ("Ctrl+Home / End", "Jump to first / last line of document (also Alt+\\ / Alt+/)"),
    ("Ctrl+B/F/P/N", "Move cursor Left / Right / Up / Down (or Arrow keys)"),
    ("Ctrl+H / Bksp", "Delete character before cursor"),
    ("Ctrl+D / Del", "Delete character under cursor"),
    ("Enter", "Smart list continuation & auto-indent"),
    ("Tab / Alt+}", "Indent line by 2 spaces"),
    ("Shift+Tab / Alt+{", "Dedent line by 2 spaces"),
    ("Ins", "Toggle Insert / Overwrite mode"),
]

def generate_markdown_table():
    lines = ["| Shortcut | Description |", "|---|---|"]
    for k, d in KEYBINDINGS:
        lines.append(f"| **`{k}`** | {d} |")
    return "\n".join(lines)

if __name__ == "__main__":
    print(f"Loaded {len(KEYBINDINGS)} keybindings.")
