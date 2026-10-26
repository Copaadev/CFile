#include "files.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define COPY_BUFFER_SIZE 65536

static int same_file(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

int cf_files_resolve_dest(const char *src, const char *dst,
                          char *out, size_t len)
{
    struct stat st;

    if (stat(dst, &st) == 0 && S_ISDIR(st.st_mode)) {
        char base[NAME_MAX + 1];

        if (cf_basename(src, base, sizeof base) != 0) {
            errno = EINVAL;
            return CF_ERRNO;
        }
        if (cf_path_join(out, len, dst, base) != 0) {
            errno = ENAMETOOLONG;
            return CF_ERRNO;
        }
        return CF_OK;
    }
    if (strlen(dst) >= len || dst[0] == '\0') {
        errno = dst[0] == '\0' ? EINVAL : ENAMETOOLONG;
        return CF_ERRNO;
    }
    memcpy(out, dst, strlen(dst) + 1);
    return CF_OK;
}

/* Writes the whole buffer, retrying on partial writes and EINTR. */
static int write_all(int fd, const char *buf, size_t count)
{
    while (count > 0) {
        ssize_t n = write(fd, buf, count);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        buf += n;
        count -= (size_t)n;
    }
    return 0;
}

/* Copies the contents of fd_in into fd_out. */
static int copy_stream(int fd_in, int fd_out)
{
    char buf[COPY_BUFFER_SIZE];

    for (;;) {
        ssize_t n = read(fd_in, buf, sizeof buf);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return 0;
        if (write_all(fd_out, buf, (size_t)n) != 0)
            return -1;
    }
}

int cf_file_copy(const char *src, const char *dst, int overwrite)
{
    char target[CF_PATH_MAX];
    struct stat ss;
    struct stat ds;
    int fd_in;
    int fd_out;
    int rc;

    if (src == NULL || dst == NULL) {
        errno = EINVAL;
        return CF_ERRNO;
    }
    rc = cf_files_resolve_dest(src, dst, target, sizeof target);
    if (rc != CF_OK)
        return rc;
    if (stat(src, &ss) != 0)
        return CF_ERRNO;
    if (S_ISDIR(ss.st_mode)) {
        errno = EISDIR;
        return CF_ERRNO;
    }
    if (!S_ISREG(ss.st_mode)) {
        errno = EINVAL;
        return CF_ERRNO;
    }
    if (stat(target, &ds) == 0 && same_file(&ss, &ds))
        return CF_ESAME;

    fd_in = open(src, O_RDONLY | O_CLOEXEC);
    if (fd_in < 0)
        return CF_ERRNO;
    fd_out = open(target,
                  O_WRONLY | O_CREAT | O_CLOEXEC | (overwrite ? O_TRUNC : O_EXCL),
                  0600);
    if (fd_out < 0) {
        int saved = errno;

        close(fd_in);
        errno = saved;
        return CF_ERRNO;
    }

    rc = copy_stream(fd_in, fd_out);
    if (rc == 0 && fchmod(fd_out, ss.st_mode & 07777) != 0)
        rc = -1;
    {
        int saved = errno;

        close(fd_in);
        if (close(fd_out) != 0 && rc == 0) {
            saved = errno;
            rc = -1;
        }
        if (rc != 0) {
            unlink(target); /* never leave a truncated copy behind */
            errno = saved;
            return CF_ERRNO;
        }
    }
    return CF_OK;
}

int cf_file_move(const char *src, const char *dst, int overwrite)
{
    char target[CF_PATH_MAX];
    struct stat ss;
    struct stat ts;
    int rc;

    if (src == NULL || dst == NULL) {
        errno = EINVAL;
        return CF_ERRNO;
    }
    rc = cf_files_resolve_dest(src, dst, target, sizeof target);
    if (rc != CF_OK)
        return rc;
    if (lstat(src, &ss) != 0)
        return CF_ERRNO;
    if (lstat(target, &ts) == 0) {
        if (same_file(&ss, &ts))
            return CF_ESAME;
        if (!overwrite) {
            errno = EEXIST;
            return CF_ERRNO;
        }
    }

    if (rename(src, target) == 0)
        return CF_OK;
    if (errno != EXDEV)
        return CF_ERRNO;

    /* Different filesystems: only regular files can be copied safely. */
    if (S_ISDIR(ss.st_mode))
        return CF_EXDEVDIR;
    if (!S_ISREG(ss.st_mode)) {
        errno = ENOTSUP;
        return CF_ERRNO;
    }
    rc = cf_file_copy(src, target, overwrite);
    if (rc != CF_OK)
        return rc;
    if (unlink(src) != 0) {
        int saved = errno;

        unlink(target); /* roll back so the file does not end up twice */
        errno = saved;
        return CF_ERRNO;
    }
    return CF_OK;
}

int cf_file_rename(const char *path, const char *new_name)
{
    char dir[CF_PATH_MAX];
    char target[CF_PATH_MAX];
    struct stat ss;
    struct stat ts;

    if (path == NULL || new_name == NULL) {
        errno = EINVAL;
        return CF_ERRNO;
    }
    if (!cf_valid_filename(new_name))
        return CF_EBADNAME;
    if (lstat(path, &ss) != 0)
        return CF_ERRNO;
    if (cf_dirname(path, dir, sizeof dir) != 0 ||
        cf_path_join(target, sizeof target, dir, new_name) != 0) {
        errno = ENAMETOOLONG;
        return CF_ERRNO;
    }
    if (lstat(target, &ts) == 0) {
        if (same_file(&ss, &ts))
            return CF_ESAME;
        errno = EEXIST;
        return CF_ERRNO;
    }
    if (rename(path, target) != 0)
        return CF_ERRNO;
    return CF_OK;
}

int cf_file_remove(const char *path)
{
    struct stat st;

    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return CF_ERRNO;
    }
    if (lstat(path, &st) != 0)
        return CF_ERRNO;
    if (S_ISDIR(st.st_mode)) {
        errno = EISDIR;
        return CF_ERRNO;
    }
    if (unlink(path) != 0)
        return CF_ERRNO;
    return CF_OK;
}

int cf_file_info(const char *path, cf_info *out)
{
    struct stat st;

    if (path == NULL || out == NULL) {
        errno = EINVAL;
        return CF_ERRNO;
    }
    if (strlen(path) >= sizeof out->path) {
        errno = ENAMETOOLONG;
        return CF_ERRNO;
    }
    if (lstat(path, &st) != 0)
        return CF_ERRNO;

    memset(out, 0, sizeof *out);
    memcpy(out->path, path, strlen(path) + 1);
    out->mode = st.st_mode;
    out->uid = st.st_uid;
    out->gid = st.st_gid;
    out->size = st.st_size;
    out->nlink = st.st_nlink;
    out->ino = st.st_ino;
    out->mtime = st.st_mtime;
    out->atime = st.st_atime;
    out->is_symlink = S_ISLNK(st.st_mode);

    if (out->is_symlink) {
        ssize_t n = readlink(path, out->link_target, sizeof out->link_target - 1);

        if (n >= 0)
            out->link_target[n] = '\0';
    }
    return CF_OK;
}
