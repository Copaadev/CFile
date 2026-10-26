/*
 * files.h - operations on individual files: copy, move, rename, remove
 * and metadata. This module never prints; callers report errors using
 * cf_error_string().
 *
 * Every function returns 0 on success or a negative cf_status value.
 * For CF_ERRNO the reason is available in errno.
 */
#ifndef CFILE_FILES_H
#define CFILE_FILES_H

#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "utils.h"

typedef struct {
    char path[CF_PATH_MAX];
    char link_target[CF_PATH_MAX]; /* only set for symbolic links */
    mode_t mode;
    uid_t uid;
    gid_t gid;
    off_t size;
    nlink_t nlink;
    ino_t ino;
    time_t mtime;
    time_t atime;
    int is_symlink;
} cf_info;

/* If dst is an existing directory the result is dst/basename(src),
 * otherwise dst itself. */
int cf_files_resolve_dest(const char *src, const char *dst,
                          char *out, size_t len);

/* Copies a regular file (symlinks are followed, permission bits are
 * preserved). Fails with EEXIST when the target exists and overwrite
 * is 0. A partially written target is removed on error. */
int cf_file_copy(const char *src, const char *dst, int overwrite);

/* Moves a file or directory. Falls back to copy + unlink across
 * filesystems for regular files. */
int cf_file_move(const char *src, const char *dst, int overwrite);

/* Renames within the same directory; new_name must be a plain name. */
int cf_file_rename(const char *path, const char *new_name);

/* Removes a non-directory entry. Symbolic links are removed, never
 * followed. Directories fail with EISDIR. */
int cf_file_remove(const char *path);

/* Collects metadata without following a final symbolic link. */
int cf_file_info(const char *path, cf_info *out);

#endif /* CFILE_FILES_H */
