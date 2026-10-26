/*
 * interface.h - interactive terminal front end. All user-facing output
 * lives here; the file and directory modules are used as a library.
 */
#ifndef CFILE_INTERFACE_H
#define CFILE_INTERFACE_H

/* Runs the interactive shell until the user quits or stdin reaches EOF.
 * Returns the process exit status. */
int cf_ui_run(int show_banner);

/* Prints command line usage (for --help and invalid options). */
void cf_ui_print_usage(const char *prog);

#endif /* CFILE_INTERFACE_H */
