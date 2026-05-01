#ifndef WELCOME_HOME_TERMINAL_H
#define WELCOME_HOME_TERMINAL_H

#include "data.h"
#include <stdbool.h>

typedef enum {
    TEXT_ALIGN_LEFT,
    TEXT_ALIGN_CENTER,
    TEXT_ALIGN_RIGHT,
} TEXT_ALIGN;

bool print_random_art(int out, struct data *data, const char *assets_path, TEXT_ALIGN align, bool debug);

#endif //WELCOME_HOME_TERMINAL_H
