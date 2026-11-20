/**
 * @file markdown.h
 * @brief Markdown token definitions, span representation, and lexer API.
 *
 * Provides a high-speed streaming lexer that segments each Markdown line
 * into token spans covering headers, lists, code fences, blockquotes,
 * task checkboxes, horizontal rules, bold, italic, code, and links.
 */

#ifndef MDE_MARKDOWN_H
#define MDE_MARKDOWN_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Markdown syntactic token types.
 */
typedef enum {
    MD_TEXT = 0,           /**< Default plain paragraph text */

    // Headers (H1 through H6)
    MD_HEADER_MARKER_1,    /**< Leading '#' characters for H1 */
    MD_HEADER_TEXT_1,      /**< Header title text for H1 */
    MD_HEADER_MARKER_2,    /**< Leading '##' for H2 */
    MD_HEADER_TEXT_2,      /**< Header title text for H2 */
    MD_HEADER_MARKER_3,    /**< Leading '###' for H3 */
    MD_HEADER_TEXT_3,      /**< Header title text for H3 */
    MD_HEADER_MARKER_4,    /**< Leading '####' for H4 */
    MD_HEADER_TEXT_4,      /**< Header title text for H4 */
    MD_HEADER_MARKER_5,    /**< Leading '#####' for H5 */
    MD_HEADER_TEXT_5,      /**< Header title text for H5 */
    MD_HEADER_MARKER_6,    /**< Leading '######' for H6 */
    MD_HEADER_TEXT_6,      /**< Header title text for H6 */

    // Block-level structures
    MD_BLOCKQUOTE_MARKER,  /**< Blockquote prefix '>' */
    MD_BLOCKQUOTE_TEXT,    /**< Quoted text */
    MD_LIST_BULLET,        /**< Unordered (-, *, +) or ordered (1.) bullet */
    MD_TASK_UNCHECKED,     /**< Checkbox '[ ]' */
    MD_TASK_CHECKED,       /**< Checkbox '[x]' or '[X]' */
    MD_CODE_FENCE,         /**< Code block fence (``` or ~~~) */
    MD_CODE_FENCE_LANG,    /**< Language identifier following opening fence */
    MD_CODE_BLOCK_LINE,    /**< Code line inside a fenced block */
    MD_HRULE,              /**< Horizontal rule (---, ***, ___) */
    MD_TABLE_BORDER,       /**< Table vertical pipe '|' */
    MD_TABLE_HEADER,       /**< Table header text */
    MD_TABLE_CELL,         /**< Table cell text */

    // Inline formatting
    MD_BOLD_MARKER,        /**< Bold delimiter '**' or '__' */
    MD_BOLD_TEXT,          /**< Strong emphasized text */
    MD_ITALIC_MARKER,      /**< Italic delimiter '*' or '_' */
    MD_ITALIC_TEXT,        /**< Emphasized text */
    MD_BOLD_ITALIC_MARKER, /**< Combined delimiter '***' */
    MD_BOLD_ITALIC_TEXT,   /**< Strong and emphasized text */
    MD_STRIKE_MARKER,      /**< Strikethrough delimiter '~~' */
    MD_STRIKE_TEXT,        /**< Struck-through text */
    MD_INLINE_CODE_MARKER, /**< Inline code backtick '`' */
    MD_INLINE_CODE_TEXT,   /**< Monospaced code text */
    MD_LINK_BRACKET,       /**< Link anchor brackets '[' and ']' */
    MD_LINK_TEXT,          /**< Link anchor text */
    MD_LINK_URL_PAREN,     /**< Link URL parentheses '(' and ')' */
    MD_LINK_URL,           /**< Destination URL */
    MD_IMAGE_MARKER        /**< Leading '!' for images */
} MdTokenType;

/**
 * @brief A contiguous byte slice within a line tagged with a token type.
 */
typedef struct {
    size_t start;          /**< 0-indexed byte offset start */
    size_t end;            /**< 0-indexed byte offset end (exclusive) */
    MdTokenType type;      /**< Token type */
} MdSpan;

/**
 * @brief Parsed metadata and token spans for a single line.
 */
typedef struct {
    MdSpan *spans;             /**< Dynamic array of token spans */
    size_t span_count;         /**< Number of spans */
    size_t span_cap;           /**< Allocated span capacity */

    bool is_code_block;        /**< Line is part of a fenced code block */
    bool is_hrule;             /**< Line is a horizontal rule */
    int header_level;          /**< 1-6 if header, 0 otherwise */
    bool is_list_item;         /**< Line is an ordered/unordered list item */
    bool is_task;              /**< Line contains a task checkbox */
    bool task_checked;         /**< true if task is [x], false if [ ] */
    size_t task_bracket_start; /**< Byte offset of '[' in task box */
    bool is_blockquote;        /**< Line is a blockquote */
    bool is_table_row;         /**< Line is a table row */
    char code_lang[32];        /**< Active code block language tag */
} MdParsedLine;

/**
 * @brief Initialize an empty MdParsedLine structure.
 * @param pl Pointer to MdParsedLine.
 */
void md_parsed_line_init(MdParsedLine *pl);

/**
 * @brief Free allocated span memory in an MdParsedLine structure.
 * @param pl Pointer to MdParsedLine.
 */
void md_parsed_line_free(MdParsedLine *pl);

/**
 * @brief Append a token span to a parsed line.
 *
 * @param pl Pointer to MdParsedLine.
 * @param start Start byte offset.
 * @param end End byte offset.
 * @param type Token type.
 * @return true on success, false on allocation failure.
 */
bool md_parsed_line_add_span(MdParsedLine *pl, size_t start, size_t end, MdTokenType type);

/**
 * @brief Parse a single Markdown line given ongoing multi-line document state.
 *
 * @param line Pointer to line character buffer.
 * @param len Length in bytes.
 * @param in_code_block In/out flag indicating whether the parser is inside a fenced block.
 * @param code_lang In/out buffer holding current code block language tag.
 * @param lang_cap Capacity of code_lang buffer.
 * @param out_line Destination MdParsedLine structure.
 */
void md_parse_line(const char *line, size_t len,
                   bool *in_code_block, char *code_lang, size_t lang_cap,
                   MdParsedLine *out_line);

/**
 * @brief Detect whether a line starts with a list bullet or task box.
 *
 * @param line Pointer to line string.
 * @param len Length in bytes.
 * @param out_prefix_len Output byte length of the bullet/task prefix.
 * @param out_bullet Output character buffer receiving bullet string.
 * @param bullet_cap Capacity of out_bullet.
 * @param out_is_task Output boolean indicating task box presence.
 * @param out_task_checked Output boolean indicating checked state.
 * @return true if line begins with a list item or task.
 */
bool md_detect_list(const char *line, size_t len,
                    size_t *out_prefix_len,
                    char *out_bullet, size_t bullet_cap,
                    bool *out_is_task, bool *out_task_checked);

/**
 * @brief Test whether a line matches a Markdown horizontal rule (---, ***, ___).
 * @param line Line string.
 * @param len Length in bytes.
 * @return true if line is a horizontal rule.
 */
bool md_is_hrule(const char *line, size_t len);

/**
 * @brief Check if a token type represents a header marker (#).
 */
static inline bool md_is_header_marker(MdTokenType type) {
    return type == MD_HEADER_MARKER_1 ||
           type == MD_HEADER_MARKER_2 ||
           type == MD_HEADER_MARKER_3 ||
           type == MD_HEADER_MARKER_4 ||
           type == MD_HEADER_MARKER_5 ||
           type == MD_HEADER_MARKER_6;
}

/**
 * @brief Check if a token type represents header title text.
 */
static inline bool md_is_header_text(MdTokenType type) {
    return type == MD_HEADER_TEXT_1 ||
           type == MD_HEADER_TEXT_2 ||
           type == MD_HEADER_TEXT_3 ||
           type == MD_HEADER_TEXT_4 ||
           type == MD_HEADER_TEXT_5 ||
           type == MD_HEADER_TEXT_6;
}

#ifdef __cplusplus
}
#endif

#endif // MDE_MARKDOWN_H
