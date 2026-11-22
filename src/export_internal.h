/**
 * @file export_internal.h
 * @brief Internal data types and utility functions shared across export backends.
 */

#ifndef MDE_EXPORT_INTERNAL_H
#define MDE_EXPORT_INTERNAL_H

#define _POSIX_C_SOURCE 200809L
#include "export.h"
#include "markdown.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Node in the multi-file export document list.
 */
typedef struct {
    const Buffer *buf;        /**< Active buffer pointer (root or owned_buf) */
    Buffer *owned_buf;        /**< Heap buffer for linked files loaded from disk */
    char abs_path[PATH_MAX];  /**< Canonical absolute path (or "" for untitled root) */
    char base_dir[PATH_MAX];  /**< Directory used to resolve relative links */
    char display_name[256];   /**< Basename for metadata attributes */
    char anchor_id[64];       /**< Stable internal anchor ID ("mde-doc-N") */
    char title[128];          /**< Document title */
} ExportDocNode;

/**
 * @brief Collection of document nodes participating in a multi-file export.
 */
typedef struct {
    ExportDocNode *nodes;
    size_t count;
    size_t cap;
} ExportDocList;

void set_str(char *dst, size_t cap, const char *src);
bool is_skipped_export_marker(MdTokenType type);
bool is_table_sep_row(const char *line, size_t len);
size_t count_table_cols(const char *line, size_t len);
bool is_ordered_bullet(const char *line, size_t len);
size_t extract_link_or_image_span(const char *raw, const MdParsedLine *pl,
                                  size_t start_idx, size_t end_idx,
                                  const char **out_label, size_t *out_label_len,
                                  const char **out_url, size_t *out_url_len);
ssize_t find_linked_doc_index(const ExportDocList *list, size_t from_idx,
                              const char *url, size_t url_len,
                              char *out_frag, size_t frag_cap);
void slugify_heading(const char *text, size_t len, char *out_slug, size_t cap);

bool export_builtin_html(const ExportDocList *docs, const char *out_path, char *err_msg, size_t err_cap);
bool export_builtin_xml(const ExportDocList *docs, const char *out_path, char *err_msg, size_t err_cap);
bool export_builtin_latex(const ExportDocList *docs, const char *out_path, char *err_msg, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif // MDE_EXPORT_INTERNAL_H
