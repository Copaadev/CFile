#include "directory.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static int is_dot_entry(const char *name)
{
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static int entry_compare(const void *a, const void *b)
{
    const cf_entry *ea = a;
    const cf_entry *eb = b;
    int da = S_ISDIR(ea->mode) ? 0 : 1;
    int db = S_ISDIR(eb->mode) ? 0 : 1;
    int c;

    if (da != db)
        return da - db;
    c = strcasecmp(ea->name, eb->name);
    return c != 0 ? c : strcmp(ea->name, eb->name);
}

static int list_grow(cf_dirlist *list)
{
    size_t cap;
    cf_entry *items;

    if (list->count < list->capacity)
        return 0;
    cap = list->capacity == 0 ? 64 : list->capacity * 2;
    if (cap > SIZE_MAX / sizeof *items) {
        errno = ENOMEM;
        return -1;
    }
    items = realloc(list->items, cap * sizeof *items);
    if (items == NULL)
        return -1;
    list->items = items;
    list->capacity = cap;
    return 0;
}

void cf_dirlist_free(cf_dirlist *list)
{
    if (list == NULL)
        return;
    free(list->items);
    memset(list, 0, sizeof *list);
}

int cf_dir_list(const char *path, int show_hidden, cf_dirlist *out)
{
    DIR *d;
    struct dirent *de;
    int fd;

    if (path == NULL || out == NULL) {
        errno = EINVAL;
        return CF_ERRNO;
    }
    memset(out, 0, sizeof *out);
    d = opendir(path);
    if (d == NULL)
        return CF_ERRNO;
    fd = dirfd(d);

    for (;;) {
        struct stat st;
        cf_entry *e;

        errno = 0;
        de = readdir(d);
        if (de == NULL)
            break;
        if (is_dot_entry(de->d_name))
            continue;
        if (!show_hidden && de->d_name[0] == '.')
            continue;
        if (fstatat(fd, de->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            out->skipped++;
            continue;
        }
        if (list_grow(out) != 0)
            goto fail;

        e = &out->items[out->count++];
        memset(e, 0, sizeof *e);
        snprintf(e->name, sizeof e->name, "%s", de->d_name);
        e->mode = st.st_mode;
        e->size = st.st_size;
        e->mtime = st.st_mtime;
        if (S_ISLNK(st.st_mode)) {
            ssize_t n = readlinkat(fd, de->d_name, e->link_target,
                                   sizeof e->link_target - 1);

            if (n >= 0)
                e->link_target[n] = '\0';
        }
    }
    if (errno != 0)
        goto fail;

    closedir(d);
    if (out->count > 1)
        qsort(out->items, out->count, sizeof *out->items, entry_compare);
    return CF_OK;

fail:
    {
        int saved = errno;

        closedir(d);
        cf_dirlist_free(out);
        errno = saved;
        return CF_ERRNO;
    }
}

int cf_dir_make(const char *path, int parents)
{
    char buf[CF_PATH_MAX];
    size_t len;

    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return CF_ERRNO;
    }
    if (!parents)
        return mkdir(path, 0777) == 0 ? CF_OK : CF_ERRNO;

    len = strlen(path);
    if (len >= sizeof buf) {
        errno = ENAMETOOLONG;
        return CF_ERRNO;
    }
    memcpy(buf, path, len + 1);
    while (len > 1 && buf[len - 1] == '/')
        buf[--len] = '\0';

    for (char *p = buf + 1; *p != '\0'; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(buf, 0777) != 0 && errno != EEXIST)
            return CF_ERRNO;
        *p = '/';
    }
    if (mkdir(buf, 0777) == 0)
        return CF_OK;
    if (errno == EEXIST) {
        struct stat st;

        if (stat(buf, &st) == 0 && S_ISDIR(st.st_mode))
            return 1;
        errno = EEXIST;
    }
    return CF_ERRNO;
}

int cf_dir_remove(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return CF_ERRNO;
    }
    return rmdir(path) == 0 ? CF_OK : CF_ERRNO;
}

int cf_dir_change(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return CF_ERRNO;
    }
    return chdir(path) == 0 ? CF_OK : CF_ERRNO;
}

int cf_dir_cwd(char *buf, size_t len)
{
    return getcwd(buf, len) != NULL ? CF_OK : CF_ERRNO;
}

static void size_walk(const char *path, int depth, cf_dirstats *stats)
{
    struct stat st;
    DIR *d;
    struct dirent *de;

    if (lstat(path, &st) != 0) {
        stats->errors++;
        return;
    }
    if (!S_ISDIR(st.st_mode)) {
        stats->files++;
        if (S_ISREG(st.st_mode))
            stats->bytes += (unsigned long long)st.st_size;
        return;
    }
    stats->dirs++;
    if (depth >= CF_SEARCH_MAX_DEPTH) {
        stats->errors++;
        return;
    }
    d = opendir(path);
    if (d == NULL) {
        stats->errors++;
        return;
    }
    while ((de = readdir(d)) != NULL) {
        char child[CF_PATH_MAX];

        if (is_dot_entry(de->d_name))
            continue;
        if (cf_path_join(child, sizeof child, path, de->d_name) != 0) {
            stats->errors++;
            continue;
        }
        size_walk(child, depth + 1, stats);
    }
    closedir(d);
}

int cf_dir_size(const char *path, cf_dirstats *out)
{
    struct stat st;

    if (path == NULL || out == NULL) {
        errno = EINVAL;
        return CF_ERRNO;
    }
    memset(out, 0, sizeof *out);
    if (lstat(path, &st) != 0)
        return CF_ERRNO;
    size_walk(path, 0, out);
    if (S_ISDIR(st.st_mode))
        out->dirs--; /* do not count the root itself */
    return CF_OK;
}

static void search_walk(const char *dir, const char *pattern, int depth,
                        int max_depth, cf_search_cb cb, void *ctx,
                        unsigned long *skipped, int *stop)
{
    DIR *d = opendir(dir);
    struct dirent *de;

    if (d == NULL) {
        if (skipped != NULL)
            (*skipped)++;
        return;
    }
    while (!*stop && (de = readdir(d)) != NULL) {
        char child[CF_PATH_MAX];
        struct stat st;

        if (is_dot_entry(de->d_name))
            continue;
        if (cf_path_join(child, sizeof child, dir, de->d_name) != 0 ||
            lstat(child, &st) != 0) {
            if (skipped != NULL)
                (*skipped)++;
            continue;
        }
        if (cf_icontains(de->d_name, pattern) && cb(child, &st, ctx) != 0) {
            *stop = 1;
            break;
        }
        if (S_ISDIR(st.st_mode) && depth < max_depth)
            search_walk(child, pattern, depth + 1, max_depth, cb, ctx,
                        skipped, stop);
    }
    closedir(d);
}

int cf_dir_search(const char *root, const char *pattern, int max_depth,
                  cf_search_cb cb, void *ctx, unsigned long *skipped)
{
    struct stat st;
    int stop = 0;

    if (root == NULL || pattern == NULL || cb == NULL || max_depth < 0) {
        errno = EINVAL;
        return CF_ERRNO;
    }
    if (max_depth > CF_SEARCH_MAX_DEPTH)
        max_depth = CF_SEARCH_MAX_DEPTH;
    if (skipped != NULL)
        *skipped = 0;
    if (stat(root, &st) != 0)
        return CF_ERRNO;
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return CF_ERRNO;
    }
    search_walk(root, pattern, 0, max_depth, cb, ctx, skipped, &stop);
    return CF_OK;
}
