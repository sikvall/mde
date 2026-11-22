/**
 * @file config.h
 * @brief Configuration file parser and settings persistence.
 *
 * Handles reading and writing persistent user configuration options
 * (wrap column, tab size, backup toggling, view mode) from
 * ~/.config/mde/config (with fallback to ~/.mde/config).
 */

#ifndef MDE_CONFIG_H
#define MDE_CONFIG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief User configuration settings.
 */
typedef struct {
    int wrap_col;       /**< Soft-wrap right margin column limit */
    int tab_size;       /**< Tab expansion visual width in spaces */
    bool make_backup;   /**< Whether to generate Emacs-style backup files (~) */
    int view_mode;      /**< Initial editor view mode (0=Editor, 1=Split, 2=Preview) */
} MdeConfig;

/**
 * @brief Initialize a configuration struct with standard default values.
 *
 * @param cfg Pointer to MdeConfig to initialize.
 */
void config_init_defaults(MdeConfig *cfg);

/**
 * @brief Load configuration options from ~/.config/mde/config or ~/.mde/config.
 *
 * @param cfg Pointer to MdeConfig to populate.
 * @return true if a configuration file was found and loaded, false otherwise.
 */
bool config_load(MdeConfig *cfg);

/**
 * @brief Save configuration options to user disk (~/.config/mde/config).
 *
 * @param cfg Pointer to MdeConfig containing settings to persist.
 * @return true on successful write, false on I/O error.
 */
bool config_save(const MdeConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif // MDE_CONFIG_H
