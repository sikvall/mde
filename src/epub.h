/**
 * @file epub.h
 * @brief Native Markdown to EPUB 2/3 e-book converter using libzip.
 */

#ifndef MDE_EPUB_H
#define MDE_EPUB_H

#define _POSIX_C_SOURCE 200809L
#include "buffer.h"
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Target EPUB specification standard version.
 */
typedef enum {
    EPUB_VERSION_3 = 3, /**< EPUB 3.3 (default modern e-book standard) */
    EPUB_VERSION_2 = 2  /**< EPUB 2.0.1 (legacy fallback for older e-readers) */
} EpubVersion;

/**
 * @brief E-book publication metadata.
 */
typedef struct {
    char title[256];          /**< Book title */
    char author[256];         /**< Primary author / creator */
    char language[32];        /**< Language code (e.g., "en", "sv", "de") */
    char identifier[128];     /**< Unique book ID (URN UUID, ISBN, etc.) */
    char date[64];            /**< Publication date (YYYY-MM-DD) */
    char publisher[256];      /**< Publisher name */
    char rights[256];         /**< Copyright / licensing statement */
    char description[512];    /**< Summary or abstract */
    char cover_path[PATH_MAX];/**< Path to optional cover image (JPG, PNG, WebP) */
    char css_path[PATH_MAX];  /**< Path to optional custom CSS stylesheet */
} EpubMetadata;

/**
 * @brief Callback function type for reporting conversion progress.
 *
 * @param current_step 1-based index of current operation.
 * @param total_steps Total number of steps in conversion.
 * @param step_name Descriptive title of the current phase.
 * @param user_data Caller-provided context pointer.
 */
typedef void (*EpubProgressCallback)(int current_step, int total_steps, const char *step_name, void *user_data);

/**
 * @brief Configuration options for EPUB generation.
 */
typedef struct {
    EpubVersion version;        /**< EPUB_VERSION_3 or EPUB_VERSION_2 */
    EpubMetadata meta;          /**< Explicit metadata (overrides YAML front matter) */
    bool split_h1;              /**< Split chapters on H1 ('# ') headings (default: true) */
    bool include_linked;        /**< Recursively discover and bundle linked Markdown files (default: true) */
    EpubProgressCallback progress_cb; /**< Optional progress reporter callback */
    void *user_data;            /**< User data passed to progress callback */
} EpubOptions;

/**
 * @brief Initialize an EpubOptions structure with sensible defaults.
 *
 * Defaults: EPUB_VERSION_3, split_h1 = true, include_linked = true.
 */
void epub_options_init(EpubOptions *opts);

/**
 * @brief Parse YAML front matter metadata from the beginning of a Markdown buffer.
 *
 * Scans for an opening "---" delimiter and parses key-value pairs (title, author,
 * language, date, publisher, identifier, rights, description, cover, css) until
 * a closing "---" or "..." delimiter.
 *
 * @param buf Source text buffer.
 * @param meta Destination metadata structure.
 * @param out_body_start_line Output line index where body text begins (skipping front matter).
 * @return true if front matter was present and parsed, false otherwise.
 */
bool epub_parse_front_matter(const Buffer *buf, EpubMetadata *meta, size_t *out_body_start_line);

/**
 * @brief Convert a Markdown buffer (and optionally its linked documents) to a compliant EPUB file.
 *
 * @param buf Root Markdown buffer.
 * @param out_path Destination .epub archive path.
 * @param opts Conversion options (or NULL for defaults).
 * @param err_msg Output buffer for error message if conversion fails.
 * @param err_cap Capacity of err_msg.
 * @return true on successful export, false on failure.
 */
bool epub_convert_buffer(const Buffer *buf, const char *out_path, const EpubOptions *opts,
                         char *err_msg, size_t err_cap);

/**
 * @brief Convert a Markdown file from disk to an EPUB file (CLI interface).
 *
 * @param in_md_path Source Markdown file path.
 * @param out_epub_path Destination EPUB path (or NULL to default to `<basename>.epub`).
 * @param opts Conversion options (or NULL for defaults).
 * @param err_msg Output buffer for error message if conversion fails.
 * @param err_cap Capacity of err_msg.
 * @return true on successful export, false on failure.
 */
bool epub_convert_file(const char *in_md_path, const char *out_epub_path, const EpubOptions *opts,
                       char *err_msg, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif // MDE_EPUB_H
