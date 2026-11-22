/**
 * @file backup.h
 * @brief Emacs-style backup and auto-save recovery handling.
 */

#ifndef MDE_BACKUP_H
#define MDE_BACKUP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool backup_create_tilde(const char *file_path);
bool backup_create_autosave(const char *file_path, const void *buf_ptr);
bool backup_remove_autosave(const char *file_path);

#ifdef __cplusplus
}
#endif

#endif // MDE_BACKUP_H
