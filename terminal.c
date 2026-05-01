#include "terminal.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define RESERVED_ROWS 1
#define BUFFER_SIZE 4096

bool get_terminal_size(int fd, size_t *cols, size_t *rows) {
    struct winsize size;
    if (!isatty(fd) || ioctl(fd, TIOCGWINSZ, &size) == -1) {
        return false;
    }

    if (size.ws_col == 0 || size.ws_row <= RESERVED_ROWS) {
        return false;
    }

    *cols = size.ws_col;
    *rows = size.ws_row - RESERVED_ROWS;
    return true;
}

bool write_all(int out, const char *buffer, size_t bytes_len) {
    size_t bytes_written = 0;

    while (bytes_written < bytes_len) {
        ssize_t result = write(out, buffer + bytes_written, bytes_len - bytes_written);
        if (result == -1) {
            if (errno == EINTR) {
                continue;
            }

            return false;
        }

        if (result == 0) {
            return false;
        }

        bytes_written += (size_t) result;
    }

    return true;
}

size_t random_bounded(size_t bound) {
    return (size_t) ((double) rand() / ((double) RAND_MAX + 1) * bound);
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

size_t text_align_padding(size_t terminal_cols, size_t text_cols, TEXT_ALIGN align) {
    if (terminal_cols <= text_cols) {
        return 0;
    }

    size_t available_cols = terminal_cols - text_cols;
    switch (align) {
        case TEXT_ALIGN_LEFT:
            return 0;
        case TEXT_ALIGN_RIGHT:
            return available_cols;
        case TEXT_ALIGN_CENTER:
            return available_cols / 2;
    }

    return available_cols / 2;
}

bool write_spaces(int out, size_t count) {
    char buffer[BUFFER_SIZE];
    memset(buffer, ' ', sizeof(buffer));

    while (count > 0) {
        size_t chunk = count < sizeof(buffer) ? count : sizeof(buffer);
        if (!write_all(out, buffer, chunk)) {
            return false;
        }

        count -= chunk;
    }

    return true;
}

bool print_random_art(int out, struct data *data, const char *assets_path, TEXT_ALIGN align, bool debug) {
    if (data->cache_len == 0) {
        if (debug) fprintf(stderr, "DEBUG: No art available.\n");

        return false;
    }

    size_t cols;
    size_t rows;
    if (!get_terminal_size(out, &cols, &rows)) {
        if (debug) fprintf(stderr, "DEBUG: Could not get terminal size.\n");
        return false;
    }

    if (debug) fprintf(stdout, "DEBUG: terminal size: %zux%zu\n", cols, rows);

    srand((unsigned int) time(NULL));

    size_t selected_index = 0;
    size_t matches = 0;
    for (size_t i = 0; i < data->cache_len; ++i) {
        if (data->cache[i] == NULL || cols < data->cache[i]->cols || rows < data->cache[i]->rows) {
            continue;
        }

        ++matches;
        if (random_bounded(matches) == 0) {
            selected_index = i;
        }
    }

    if (matches == 0) {
        if (debug) fprintf(stderr, "DEBUG: No art available for this size.\n");

        return true;
    }

    if (debug) fprintf(stdout, "DEBUG: asset = '%s'\n", data->cache[selected_index]->asset);

    size_t path_len = strnlen(assets_path, PATH_MAX) + strnlen(data->cache[selected_index]->asset, PATH_MAX) + 2;
    if (path_len > PATH_MAX) {
        fprintf(stderr, "ERROR: Asset path exceeds PATH_MAX.\n");
        return false;
    }

    char *path = malloc(path_len);
    if (!path) {
        fprintf(stderr, "ERROR: Out of memory.\n");
        return false;
    }

    snprintf(path, path_len, "%s/%s", assets_path, data->cache[selected_index]->asset);

    FILE *in = fopen(path, "r");
    if (!in) {
        fprintf(stderr, "ERROR: Could not read asset file.\n");
        free(path);
        return false;
    }

    char *line = NULL;
    size_t line_size = 0;
    ssize_t line_len;
    size_t asset_cols = 0;
    errno = 0;
    while ((line_len = getline(&line, &line_size, in)) != -1) {
        size_t line_cols = (size_t) line_len;
        while (line_cols > 0 && (line[line_cols - 1] == '\n' || line[line_cols - 1] == '\r')) {
            --line_cols;
        }

        line_cols = line_display_cols(line, line_cols);
        if (line_cols > asset_cols) {
            asset_cols = line_cols;
        }
    }

    if (ferror(in) || errno == ENOMEM) {
        fprintf(stderr, "ERROR: Could not read asset file.\n");
        free(line);
        fclose(in);
        free(path);
        return false;
    }

    if (fseek(in, 0, SEEK_SET) != 0) {
        fprintf(stderr, "ERROR: Could not read asset file.\n");
        free(line);
        fclose(in);
        free(path);
        return false;
    }

    size_t padding = text_align_padding(cols, asset_cols, align);

    errno = 0;
    while ((line_len = getline(&line, &line_size, in)) != -1) {
        if (!write_spaces(out, padding) || !write_all(out, line, (size_t) line_len)) {
            fprintf(stderr, "ERROR: Could not write asset.\n");
            free(line);
            fclose(in);
            free(path);
            return false;
        }
    }

    if (ferror(in) || errno == ENOMEM) {
        fprintf(stderr, "ERROR: Could not read asset file.\n");
        free(line);
        fclose(in);
        free(path);
        return false;
    }

    free(line);
    fclose(in);
    free(path);

    return true;
}
