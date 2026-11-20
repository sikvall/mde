/**
 * @file export.h
 * @brief Document export subsystem for HTML, XML, EPUB, and LaTeX.
 *
 * Uses pandoc when available and appropriate (and requires pandoc for EPUB),
 * with built-in C11 exporters for HTML5, structured XML, and LaTeX as
 * self-contained fallbacks.
 */

#ifndef MDE_EXPORT_H
#define MDE_EXPORT_H

#include "buffer.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Supported export target formats.
 */
typedef enum {
    EXPORT_HTML = 0,       /**< HTML5 web document (.html) */
    EXPORT_XML,            /**< Structured XML / DocBook 5 (.xml) */
    EXPORT_EPUB,           /**< EPUB 3 e-book (.epub, via pandoc) */
    EXPORT_LATEX,          /**< LaTeX document (.tex) */
    EXPORT_FORMAT_COUNT    /**< Sentinel count of export formats */
} ExportFormat;

/**
 * @brief Export backend engine selection.
 */
typedef enum {
    EXPORT_ENGINE_AUTO = 0, /**< Prefer pandoc if available; fall back to built-in for HTML/XML/LaTeX */
    EXPORT_ENGINE_BUILTIN,  /**< Force built-in C11 exporter (unavailable for EPUB) */
    EXPORT_ENGINE_PANDOC    /**< Force pandoc external converter */
} ExportEngine;

/**
 * @brief Check whether the `pandoc` executable is available on PATH.
 * @return true if an executable `pandoc` binary is found in PATH.
 */
bool export_has_pandoc(void);

/**
 * @brief Get human-readable display name for an export format.
 * @param fmt Export format enum value.
 * @return Constant string such as "HTML", "XML", "EPUB", "LaTeX".
 */
const char *export_format_name(ExportFormat fmt);

/**
 * @brief Get standard file extension (including leading dot) for an export format.
 * @param fmt Export format enum value.
 * @return Constant string such as ".html", ".xml", ".epub", ".tex".
 */
const char *export_format_ext(ExportFormat fmt);

/**
 * @brief Derive a default output file path from the source buffer path and target format.
 *
 * Replaces a trailing ".md" or ".markdown" extension (or appends the new extension).
 * Falls back to "untitled.<ext>" if src_path is NULL or empty.
 *
 * @param src_path Source file path (may be NULL).
 * @param fmt Target export format.
 * @param out_path Destination path buffer.
 * @param max_len Capacity of out_path.
 */
void export_default_path(const char *src_path, ExportFormat fmt, char *out_path, size_t max_len);

/**
 * @brief Export a Markdown text buffer to the specified format and destination path.
 *
 * Uses pandoc safely via fork/execvp (never a shell) when appropriate, or falls back
 * to built-in C11 converters for HTML5, XML, and LaTeX.
 *
 * @param buf Source document buffer.
 * @param fmt Target format (HTML, XML, EPUB, LaTeX).
 * @param out_path Destination file path.
 * @param engine Engine preference (AUTO, BUILTIN, or PANDOC).
 * @param out_engine_used Optional buffer receiving the engine actually used ("pandoc" or "built-in").
 * @param engine_cap Capacity of out_engine_used.
 * @param err_msg Optional buffer receiving an error message on failure.
 * @param err_cap Capacity of err_msg.
 * @return true on success, false on failure.
 */
bool export_document(const Buffer *buf, ExportFormat fmt, const char *out_path,
                     ExportEngine engine, char *out_engine_used, size_t engine_cap,
                     char *err_msg, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif // MDE_EXPORT_H
