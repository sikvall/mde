/**
 * @file config.c
 * @brief Implementation of configuration loading and persistence.
 */

#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

void config_init_defaults(MdeConfig *cfg) {
    if (!cfg) return;
    cfg->wrap_col = 78;
    cfg->wrap_mode = WRAP_MODE_SOFT;
    cfg->tab_size = 2;
    cfg->make_backup = true;
    cfg->view_mode = 0;
    cfg->show_keybindings = true;
}

/**
 * @brief Resolve the user configuration file path according to XDG/legacy layout.
 *
 * Checks for $HOME/.config/mde/config; if $HOME/.config does not exist,
 * falls back to $HOME/.mde/config.
 *
 * @param out_path Buffer to receive resolved configuration path.
 * @param cap Capacity of out_path buffer in bytes.
 * @param for_write If true, ensures parent directory is created.
 * @return true if path was resolved, false on error.
 */
static bool get_config_path(char *out_path, size_t cap, bool for_write) {
    if (cap < PATH_MAX) return false;

    const char *xdg_env = getenv("XDG_CONFIG_HOME");
    if (xdg_env && xdg_env[0]) {
        char app_dir[PATH_MAX];
        int an = snprintf(app_dir, sizeof(app_dir), "%s/mde", xdg_env);
        if (an < 0 || (size_t)an >= sizeof(app_dir)) return false;
        if (for_write) {
            mkdir(xdg_env, 0755);
            mkdir(app_dir, 0755);
        }
        int cn = snprintf(out_path, cap, "%s/config", app_dir);
        return (cn > 0 && (size_t)cn < cap);
    }

    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;

    char xdg_dir[PATH_MAX];
    int n = snprintf(xdg_dir, sizeof(xdg_dir), "%s/.config", home);
    if (n < 0 || (size_t)n >= sizeof(xdg_dir)) return false;

    struct stat st;
    bool has_xdg = (stat(xdg_dir, &st) == 0 && S_ISDIR(st.st_mode));

    if (has_xdg) {
        char app_dir[PATH_MAX];
        int an = snprintf(app_dir, sizeof(app_dir), "%s/mde", xdg_dir);
        if (an < 0 || (size_t)an >= sizeof(app_dir)) return false;
        if (for_write) {
            mkdir(app_dir, 0755);
        }
        int cn = snprintf(out_path, cap, "%s/config", app_dir);
        return (cn > 0 && (size_t)cn < cap);
    } else {
        char mde_dot_dir[PATH_MAX];
        int dn = snprintf(mde_dot_dir, sizeof(mde_dot_dir), "%s/.mde", home);
        if (dn < 0 || (size_t)dn >= sizeof(mde_dot_dir)) return false;
        if (for_write) {
            mkdir(mde_dot_dir, 0755);
        }
        int cn = snprintf(out_path, cap, "%s/config", mde_dot_dir);
        return (cn > 0 && (size_t)cn < cap);
    }
}

bool config_load(MdeConfig *cfg) {
    if (!cfg) return false;
    config_init_defaults(cfg);

    char path[PATH_MAX];
    if (!get_config_path(path, sizeof(path), false)) return false;

    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = p;
        char *val = eq + 1;

        // Trim key
        char *kend = key + strlen(key);
        while (kend > key && isspace((unsigned char)*(kend - 1))) *(--kend) = '\0';

        // Trim val
        while (*val && isspace((unsigned char)*val)) val++;
        char *vend = val + strlen(val);
        while (vend > val && isspace((unsigned char)*(vend - 1))) *(--vend) = '\0';

        if (strcmp(key, "wrap_mode") == 0) {
            if (strcasecmp(val, "soft") == 0) {
                cfg->wrap_mode = WRAP_MODE_SOFT;
            } else if (strcasecmp(val, "hard") == 0) {
                cfg->wrap_mode = WRAP_MODE_HARD;
            } else if (strcasecmp(val, "off") == 0 || strcasecmp(val, "none") == 0) {
                cfg->wrap_mode = WRAP_MODE_OFF;
            }
        } else if (strcmp(key, "wrap_col") == 0) {
            cfg->wrap_col = atoi(val);
        } else if (strcmp(key, "tab_size") == 0) {
            cfg->tab_size = atoi(val);
        } else if (strcmp(key, "make_backup") == 0) {
            cfg->make_backup = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        } else if (strcmp(key, "view_mode") == 0) {
            cfg->view_mode = atoi(val);
        } else if (strcmp(key, "show_keybindings") == 0) {
            cfg->show_keybindings = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        }
    }

    fclose(f);
    return true;
}

bool config_save(const MdeConfig *cfg) {
    if (!cfg) return false;

    char path[PATH_MAX];
    if (!get_config_path(path, sizeof(path), true)) return false;

    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "# mde configuration\n");
    const char *mode_str = "soft";
    if (cfg->wrap_mode == WRAP_MODE_HARD) mode_str = "hard";
    else if (cfg->wrap_mode == WRAP_MODE_OFF) mode_str = "off";
    fprintf(f, "wrap_mode=%s\n", mode_str);
    fprintf(f, "wrap_col=%d\n", cfg->wrap_col);
    fprintf(f, "tab_size=%d\n", cfg->tab_size);
    fprintf(f, "make_backup=%s\n", cfg->make_backup ? "true" : "false");
    fprintf(f, "view_mode=%d\n", cfg->view_mode);
    fprintf(f, "show_keybindings=%s\n", cfg->show_keybindings ? "true" : "false");

    fclose(f);
    return true;
}
