#include "data.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

bool write_exact(FILE *out, const void *ptr, size_t size, size_t count) {
    return fwrite(ptr, size, count, out) == count;
}

bool read_exact(FILE *in, void *ptr, size_t size, size_t count) {
    return fread(ptr, size, count, in) == count;
}

struct data *empty_data(void) {
    struct data *data = malloc(sizeof(struct data));
    if (data == NULL) {
        fprintf(stderr, "ERROR: Out of memory.\n");
        return NULL;
    }

    data->last_print_time = 0;
    data->last_cache_time = 0;
    data->cache_len = 0;
    data->cache = NULL;
    return data;
}

bool data_path(char **path, const char *dir, const char *name) {
    size_t dir_len = strnlen(dir, PATH_MAX);
    size_t name_len = strnlen(name, PATH_MAX);
    size_t path_len = dir_len + name_len + 2;

    if (dir_len == PATH_MAX || name_len == PATH_MAX || path_len > PATH_MAX) {
        fprintf(stderr, "ERROR: Data path exceeds PATH_MAX.\n");
        return false;
    }

    *path = malloc(path_len);
    if (*path == NULL) {
        fprintf(stderr, "ERROR: Out of memory.\n");
        return false;
    }

    snprintf(*path, path_len, "%s/%s", dir, name);
    return true;
}

bool serialize_data(FILE *out, struct data *data) {
    if (data->cache_len > 0 && data->cache == NULL) return false;

    if (!write_exact(out, &data->last_print_time, sizeof(time_t), 1)) return false;
    if (!write_exact(out, &data->last_cache_time, sizeof(time_t), 1)) return false;
    if (!write_exact(out, &data->cache_len, sizeof(size_t), 1)) return false;

    for (size_t i = 0; i < data->cache_len; ++i) {
        if (data->cache[i] == NULL || data->cache[i]->asset == NULL) {
            size_t asset_len = 0;
            if (!write_exact(out, &asset_len, sizeof(size_t), 1)) return false;
            continue;
        }

        size_t asset_len = strnlen(data->cache[i]->asset, PATH_MAX) + 1;
        if (asset_len > PATH_MAX) return false;
        if (!write_exact(out, &asset_len, sizeof(size_t), 1)) return false;
        if (!write_exact(out, data->cache[i]->asset, 1, asset_len)) return false;
        if (!write_exact(out, &data->cache[i]->cols, sizeof(size_t), 1)) return false;
        if (!write_exact(out, &data->cache[i]->rows, sizeof(size_t), 1)) return false;
    }

    return true;
}

bool deserialize_data(FILE *in, struct data *data) {
    data->last_print_time = 0;
    data->last_cache_time = 0;
    data->cache_len = 0;
    data->cache = NULL;

    if (!read_exact(in, &data->last_print_time, sizeof(time_t), 1)) return false;
    if (!read_exact(in, &data->last_cache_time, sizeof(time_t), 1)) return false;
    if (!read_exact(in, &data->cache_len, sizeof(size_t), 1)) return false;

    if (data->cache_len == 0) {
        return true;
    }

    if (data->cache_len > SIZE_MAX / sizeof(struct cache *)) {
        return false;
    }

    data->cache = calloc(data->cache_len, sizeof(struct cache *));
    if (data->cache == NULL) {
        fprintf(stderr, "ERROR: Out of memory.\n");
        return false;
    }

    for (size_t i = 0; i < data->cache_len; ++i) {
        size_t asset_len;
        if (!read_exact(in, &asset_len, sizeof(size_t), 1)) return false;

        if (asset_len == 0) {
            data->cache[i] = NULL;
            continue;
        }

        if (asset_len > PATH_MAX) {
            return false;
        }

        data->cache[i] = malloc(sizeof(struct cache));
        if (data->cache[i] == NULL) {
            fprintf(stderr, "ERROR: Out of memory.\n");
            return false;
        }
        data->cache[i]->asset = NULL;
        data->cache[i]->cols = 0;
        data->cache[i]->rows = 0;

        data->cache[i]->asset = malloc(asset_len);
        if (data->cache[i]->asset == NULL) {
            fprintf(stderr, "ERROR: Out of memory.\n");
            return false;
        }

        if (!read_exact(in, data->cache[i]->asset, 1, asset_len)) return false;
        if (data->cache[i]->asset[asset_len - 1] != '\0') return false;
        if (!read_exact(in, &data->cache[i]->cols, sizeof(size_t), 1)) return false;
        if (!read_exact(in, &data->cache[i]->rows, sizeof(size_t), 1)) return false;
    }

    return true;
}

struct data *read_or_create_data(const char *dir, const char *name) {
    char *path = NULL;
    if (!data_path(&path, dir, name)) {
        return NULL;
    }

    FILE *file = fopen(path, "rb");
    free(path);
    if (!file) {
        return empty_data();
    }

    struct data *data = malloc(sizeof(struct data));
    if (data == NULL) {
        fprintf(stderr, "ERROR: Out of memory.\n");
        fclose(file);
        return NULL;
    }

    if (!deserialize_data(file, data)) {
        fprintf(stderr, "ERROR: Could not read data file.\n");
        fclose(file);
        free_data(data);
        return NULL;
    }

    fclose(file);
    return data;
}

bool write_data(struct data *data, const char *dir, const char *name) {
    char *path = NULL;
    if (!data_path(&path, dir, name)) {
        return false;
    }

    FILE *file = fopen(path, "wb");
    free(path);
    if (!file) {
        fprintf(stderr, "ERROR: Could not open data file.\n");
        return false;
    }

    bool ok = serialize_data(file, data);
    if (fclose(file) != 0) {
        ok = false;
    }

    if (!ok) {
        fprintf(stderr, "ERROR: Could not write data file.\n");
    }

    return ok;
}

bool is_file_or_sym(const struct dirent *entry, char *path_buffer, const char *assets_path, size_t assets_path_len, bool debug) {
    size_t entry_name_len = strnlen(entry->d_name, NAME_MAX);
    size_t path_len = assets_path_len + entry_name_len + 2;
    if (assets_path_len == PATH_MAX || entry_name_len == NAME_MAX || path_len > PATH_MAX) {
        if (debug) fprintf(stderr, "DEBUG: Ignoring asset '%s' as it exceeds PATH_MAX.\n", entry->d_name);
        return false;
    }

    snprintf(path_buffer, path_len, "%s/%s", assets_path, entry->d_name);

    struct stat st;
    if (stat(path_buffer, &st) == -1) {
        if (debug) fprintf(stderr, "DEBUG: Could not read asset '%s'.\n", entry->d_name);
        return false;
    }

    return S_ISREG(st.st_mode);
}

static size_t utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static size_t line_display_cols(const char *line, size_t line_len) {
    size_t cols = 0;

    for (size_t i = 0; i < line_len && line[i] != '\0';) {
        if (line[i] == '\033') {
            ++i;
            while (i < line_len && line[i] != '\0' && line[i] != 'm') {
                ++i;
            }
            if (i < line_len && line[i] == 'm') {
                ++i;
            }
            continue;
        }

        unsigned char c = (unsigned char) line[i];
        if ((c & 0xC0) == 0x80) {
            ++i;
            continue;
        }

        size_t char_len = utf8_char_len(c);
        if (char_len > line_len - i) {
            char_len = 1;
        }

        ++cols;
        i += char_len;
    }

    return cols;
}

bool cache_data(struct data *data, const char *assets_path, time_t system_time, bool debug) {
    DIR *directory = opendir(assets_path);
    if (directory == NULL) {
        fprintf(stderr, "ERROR: Could not read assets directory.\n");
        return false;
    }

    char *path_buffer = malloc(PATH_MAX);
    if (path_buffer == NULL) {
        fprintf(stderr, "ERROR: Out of memory.\n");
        closedir(directory);
        return false;
    }

    size_t assets_count = 0;
    size_t assets_path_len = strnlen(assets_path, PATH_MAX);
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (is_file_or_sym(entry, path_buffer, assets_path, assets_path_len, false)) {
            ++assets_count;
        }
    }

    struct cache **new_cache = NULL;
    if (assets_count > 0) {
        new_cache = calloc(assets_count, sizeof(struct cache *));
        if (new_cache == NULL) {
            fprintf(stderr, "ERROR: Out of memory.\n");
            free(path_buffer);
            closedir(directory);
            return false;
        }
    }

    rewinddir(directory);

    char *line_buffer = NULL;
    size_t line_buffer_size = 0;
    size_t index = 0;
    while ((entry = readdir(directory)) != NULL) {
        if (!is_file_or_sym(entry, path_buffer, assets_path, assets_path_len, debug)) {
            continue;
        }

        if (index >= assets_count) {
            if (debug) fprintf(stderr, "DEBUG: Ignoring asset '%s' as the directory changed while caching.\n", entry->d_name);
            continue;
        }

        FILE *file = fopen(path_buffer, "r");
        if (!file) {
            if (debug) fprintf(stderr, "DEBUG: Could not read asset '%s'.\n", entry->d_name);
            ++index;
            continue;
        }

        size_t cols = 0;
        size_t rows = 0;
        bool read_error = false;

        ssize_t line_len;
        errno = 0;
        while ((line_len = getline(&line_buffer, &line_buffer_size, file)) != -1) {
            size_t line_cols = (size_t) line_len;
            while (line_cols > 0 && (line_buffer[line_cols - 1] == '\n' || line_buffer[line_cols - 1] == '\r')) {
                --line_cols;
            }

            line_cols = line_display_cols(line_buffer, line_cols);

            if (line_cols > cols) {
                cols = line_cols;
            }
            ++rows;
        }

        if (ferror(file) || errno == ENOMEM) {
            if (debug) fprintf(stderr, "DEBUG: Could not read asset '%s'.\n", entry->d_name);
            read_error = true;
        }

        fclose(file);
        if (read_error) {
            ++index;
            continue;
        }

        new_cache[index] = malloc(sizeof(struct cache));
        if (new_cache[index] == NULL) {
            fprintf(stderr, "ERROR: Out of memory.\n");
            free(line_buffer);
            free(path_buffer);
            closedir(directory);
            for (size_t i = 0; i < assets_count; ++i) {
                if (new_cache[i] != NULL) {
                    free(new_cache[i]->asset);
                    free(new_cache[i]);
                }
            }
            free(new_cache);
            return false;
        }

        size_t asset_len = strnlen(entry->d_name, NAME_MAX) + 1;
        new_cache[index]->asset = malloc(asset_len);
        if (new_cache[index]->asset == NULL) {
            fprintf(stderr, "ERROR: Out of memory.\n");
            free(new_cache[index]);
            new_cache[index] = NULL;
            free(line_buffer);
            free(path_buffer);
            closedir(directory);
            for (size_t i = 0; i < assets_count; ++i) {
                if (new_cache[i] != NULL) {
                    free(new_cache[i]->asset);
                    free(new_cache[i]);
                }
            }
            free(new_cache);
            return false;
        }

        memcpy(new_cache[index]->asset, entry->d_name, asset_len);
        new_cache[index]->cols = cols;
        new_cache[index]->rows = rows;
        ++index;
    }

    closedir(directory);
    free(line_buffer);
    free(path_buffer);

    if (data->cache != NULL) {
        for (size_t i = 0; i < data->cache_len; ++i) {
            if (data->cache[i] != NULL) {
                free(data->cache[i]->asset);
                free(data->cache[i]);
            }
        }
    }
    free(data->cache);

    data->cache_len = assets_count;
    data->cache = new_cache;
    data->last_cache_time = system_time;

    return true;
}

void print_data(FILE *out, struct data *data) {
    if (out == NULL) {
        return;
    }

    if (data == NULL) {
        fprintf(out, "NULL\n");
        return;
    }

    fprintf(out, "data {\n");
    fprintf(out, "\tlast_print_time: %lld\n", (long long) data->last_print_time);
    fprintf(out, "\tlast_cache_time: %lld\n", (long long) data->last_cache_time);
    fprintf(out, "\tcache[%zu]: [", data->cache_len);
    for (size_t i = 0; data->cache != NULL && i < data->cache_len; ++i) {
        if (i != 0) {
            fprintf(out, ",");
        }
        if (data->cache[i] == NULL) {
            fprintf(out, "\n\t\t%zu: NULL", i);
        } else {
            fprintf(out, "\n\t\t%zu: {'%s', %zu, %zu}", i, data->cache[i]->asset, data->cache[i]->cols, data->cache[i]->rows);
        }
    }
    fprintf(out, "\n\t]\n");
    fprintf(out, "}\n");
}

void free_data(struct data *data) {
    if (data == NULL) {
        return;
    }

    if (data->cache != NULL) {
        for (size_t i = 0; i < data->cache_len; ++i) {
            if (data->cache[i] != NULL) {
                free(data->cache[i]->asset);
                free(data->cache[i]);
            }
        }
    }
    free(data->cache);
    free(data);
}
