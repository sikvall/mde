/**
 * @file renderer.h
 * @brief Formatted Markdown preview generation and layout engine.
 *
 * Translates raw Markdown lines and token spans into styled presentation
 * chunks for the rendered preview pane, complete with decorative headers,
 * unicode bullet points, styled blockquote bars, and line mapping for
 * synchronized scrolling.
 */

#ifndef MDE_RENDERER_H
#define MDE_RENDERER_H

#define _XOPEN_SOURCE_EXTENDED 1
#include <ncursesw/curses.h>
#include <stddef.h>
#include <stdbool.h>
#include "buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A single styled text segment within a rendered display line.
 */
typedef struct {
    char *text;         /**< UTF-8 string chunk */
    size_t text_len;    /**< Length in bytes */
    attr_t attr;        /**< Ncurses video attributes and color pair */
} RenderChunk;

/**
 * @brief A single rendered line composed of one or more styled chunks.
 */
typedef struct {
    RenderChunk *chunks;    /**< Dynamic array of text chunks */
    size_t chunk_count;     /**< Number of chunks in line */
    size_t chunk_cap;       /**< Allocated capacity of chunks */
    size_t source_row;      /**< Mapping back to original buffer row (for sync-scroll) */
} RenderedLine;

/**
 * @brief A collection of rendered lines representing the preview document.
 */
typedef struct {
    RenderedLine *lines;    /**< Dynamic array of rendered lines */
    size_t count;           /**< Number of rendered lines */
    size_t cap;             /**< Allocated capacity */
} RenderedDoc;

/**
 * @brief Initialize an empty RenderedDoc structure.
 * @param doc Pointer to RenderedDoc instance.
 */
void rendered_doc_init(RenderedDoc *doc);

/**
 * @brief Free all allocated memory associated with a RenderedDoc.
 * @param doc Pointer to RenderedDoc instance.
 */
void rendered_doc_free(RenderedDoc *doc);

/**
 * @brief Generate a full rendered preview document from a text buffer.
 *
 * @param buf The source TextBuffer.
 * @param width Target display width (for horizontal rules and decorative lines).
 * @param out_doc Destination RenderedDoc structure.
 */
void render_document(const Buffer *buf, int width, RenderedDoc *out_doc);

/**
 * @brief Render a slice of buffer lines [start_row, end_row) into out_doc.
 *
 * Scans code-fence context prior to start_row so code blocks spanning the
 * viewport boundary are rendered with accurate syntax highlighting.
 * Expands range boundaries to encapsulate entire table blocks.
 *
 * @param buf The source TextBuffer.
 * @param width Target display width.
 * @param start_row Starting 0-indexed buffer line.
 * @param end_row Ending 0-indexed buffer line (exclusive).
 * @param out_doc Destination RenderedDoc structure.
 */
void render_document_range(const Buffer *buf, int width, size_t start_row, size_t end_row, RenderedDoc *out_doc);

/**
 * @brief Find the rendered line index corresponding to a given source buffer line.
 *
 * Used for synchronized vertical scrolling between the editor and preview panes.
 *
 * @param doc Pointer to rendered document.
 * @param buffer_row The 0-indexed line in the source buffer.
 * @return Index of the first rendered line mapped to or following buffer_row.
 */
size_t rendered_doc_find_line(const RenderedDoc *doc, size_t buffer_row);

/**
 * @brief Map buffer cursor (row, col) to visual column in rendered preview.
 *
 * @param buf Pointer to the source buffer.
 * @param doc Pointer to rendered document.
 * @param row Buffer line index.
 * @param col Byte offset within line.
 * @return Visual column offset in rendered line.
 */
int renderer_get_cursor_vcol(const Buffer *buf, const RenderedDoc *doc, size_t row, size_t col);

#ifdef __cplusplus
}
#endif

#endif // MDE_RENDERER_H
