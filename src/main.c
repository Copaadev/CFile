#include <stdio.h>
#include <string.h>

#include "directory.h"
#include "interface.h"
#include "utils.h"

int main(int argc, char **argv)
{
    int no_color = 0;
    int banner = 1;
    const char *start_dir = NULL;
    int options_done = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (!options_done && strcmp(a, "--") == 0) {
            options_done = 1;
        } else if (!options_done && (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0)) {
            cf_ui_print_usage(argv[0]);
            return 0;
        } else if (!options_done && (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0)) {
            printf("cfile %s\n", CF_VERSION);
            return 0;
        } else if (!options_done && strcmp(a, "--no-color") == 0) {
            no_color = 1;
        } else if (!options_done && strcmp(a, "--no-banner") == 0) {
            banner = 0;
        } else if (!options_done && a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "cfile: unknown option '%s'\nTry '%s --help'.\n", a, argv[0]);
            return 2;
        } else if (start_dir == NULL) {
            start_dir = a;
        } else {
            fprintf(stderr, "cfile: too many arguments\nTry '%s --help'.\n", argv[0]);
            return 2;
        }
    }

    cf_color_init(no_color);
    if (start_dir != NULL && cf_dir_change(start_dir) != CF_OK) {
        fprintf(stderr, "cfile: cannot enter '%s': %s\n", start_dir,
                cf_error_string(CF_ERRNO));
        return 1;
    }
    return cf_ui_run(banner);
}
