#include "xdg.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

bool dir_exists(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool dir_absolute(const char *path) {
    return path != NULL && path[0] == '/';
}

// Thanks to Carl @ StackOverflow (https://stackoverflow.com/a/2336245)
bool mkdirs(const char *dir, mode_t mode) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    if (dir == NULL || dir[0] == '\0') return false;

    if (strnlen(dir, sizeof(tmp)) >= sizeof(tmp)) return false;

    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, mode) == -1 && !dir_exists(tmp)) {
                *p = '/';
                return false;
            }
            *p = '/';
        }
    }

    return mkdir(tmp, mode) != -1 || dir_exists(tmp);
}

char *paths_join(const char *base, const char *dir, bool debug) {
    size_t base_len = strnlen(base, PATH_MAX);
    size_t dir_len = strnlen(dir, PATH_MAX);
    size_t path_len = base_len + dir_len + 2;

    if (base_len == PATH_MAX || dir_len == PATH_MAX || path_len > PATH_MAX) {
        if (debug) fprintf(stderr, "DEBUG: Ignoring '%s/%s' as it exceeds PATH_MAX.\n", base, dir);
        return NULL;
    }

    char *path = malloc(path_len);
    if (!path) {
        fprintf(stderr, "ERROR: Out of memory.\n");
        return NULL;
    }

    snprintf(path, path_len, "%s/%s", base, dir);
    return path;
}

char *get_or_create_path(const char *base, const char *dir, bool search, bool debug) {
    char *path = paths_join(base, dir, debug);
    if (path == NULL) return NULL;

    if (search) {
        if (dir_exists(path)) return path;
    } else {
        if (!mkdirs(path, S_IRWXU)) {
            fprintf(stderr, "ERROR: Could not create directory '%s'.\n", path);
            free(path);
            return NULL;
        }

        return path;
    }

    free(path);
    return NULL;
}

char *get_home_path(const char *home_sub_dir, const char *dir, bool search, bool debug) {
    char *home = getenv("HOME");
    if (!dir_exists(home)) return NULL;

    size_t home_len = strnlen(home, PATH_MAX);
    size_t home_sub_dir_len = strnlen(home_sub_dir, PATH_MAX);
    size_t base_len = home_len + home_sub_dir_len + 2;

    if (home_len == PATH_MAX || home_sub_dir_len == PATH_MAX || base_len > PATH_MAX) {
        if (debug) fprintf(stderr, "DEBUG: Ignoring '%s/%s' as it exceeds PATH_MAX.\n", home, home_sub_dir);
        return NULL;
    }

    char *base = malloc(base_len);
    if (!base) {
        fprintf(stderr, "ERROR: Out of memory.\n");
        return NULL;
    }

    snprintf(base, base_len, "%s/%s", home, home_sub_dir);

    char *path = get_or_create_path(base, dir, search, debug);
    free(base);
    return path;
}

char *get_xdg_home_path(const char *xdg_home_env, const char *home_sub_dir, const char *dir, bool search, bool debug) {
    // ${xdg_home_env}/{dir}
    char *xdg_home = getenv(xdg_home_env);

    if (xdg_home != NULL && xdg_home[0] != '\0') {
        if (dir_absolute(xdg_home)) return get_or_create_path(xdg_home, dir, search, debug);

        if (debug) fprintf(stderr, "DEBUG: Ignoring path in %s as it is not absolute.\n", xdg_home_env);
        return get_home_path(home_sub_dir, dir, search, debug);
    }

    return get_home_path(home_sub_dir, dir, search, debug);
}

char *get_xdg_path(const char *xdg_home_env, const char *xdg_dirs_env, const char *xdg_dirs_default, const char *home_sub_dir, const char *dir, bool search, bool debug) {
    char *path = get_xdg_home_path(xdg_home_env, home_sub_dir, dir, search, debug);
    if (path != NULL) return path;

    // ${xdg_dirs_env}/{dir}
    const char *xdg_dirs = getenv(xdg_dirs_env);
    if (search) {
        if (xdg_dirs == NULL || xdg_dirs[0] == '\0') {
            xdg_dirs = xdg_dirs_default;
        }

        const char *xdg_dir = xdg_dirs;
        while (xdg_dir != NULL && *xdg_dir != '\0') {
            const char *next = strchr(xdg_dir, ':');
            size_t xdg_dir_len = next == NULL ? strlen(xdg_dir) : (size_t) (next - xdg_dir);

            if (xdg_dir_len == 0) {
                xdg_dir = next == NULL ? NULL : next + 1;
                continue;
            }

            if (xdg_dir_len >= PATH_MAX) {
                if (debug) fprintf(stderr, "DEBUG: Ignoring path in %s as it exceeds PATH_MAX.\n", xdg_dirs_env);
            } else {
                char xdg_dir_buf[PATH_MAX];
                memcpy(xdg_dir_buf, xdg_dir, xdg_dir_len);
                xdg_dir_buf[xdg_dir_len] = '\0';

                if (!dir_absolute(xdg_dir_buf)) {
                    if (debug) fprintf(stderr, "DEBUG: Ignoring path in %s as it is not absolute.\n", xdg_dirs_env);
                } else {
                    char *path = get_or_create_path(xdg_dir_buf, dir, search, debug);
                    if (path != NULL) return path;
                }
            }

            xdg_dir = next == NULL ? NULL : next + 1;
        }
    }

    return search ? get_xdg_home_path(xdg_home_env, home_sub_dir, dir, false, debug) : NULL;
}

char *get_config_path(const char *config_dir, bool debug) {
    return get_xdg_path("XDG_CONFIG_HOME", "XDG_CONFIG_DIRS", "/etc/xdg", ".config", config_dir, true, debug);
}

char *get_data_path(const char *data_dir, bool debug) {
    return get_xdg_path("XDG_DATA_HOME", "XDG_DATA_DIRS", "/usr/local/share:/usr/share", ".local/share", data_dir, true, debug);
}
