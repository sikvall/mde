/**
 * @file backup.h
 * @brief Emacs-style backup and auto-save recovery handling.
 *
 * Provides mechanisms for creating single-session tilde backups (filename~)
 * upon file save and autosave recovery files (#filename#) during editing.
 */

#ifndef MDE_BACKUP_H
#define MDE_BACKUP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create an Emacs-style backup file with a tilde suffix (filename~).
 *
 * Copies the existing file on disk to filename~ before the first overwrite
 * in an editing session.
 *
 * @param file_path Canonical or relative path to the original file.
 * @return true if backup succeeded or was created, false on error.
 */
bool backup_create_tilde(const char *file_path);

/**
 * @brief Create an Emacs-style autosave recovery file (#filename#).
 *
 * Writes the current unsaved buffer state into a recovery file with
 * leading and trailing '#' in the same directory as the target file.
 *
 * @param file_path Target file path.
 * @param buf_ptr Pointer to the Buffer being edited.
 * @return true on successful write, false on I/O error.
 */
bool backup_create_autosave(const char *file_path, const void *buf_ptr);

/**
 * @brief Remove the Emacs-style autosave recovery file (#filename#).
 *
 * Called when a buffer is cleanly saved or closed to delete recovery state.
 *
 * @param file_path Target file path whose autosave file should be removed.
 * @return true on successful deletion or if file did not exist, false on error.
 */
bool backup_remove_autosave(const char *file_path);

#ifdef __cplusplus
}
#endif

#endif // MDE_BACKUP_H
