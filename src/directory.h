/*
 * directory.h - directory operations: listing, creation, removal,
 * navigation, recursive size and search. Like files.h this module
 * never prints anything.
 */
#ifndef CFILE_DIRECTORY_H
#define CFILE_DIRECTORY_H

#include <limits.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "utils.h"

#define CF_SEARCH_MAX_DEPTH 64

typedef struct {
    char name[NAME_MAX + 1];
    char link_target[256]; /* only set for symbolic links */
    mode_t mode;
    off_t size;
    time_t mtime;
} cf_entry;

typedef struct {
    cf_entry *items;
    size_t count;
    size_t capacity;
    size_t skipped; /* entries that vanished or could not be examined */
} cf_dirlist;

typedef struct {
    unsigned long long bytes; /* sum of apparent sizes of regular files */
    unsigned long files;
    unsigned long dirs;
    unsigned long errors;     /* unreadable directories or entries */
} cf_dirstats;

/* Called for every match; return non-zero to stop the search. */
typedef int (*cf_search_cb)(const char *path, const struct stat *st, void *ctx);

/* Lists a directory sorted with directories first, then by name
 * (case-insensitive). Release with cf_dirlist_free(). */
int cf_dir_list(const char *path, int show_hidden, cf_dirlist *out);
void cf_dirlist_free(cf_dirlist *list);

/* Creates a directory. With parents set, behaves like mkdir -p and
 * returns 1 when the directory already existed. Returns 0 if created. */
int cf_dir_make(const char *path, int parents);

/* Removes an empty directory (rmdir semantics). */
int cf_dir_remove(const char *path);

/* Changes the process working directory / reads it. */
int cf_dir_change(const char *path);
int cf_dir_cwd(char *buf, size_t len);

/* Recursive size of a path. Symbolic links are counted, not followed. */
int cf_dir_size(const char *path, cf_dirstats *out);

/* Case-insensitive recursive search by file name (substring match).
 * Symbolic links are never followed. Unreadable sub-directories are
 * skipped and counted in *skipped (may be NULL). */
int cf_dir_search(const char *root, const char *pattern, int max_depth,
                  cf_search_cb cb, void *ctx, unsigned long *skipped);

#endif /* CFILE_DIRECTORY_H */
