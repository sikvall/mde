/**
 * @file theme.h
 * @brief Terminal color palette, styling, and visual attributes.
 *
 * Configures 256-color and 16-color ncurses color pairs for all syntax
 * tokens (headers, lists, bold, italic, code blocks, tables, checkboxes),
 * UI chrome (gutter, ruler, status bar, divider, dialogs), and active states.
 */

#ifndef MDE_THEME_H
#define MDE_THEME_H

#include "mde_curses.h"
#include "markdown.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Color pair identifier enumeration.
 */
enum ColorPairId {
    CP_DEFAULT = 1,
    CP_HEADER1,
    CP_HEADER2,
    CP_HEADER3,
    CP_HEADER4,
    CP_HEADER5,
    CP_HEADER6,
    CP_MARKER,            /**< Inactive/dim syntax markers (#, **, etc.) */
    CP_MARKER_ACTIVE,     /**< Highlighted syntax markers on active line */
    CP_BOLD,
    CP_ITALIC,
    CP_BOLD_ITALIC,
    CP_STRIKE,
    CP_CODE,
    CP_CODE_BLOCK,
    CP_BLOCKQUOTE,
    CP_LINK,
    CP_URL,
    CP_TASK_UNCHECKED,
    CP_TASK_CHECKED,
    CP_HRULE,
    CP_LINE_NUM,
    CP_LINE_NUM_CUR,
    CP_GUTTER_MARKER,     /**< Active line pointer in gutter (▶) */
    CP_RULER,             /**< Column ruler base ticks and numbers */
    CP_RULER_CURSOR,      /**< Column ruler cursor indicator (▼) */
    CP_RULER_LIMIT,       /**< Active auto line break guide tick (┆) */
    CP_STATUS_BAR,
    CP_STATUS_BAR_ACCENT,
    CP_STATUS_BAR_DIRTY,
    CP_SEARCH_MATCH,
    CP_SEARCH_CUR,
    CP_DIVIDER,
    CP_HELP_BOX,
    CP_HELP_KEY,
    CP_DIALOG_BOX
};

/**
 * @brief Initialize all color pairs and attributes in ncurses.
 *
 * Detects terminal color depth (256-color palette vs. 16/8-color fallback)
 * and sets up appropriate foreground and background pairs.
 */
void theme_init(void);

/**
 * @brief Retrieve ncurses video attribute and color pair for a markdown token.
 *
 * @param type The markdown token type (header, bold, code, marker, etc.).
 * @param is_active_line True if the token is on the current cursor line.
 * @return Combined ncurses attr_t value.
 */
attr_t theme_token_attr(MdTokenType type, bool is_active_line);

#ifdef __cplusplus
}
#endif

#endif // MDE_THEME_H
