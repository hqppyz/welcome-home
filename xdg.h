#ifndef WELCOME_HOME_XDG_H
#define WELCOME_HOME_XDG_H

#include <stdbool.h>

char *get_config_path(const char *config_dir, bool debug);

char *get_data_path(const char *data_dir, bool debug);

#endif //WELCOME_HOME_XDG_H
