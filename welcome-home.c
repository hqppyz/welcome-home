#include "xdg.h"
#include "data.h"
#include "terminal.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define NAME "welcome-home"

time_t get_path_last_mod_time(const char *path) {
    struct stat st;
    if (stat(path, &st) == -1) {
        return 0;
    }

    return st.st_mtime;
}

time_t get_local_midnight(time_t system_time) {
    struct tm *local_time = localtime(&system_time);
    if (!local_time) {
        return (time_t) -1;
    }

    struct tm midnight = *local_time;
    midnight.tm_hour = 0;
    midnight.tm_min = 0;
    midnight.tm_sec = 0;

    return mktime(&midnight);
}

int main(int argc, char **argv) {
    int status = EXIT_SUCCESS;

    bool always = false;
    bool debug = false;
    TEXT_ALIGN align = TEXT_ALIGN_CENTER;

    char *data_path = NULL;
    char *assets_path = NULL;
    struct data *data = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "adclr")) != -1) {
        switch (opt) {
            case 'a':
                always = true;
                break;
            case 'd':
                debug = true;
                break;
            case 'c':
                align = TEXT_ALIGN_CENTER;
                break;
            case 'l':
                align = TEXT_ALIGN_LEFT;
                break;
            case 'r':
                align = TEXT_ALIGN_RIGHT;
                break;
            default:
                fprintf(stdout, "USAGE: %s [-a] [-d] [-l|-c|-r]\n", argc == 0 ? "welcome-home" : argv[0]);
                goto error;
        }
    }

    time_t system_time = time(NULL);
    if (system_time == (time_t) -1) {
        fprintf(stderr, "ERROR: Could not get system time.\n");
        goto error;
    }

    time_t system_midnight = get_local_midnight(system_time);
    if (system_midnight == (time_t) -1) {
        fprintf(stderr, "ERROR: Could not get system date.\n");
        goto error;
    }

    data_path = get_data_path(NAME, debug);
    if (!data_path) goto error;

    if (debug) fprintf(stdout, "DEBUG: data_path = '%s'.\n", data_path);

    data = read_or_create_data(data_path, ".data");
    if (!data) goto error;

    if (debug) {
        fprintf(stdout, "DEBUG: ");
        print_data(stdout, data);
    }

    if (!always && data->last_print_time > system_midnight) {
        // We've already printed today

        // If the boot time is available in the system, we also print after a restart
        struct timespec system_boot_time;
        if (clock_gettime(CLOCK_BOOTTIME, &system_boot_time) != 0 || data->last_print_time > system_boot_time.tv_sec) {
            if (debug) fprintf(stdout, "DEBUG: Welcome message not necessary.\n");

            goto cleanup;
        }
    }

    assets_path = get_config_path(NAME, debug);
    if (!assets_path) goto error;

    if (debug) fprintf(stdout, "DEBUG: assets_path = '%s'.\n", assets_path);

    time_t assets_last_mod_time = get_path_last_mod_time(assets_path);
    if (data->last_cache_time == 0 || assets_last_mod_time > data->last_cache_time) {
        // Invalidate cache
        if (!cache_data(data, assets_path, system_time, debug)) goto error;
    }

    if (!print_random_art(STDOUT_FILENO, data, assets_path, align, debug)) goto error;
    data->last_print_time = system_time;

    if (!write_data(data, data_path, ".data")) goto error;

    goto cleanup;

error:
    status = EXIT_FAILURE;

cleanup:
    free_data(data);
    free(assets_path);
    free(data_path);
    return status;
}
