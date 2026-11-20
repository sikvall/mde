/**
 * @file theme.c
 * @brief Implementation of color themes, syntax highlighting palettes, and visual styles.
 */

#define _XOPEN_SOURCE_EXTENDED 1
#include "theme.h"

void theme_init(void) {
    if (!has_colors()) return;

    start_color();
    use_default_colors();

    if (COLORS >= 256) {
        // Modern 256-color palette
        init_pair(CP_DEFAULT,           -1,  -1);
        init_pair(CP_HEADER1,           81,  -1); // Bright Cyan
        init_pair(CP_HEADER2,          221,  -1); // Warm Gold/Yellow
        init_pair(CP_HEADER3,          114,  -1); // Soft Green
        init_pair(CP_HEADER4,          211,  -1); // Salmon Pink
        init_pair(CP_HEADER5,          141,  -1); // Purple
        init_pair(CP_HEADER6,          180,  -1); // Tan
        init_pair(CP_MARKER,           244,  -1); // Muted Dim Gray for inactive markers
        init_pair(CP_MARKER_ACTIVE,     81,  -1); // Bright Cyan/Accent for active line markers
        init_pair(CP_BOLD,             255,  -1); // Pure White
        init_pair(CP_ITALIC,           223,  -1); // Light Wheat
        init_pair(CP_BOLD_ITALIC,      229,  -1); // Light Gold
        init_pair(CP_STRIKE,           242,  -1); // Strikethrough dim
        init_pair(CP_CODE,             115, 236); // Green on dark gray
        init_pair(CP_CODE_BLOCK,       250, 235); // Gray on very dark gray
        init_pair(CP_BLOCKQUOTE,       247,  -1); // Light gray
        init_pair(CP_LINK,              75,  -1); // Sky blue
        init_pair(CP_URL,              242,  -1); // Dim URL
        init_pair(CP_TASK_UNCHECKED,   214,  -1); // Orange/Yellow [ ]
        init_pair(CP_TASK_CHECKED,      48,  -1); // Bright Spring Green [x]
        init_pair(CP_HRULE,             67,  -1); // Steel Blue
        init_pair(CP_LINE_NUM,         240,  -1); // Dark Gray
        init_pair(CP_LINE_NUM_CUR,     220,  -1); // Bright Yellow for active line
        init_pair(CP_GUTTER_MARKER,    220,  -1); // Bright Yellow pointer (▶)
        init_pair(CP_RULER,            241, 234); // Ruler numbers and ticks
        init_pair(CP_RULER_CURSOR,     220, 236); // Column cursor marker (▼)
        init_pair(CP_RULER_LIMIT,      203, 234); // Auto line break column guide tick
        init_pair(CP_STATUS_BAR,       254, 237); // White on Slate
        init_pair(CP_STATUS_BAR_ACCENT, 81, 237); // Cyan on Slate
        init_pair(CP_STATUS_BAR_DIRTY, 203, 237); // Coral Red on Slate
        init_pair(CP_SEARCH_MATCH,      16, 221); // Black on Gold
        init_pair(CP_SEARCH_CUR,        16,  48); // Black on Spring Green
        init_pair(CP_DIVIDER,          239,  -1); // Subtle divider gray
        init_pair(CP_HELP_BOX,         253, 236); // Help popup
        init_pair(CP_HELP_KEY,          81, 236); // Cyan keys
        init_pair(CP_DIALOG_BOX,       255, 238); // Dialog
    } else {
        // Standard 8/16 color fallback
        init_pair(CP_DEFAULT,           -1,            -1);
        init_pair(CP_HEADER1,           COLOR_CYAN,    -1);
        init_pair(CP_HEADER2,           COLOR_YELLOW,  -1);
        init_pair(CP_HEADER3,           COLOR_GREEN,   -1);
        init_pair(CP_HEADER4,           COLOR_MAGENTA, -1);
        init_pair(CP_HEADER5,           COLOR_BLUE,    -1);
        init_pair(CP_HEADER6,           COLOR_WHITE,   -1);
        init_pair(CP_MARKER,            COLOR_BLACK,   -1);
        init_pair(CP_MARKER_ACTIVE,     COLOR_CYAN,    -1);
        init_pair(CP_BOLD,              COLOR_WHITE,   -1);
        init_pair(CP_ITALIC,            COLOR_YELLOW,  -1);
        init_pair(CP_BOLD_ITALIC,       COLOR_YELLOW,  -1);
        init_pair(CP_STRIKE,            COLOR_WHITE,   -1);
        init_pair(CP_CODE,              COLOR_GREEN,   -1);
        init_pair(CP_CODE_BLOCK,        COLOR_WHITE,   -1);
        init_pair(CP_BLOCKQUOTE,        COLOR_CYAN,    -1);
        init_pair(CP_LINK,              COLOR_BLUE,    -1);
        init_pair(CP_URL,               COLOR_BLACK,   -1);
        init_pair(CP_TASK_UNCHECKED,    COLOR_YELLOW,  -1);
        init_pair(CP_TASK_CHECKED,      COLOR_GREEN,   -1);
        init_pair(CP_HRULE,             COLOR_CYAN,    -1);
        init_pair(CP_LINE_NUM,          COLOR_WHITE,   -1);
        init_pair(CP_LINE_NUM_CUR,      COLOR_YELLOW,  -1);
        init_pair(CP_GUTTER_MARKER,     COLOR_YELLOW,  -1);
        init_pair(CP_RULER,             COLOR_WHITE,   -1);
        init_pair(CP_RULER_CURSOR,      COLOR_YELLOW,  -1);
        init_pair(CP_RULER_LIMIT,       COLOR_RED,     -1);
        init_pair(CP_STATUS_BAR,        COLOR_BLACK,   COLOR_WHITE);
        init_pair(CP_STATUS_BAR_ACCENT, COLOR_BLUE,    COLOR_WHITE);
        init_pair(CP_STATUS_BAR_DIRTY,  COLOR_RED,     COLOR_WHITE);
        init_pair(CP_SEARCH_MATCH,      COLOR_BLACK,   COLOR_YELLOW);
        init_pair(CP_SEARCH_CUR,        COLOR_BLACK,   COLOR_GREEN);
        init_pair(CP_DIVIDER,           COLOR_WHITE,   -1);
        init_pair(CP_HELP_BOX,          COLOR_WHITE,   COLOR_BLUE);
        init_pair(CP_HELP_KEY,          COLOR_YELLOW,  COLOR_BLUE);
        init_pair(CP_DIALOG_BOX,        COLOR_WHITE,   COLOR_BLUE);
    }
}

attr_t theme_token_attr(MdTokenType type, bool is_active_line) {
    // When on the active line, show markers prominently rather than dimmed
    attr_t marker_attr = is_active_line
        ? (COLOR_PAIR(CP_MARKER_ACTIVE) | A_BOLD)
        : (COLOR_PAIR(CP_MARKER) | A_DIM);

    switch (type) {
        case MD_HEADER_MARKER_1:
        case MD_HEADER_MARKER_2:
        case MD_HEADER_MARKER_3:
        case MD_HEADER_MARKER_4:
        case MD_HEADER_MARKER_5:
        case MD_HEADER_MARKER_6:
            return marker_attr;

        case MD_HEADER_TEXT_1:
            return COLOR_PAIR(CP_HEADER1) | A_BOLD;
        case MD_HEADER_TEXT_2:
            return COLOR_PAIR(CP_HEADER2) | A_BOLD;
        case MD_HEADER_TEXT_3:
            return COLOR_PAIR(CP_HEADER3) | A_BOLD;
        case MD_HEADER_TEXT_4:
            return COLOR_PAIR(CP_HEADER4) | A_BOLD;
        case MD_HEADER_TEXT_5:
            return COLOR_PAIR(CP_HEADER5) | A_BOLD;
        case MD_HEADER_TEXT_6:
            return COLOR_PAIR(CP_HEADER6) | A_BOLD;

        case MD_BLOCKQUOTE_MARKER:
            return is_active_line
                ? (COLOR_PAIR(CP_HEADER1) | A_BOLD)
                : (COLOR_PAIR(CP_MARKER) | A_DIM);
        case MD_BLOCKQUOTE_TEXT:
            return COLOR_PAIR(CP_BLOCKQUOTE);

        case MD_LIST_BULLET:
            return is_active_line
                ? (COLOR_PAIR(CP_HEADER2) | A_BOLD)
                : (COLOR_PAIR(CP_HEADER2));

        case MD_TASK_UNCHECKED:
            return COLOR_PAIR(CP_TASK_UNCHECKED) | A_BOLD;
        case MD_TASK_CHECKED:
            return COLOR_PAIR(CP_TASK_CHECKED) | A_BOLD;

        case MD_CODE_FENCE:
            return marker_attr;
        case MD_CODE_FENCE_LANG:
            return COLOR_PAIR(CP_HEADER3) | A_BOLD;
        case MD_CODE_BLOCK_LINE:
            return COLOR_PAIR(CP_CODE_BLOCK);

        case MD_HRULE:
            return COLOR_PAIR(CP_HRULE) | A_BOLD;

        case MD_TABLE_BORDER:
            return COLOR_PAIR(CP_HRULE);
        case MD_TABLE_HEADER:
            return COLOR_PAIR(CP_BOLD) | A_BOLD;
        case MD_TABLE_CELL:
            return COLOR_PAIR(CP_DEFAULT);

        case MD_BOLD_MARKER:
            return marker_attr;
        case MD_BOLD_TEXT:
            return COLOR_PAIR(CP_BOLD) | A_BOLD;

        case MD_ITALIC_MARKER:
            return marker_attr;
        case MD_ITALIC_TEXT:
            return COLOR_PAIR(CP_ITALIC) | A_UNDERLINE;

        case MD_BOLD_ITALIC_MARKER:
            return marker_attr;
        case MD_BOLD_ITALIC_TEXT:
            return COLOR_PAIR(CP_BOLD_ITALIC) | A_BOLD | A_UNDERLINE;

        case MD_STRIKE_MARKER:
            return marker_attr;
        case MD_STRIKE_TEXT:
            return COLOR_PAIR(CP_STRIKE) | A_DIM;

        case MD_INLINE_CODE_MARKER:
            return marker_attr;
        case MD_INLINE_CODE_TEXT:
            return COLOR_PAIR(CP_CODE);

        case MD_LINK_BRACKET:
        case MD_LINK_URL_PAREN:
            return marker_attr;
        case MD_LINK_TEXT:
            return COLOR_PAIR(CP_LINK) | A_UNDERLINE;
        case MD_LINK_URL:
            return COLOR_PAIR(CP_URL) | A_DIM;

        case MD_IMAGE_MARKER:
            return COLOR_PAIR(CP_HEADER4) | A_BOLD;

        case MD_TEXT:
        default:
            return COLOR_PAIR(CP_DEFAULT);
    }
}
