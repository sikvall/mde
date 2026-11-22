/**
 * @file config.h
 * @brief Configuration file parser and settings persistence.
 */

#ifndef MDE_CONFIG_H
#define MDE_CONFIG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int wrap_col;
    int tab_size;
    bool make_backup;
    int view_mode;
} MdeConfig;

void config_init_defaults(MdeConfig *cfg);
bool config_load(MdeConfig *cfg);
bool config_save(const MdeConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif // MDE_CONFIG_H
