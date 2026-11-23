/**
 * @file export.h
 * @brief Document export subsystem for HTML, XML, EPUB, LaTeX, and UNIX man pages.
 *
 * Uses native built-in C11 converters for HTML, XML, EPUB, LaTeX, and man pages,
 * with optional pandoc delegation available when explicitly selected.
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
    EXPORT_EPUB,           /**< EPUB 3 e-book (.epub) */
    EXPORT_LATEX,          /**< LaTeX document (.tex) */
    EXPORT_MAN,            /**< UNIX/Linux manual page (.1) */
    EXPORT_FORMAT_COUNT    /**< Sentinel count of export formats */
} ExportFormat;

/**
 * @brief Export backend engine selection.
 */
typedef enum {
    EXPORT_ENGINE_AUTO = 0, /**< Prefer native built-in converters where available; fall back to pandoc */
    EXPORT_ENGINE_BUILTIN,  /**< Force built-in C11 exporter */
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
 * @return Constant string such as "HTML", "XML", "EPUB", "LaTeX", "Man".
 */
const char *export_format_name(ExportFormat fmt);

/**
 * @brief Get standard file extension (including leading dot) for an export format.
 * @param fmt Export format enum value.
 * @return Constant string such as ".html", ".xml", ".epub", ".tex", ".1".
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
 * @brief Count local Markdown files recursively linked from the given buffer.
 *
 * Follows relative and absolute Markdown links (`[text](file.md)`) outside
 * fenced code blocks with cycle detection.
 *
 * @param buf Source root document buffer.
 * @return Number of distinct linked Markdown files found on disk (excluding root).
 */
size_t export_count_linked_files(const Buffer *buf);

/**
 * @brief Export a Markdown text buffer to the specified format and destination path.
 *
 * Automatically includes all recursively linked local Markdown files (HTML, XML,
 * EPUB, and unified single-document LaTeX). Uses pandoc safely via fork/execvp
 * (never a shell) when appropriate, or falls back to built-in C11 converters
 * for HTML5, XML, and LaTeX.
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

/**
 * @brief Extended export function allowing explicit control over linked-file inclusion.
 *
 * @param buf Source document buffer.
 * @param fmt Target format (HTML, XML, EPUB, LaTeX).
 * @param out_path Destination file path.
 * @param engine Engine preference (AUTO, BUILTIN, or PANDOC).
 * @param include_linked If true, recursively include all locally linked Markdown files.
 * @param out_linked_count Optional pointer receiving number of linked files included.
 * @param out_engine_used Optional buffer receiving the engine actually used ("pandoc" or "built-in").
 * @param engine_cap Capacity of out_engine_used.
 * @param err_msg Optional buffer receiving an error message on failure.
 * @param err_cap Capacity of err_msg.
 * @return true on success, false on failure.
 */
bool export_document_ex(const Buffer *buf, ExportFormat fmt, const char *out_path,
                        ExportEngine engine, bool include_linked, size_t *out_linked_count,
                        char *out_engine_used, size_t engine_cap,
                        char *err_msg, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif // MDE_EXPORT_H
