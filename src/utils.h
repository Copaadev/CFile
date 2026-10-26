/*
 * utils.h - shared helpers: terminal styling, formatting, path and
 * string utilities. No file-system mutation happens in this module.
 */
#ifndef CFILE_UTILS_H
#define CFILE_UTILS_H

#include <limits.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#define CF_VERSION "1.0.0"
#define CF_PATH_MAX 4096

/* Status codes returned by the file and directory modules.
 * CF_ERRNO means "the failure reason is in errno". */
typedef enum {
    CF_OK = 0,
    CF_ERRNO = -1,
    CF_ESAME = -2,     /* source and destination are the same file */
    CF_EBADNAME = -3,  /* invalid file name */
    CF_EXDEVDIR = -4   /* directory move across filesystems */
} cf_status;

/* Semantic styles; all map to "" when colors are disabled. */
typedef enum {
    CF_RESET,
    CF_BOLD,
    CF_DIM,
    CF_ACCENT,
    CF_OKAY,
    CF_ERR,
    CF_WARN,
    CF_DIR,
    CF_LINK,
    CF_EXEC
} cf_style;

/* Decides whether ANSI colors are used (TTY, TERM, NO_COLOR). */
void cf_color_init(int disable);
int cf_color_enabled(void);
const char *cf_style_code(cf_style style);

/* Human readable text for a cf_status value (uses errno for CF_ERRNO). */
const char *cf_error_string(int status);

/* Formatting. All functions always NUL-terminate when len > 0. */
void cf_format_size(unsigned long long bytes, char *buf, size_t len);
void cf_format_mode(mode_t mode, char *buf, size_t len); /* needs >= 11 */
void cf_format_time(time_t t, char *buf, size_t len);
const char *cf_type_name(mode_t mode);

/* Replaces control characters so file names cannot inject escape codes. */
void cf_sanitize(const char *in, char *out, size_t len);

/* Splits a command line in place (supports '', "" and backslash escapes).
 * Returns the argument count, -1 on an unterminated quote and -2 when
 * more than max_args arguments are present. */
int cf_tokenize(char *line, char **argv, int max_args);

/* Path helpers. They return 0 on success and -1 on failure (truncation). */
int cf_expand_tilde(const char *in, char *out, size_t len);
int cf_path_join(char *out, size_t len, const char *dir, const char *name);
int cf_basename(const char *path, char *out, size_t len);
int cf_dirname(const char *path, char *out, size_t len);
int cf_valid_filename(const char *name);

/* Case-insensitive substring test; an empty needle always matches. */
int cf_icontains(const char *haystack, const char *needle);

#endif /* CFILE_UTILS_H */
