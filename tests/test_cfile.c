/*
 * Unit tests for the utils, files and directory modules.
 * Everything runs inside a private directory created with mkdtemp();
 * nothing outside of it is ever touched.
 */
#define _XOPEN_SOURCE 700 /* nftw() for test cleanup */

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "directory.h"
#include "files.h"
#include "utils.h"

static int g_run;
static int g_failed;
static char g_root[512];

#define CHECK(cond)                                                        \
    do {                                                                   \
        g_run++;                                                           \
        if (!(cond)) {                                                     \
            g_failed++;                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                  \
    } while (0)

#define CHECK_ERRNO(call, code)                                            \
    do {                                                                   \
        int rc_ = (call);                                                  \
        int e_ = errno;                                                    \
        CHECK(rc_ == CF_ERRNO && e_ == (code));                            \
    } while (0)

/* ---------------- helpers ---------------- */

static void path_in(char *out, const char *name)
{
    snprintf(out, CF_PATH_MAX, "%s/%s", g_root, name);
}

static int write_file(const char *name, const char *data, size_t len)
{
    char p[CF_PATH_MAX];
    int fd;
    ssize_t n;

    path_in(p, name);
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    n = write(fd, data, len);
    close(fd);
    return n == (ssize_t)len ? 0 : -1;
}

static int read_file(const char *name, char *buf, size_t cap)
{
    char p[CF_PATH_MAX];
    int fd;
    ssize_t n;

    path_in(p, name);
    fd = open(p, O_RDONLY);
    if (fd < 0)
        return -1;
    n = read(fd, buf, cap - 1);
    close(fd);
    if (n < 0)
        return -1;
    buf[n] = '\0';
    return (int)n;
}

static int exists(const char *name)
{
    char p[CF_PATH_MAX];
    struct stat st;

    path_in(p, name);
    return lstat(p, &st) == 0;
}

static int has_content(const char *name, const char *expected)
{
    char buf[256];

    return read_file(name, buf, sizeof buf) >= 0 && strcmp(buf, expected) == 0;
}

static int rm_callback(const char *path, const struct stat *st, int flag,
                       struct FTW *ftw)
{
    (void)st;
    (void)ftw;
    return flag == FTW_DP ? rmdir(path) : unlink(path);
}

static void cleanup_root(void)
{
    /* Safety: only delete what this program created. */
    if (strstr(g_root, "cfile-test-") != NULL)
        nftw(g_root, rm_callback, 16, FTW_DEPTH | FTW_PHYS);
}

/* ---------------- utils ---------------- */

static void test_utils(void)
{
    char buf[64];
    char out[CF_PATH_MAX];
    char line[128];
    char *argv[8];

    cf_format_size(0, buf, sizeof buf);
    CHECK(strcmp(buf, "0 B") == 0);
    cf_format_size(1023, buf, sizeof buf);
    CHECK(strcmp(buf, "1023 B") == 0);
    cf_format_size(1536, buf, sizeof buf);
    CHECK(strcmp(buf, "1.5 KiB") == 0);
    cf_format_size(5ULL * 1024 * 1024 * 1024, buf, sizeof buf);
    CHECK(strcmp(buf, "5.0 GiB") == 0);
    cf_format_size(1234, buf, 4); /* must truncate safely */
    CHECK(strlen(buf) == 3);

    cf_format_mode(S_IFREG | 0644, buf, sizeof buf);
    CHECK(strcmp(buf, "-rw-r--r--") == 0);
    cf_format_mode(S_IFDIR | 0755, buf, sizeof buf);
    CHECK(strcmp(buf, "drwxr-xr-x") == 0);
    cf_format_mode(S_IFREG | 04755, buf, sizeof buf);
    CHECK(strcmp(buf, "-rwsr-xr-x") == 0);
    cf_format_mode(S_IFDIR | 01777, buf, sizeof buf);
    CHECK(strcmp(buf, "drwxrwxrwt") == 0);
    cf_format_mode(S_IFLNK | 0777, buf, sizeof buf);
    CHECK(buf[0] == 'l');

    snprintf(line, sizeof line, "cp 'a b' \"c d\" e\\ f");
    CHECK(cf_tokenize(line, argv, 8) == 4);
    CHECK(strcmp(argv[0], "cp") == 0 && strcmp(argv[1], "a b") == 0);
    CHECK(strcmp(argv[2], "c d") == 0 && strcmp(argv[3], "e f") == 0);
    snprintf(line, sizeof line, "   ");
    CHECK(cf_tokenize(line, argv, 8) == 0);
    snprintf(line, sizeof line, "rm 'oops");
    CHECK(cf_tokenize(line, argv, 8) == -1);
    snprintf(line, sizeof line, "a b c");
    CHECK(cf_tokenize(line, argv, 2) == -2);
    snprintf(line, sizeof line, "x \"\" y");
    CHECK(cf_tokenize(line, argv, 8) == 3 && argv[1][0] == '\0');

    CHECK(cf_valid_filename("file.txt"));
    CHECK(!cf_valid_filename(""));
    CHECK(!cf_valid_filename("."));
    CHECK(!cf_valid_filename(".."));
    CHECK(!cf_valid_filename("a/b"));

    CHECK(cf_icontains("Hello World", "lo wo"));
    CHECK(cf_icontains("README.md", "readme"));
    CHECK(!cf_icontains("abc", "abcd"));
    CHECK(cf_icontains("abc", ""));

    CHECK(cf_path_join(out, sizeof out, "a", "b") == 0 && strcmp(out, "a/b") == 0);
    CHECK(cf_path_join(out, sizeof out, "a/", "b") == 0 && strcmp(out, "a/b") == 0);
    CHECK(cf_path_join(out, 3, "abc", "d") == -1);

    CHECK(cf_basename("/x/y/z.txt", out, sizeof out) == 0 && strcmp(out, "z.txt") == 0);
    CHECK(cf_basename("dir/", out, sizeof out) == 0 && strcmp(out, "dir") == 0);
    CHECK(cf_basename("/", out, sizeof out) == -1);
    CHECK(cf_dirname("/x/y/z.txt", out, sizeof out) == 0 && strcmp(out, "/x/y") == 0);
    CHECK(cf_dirname("z.txt", out, sizeof out) == 0 && strcmp(out, ".") == 0);
    CHECK(cf_dirname("/z", out, sizeof out) == 0 && strcmp(out, "/") == 0);

    cf_sanitize("a\033[31mb\n", out, sizeof out);
    CHECK(strchr(out, '\033') == NULL && strchr(out, '\n') == NULL);
}

/* ---------------- directories ---------------- */

static void test_mkdir_rmdir(void)
{
    char p[CF_PATH_MAX];

    path_in(p, "d1");
    CHECK(cf_dir_make(p, 0) == CF_OK);
    CHECK(exists("d1"));
    CHECK_ERRNO(cf_dir_make(p, 0), EEXIST);

    path_in(p, "missing/child");
    CHECK_ERRNO(cf_dir_make(p, 0), ENOENT);
    CHECK(!exists("missing"));

    path_in(p, "deep/er/still");
    CHECK(cf_dir_make(p, 1) == CF_OK);
    CHECK(exists("deep/er/still"));
    CHECK(cf_dir_make(p, 1) == 1); /* already there */
    CHECK_ERRNO(cf_dir_make("", 0), EINVAL);

    write_file("plain.txt", "x", 1);
    path_in(p, "plain.txt");
    CHECK_ERRNO(cf_dir_make(p, 1), EEXIST);

    /* rmdir */
    path_in(p, "d1");
    CHECK(cf_dir_remove(p) == CF_OK);
    CHECK(!exists("d1"));
    CHECK_ERRNO(cf_dir_remove(p), ENOENT);

    path_in(p, "deep");
    CHECK_ERRNO(cf_dir_remove(p), ENOTEMPTY);
    CHECK(exists("deep/er/still"));

    path_in(p, "plain.txt");
    CHECK_ERRNO(cf_dir_remove(p), ENOTDIR);
    CHECK(exists("plain.txt"));
}

/* ---------------- copy ---------------- */

static void test_copy(void)
{
    char a[CF_PATH_MAX];
    char b[CF_PATH_MAX];
    char big[200000];
    char back[sizeof big + 1];
    struct stat st;

    write_file("src.txt", "hello", 5);
    chmod((path_in(a, "src.txt"), a), 0640);
    path_in(b, "copy.txt");

    CHECK(cf_file_copy(a, b, 0) == CF_OK);
    CHECK(has_content("copy.txt", "hello"));
    CHECK(has_content("src.txt", "hello"));
    CHECK(stat(b, &st) == 0 && (st.st_mode & 07777) == 0640);

    /* No silent overwrite. */
    write_file("other.txt", "different", 9);
    path_in(a, "other.txt");
    CHECK_ERRNO(cf_file_copy(a, b, 0), EEXIST);
    CHECK(has_content("copy.txt", "hello"));
    CHECK(cf_file_copy(a, b, 1) == CF_OK);
    CHECK(has_content("copy.txt", "different"));

    /* Copy into a directory keeps the base name. */
    path_in(b, "dest");
    CHECK(cf_dir_make(b, 0) == CF_OK);
    path_in(a, "src.txt");
    CHECK(cf_file_copy(a, b, 0) == CF_OK);
    CHECK(has_content("dest/src.txt", "hello"));

    /* Same file, directory source, missing source. */
    path_in(a, "src.txt");
    CHECK(cf_file_copy(a, a, 1) == CF_ESAME);
    CHECK(has_content("src.txt", "hello"));
    path_in(a, "dest");
    path_in(b, "dest2");
    CHECK_ERRNO(cf_file_copy(a, b, 0), EISDIR);
    CHECK(!exists("dest2"));
    path_in(a, "nope.txt");
    CHECK_ERRNO(cf_file_copy(a, b, 0), ENOENT);
    CHECK(!exists("dest2"));

    /* Larger than the internal buffer: data must survive intact. */
    for (size_t i = 0; i < sizeof big; i++)
        big[i] = (char)('a' + (i % 26));
    write_file("big.bin", big, sizeof big);
    path_in(a, "big.bin");
    path_in(b, "big-copy.bin");
    CHECK(cf_file_copy(a, b, 0) == CF_OK);
    CHECK(read_file("big-copy.bin", back, sizeof back) == (int)sizeof big);
    CHECK(memcmp(big, back, sizeof big) == 0);

    /* Empty file. */
    write_file("empty", "", 0);
    path_in(a, "empty");
    path_in(b, "empty-copy");
    CHECK(cf_file_copy(a, b, 0) == CF_OK);
    CHECK(stat(b, &st) == 0 && st.st_size == 0);
}

/* ---------------- rename ---------------- */

static void test_rename(void)
{
    char a[CF_PATH_MAX];

    write_file("old.txt", "data", 4);
    write_file("taken.txt", "keep", 4);
    path_in(a, "old.txt");

    CHECK(cf_file_rename(a, "new.txt") == CF_OK);
    CHECK(!exists("old.txt") && has_content("new.txt", "data"));

    path_in(a, "new.txt");
    CHECK_ERRNO(cf_file_rename(a, "taken.txt"), EEXIST);
    CHECK(has_content("taken.txt", "keep") && exists("new.txt"));
    CHECK(cf_file_rename(a, "new.txt") == CF_ESAME);

    CHECK(cf_file_rename(a, "") == CF_EBADNAME);
    CHECK(cf_file_rename(a, "..") == CF_EBADNAME);
    CHECK(cf_file_rename(a, "x/y") == CF_EBADNAME);
    CHECK(exists("new.txt"));

    path_in(a, "ghost.txt");
    CHECK_ERRNO(cf_file_rename(a, "z.txt"), ENOENT);

    /* Directories can be renamed too. */
    cf_dir_make((path_in(a, "folder"), a), 0);
    CHECK(cf_file_rename(a, "folder2") == CF_OK);
    CHECK(exists("folder2") && !exists("folder"));
}

/* ---------------- move ---------------- */

static void test_move(void)
{
    char a[CF_PATH_MAX];
    char b[CF_PATH_MAX];

    write_file("m1.txt", "one", 3);
    path_in(a, "m1.txt");
    path_in(b, "m1-moved.txt");
    CHECK(cf_file_move(a, b, 0) == CF_OK);
    CHECK(!exists("m1.txt") && has_content("m1-moved.txt", "one"));

    /* Into an existing directory. */
    cf_dir_make((path_in(a, "mdir"), a), 0);
    path_in(b, "m1-moved.txt");
    CHECK(cf_file_move(b, a, 0) == CF_OK);
    CHECK(has_content("mdir/m1-moved.txt", "one"));

    /* Refuses to clobber unless asked to. */
    write_file("m2.txt", "two", 3);
    write_file("m3.txt", "three", 5);
    path_in(a, "m2.txt");
    path_in(b, "m3.txt");
    CHECK_ERRNO(cf_file_move(a, b, 0), EEXIST);
    CHECK(exists("m2.txt") && has_content("m3.txt", "three"));
    CHECK(cf_file_move(a, b, 1) == CF_OK);
    CHECK(!exists("m2.txt") && has_content("m3.txt", "two"));

    CHECK(cf_file_move(b, b, 1) == CF_ESAME);
    CHECK(has_content("m3.txt", "two"));

    path_in(a, "ghost");
    CHECK_ERRNO(cf_file_move(a, b, 1), ENOENT);

    /* Directories move with their content. */
    cf_dir_make((path_in(a, "tree"), a), 0);
    write_file("tree/leaf.txt", "leaf", 4);
    path_in(b, "tree-moved");
    CHECK(cf_file_move(a, b, 0) == CF_OK);
    CHECK(has_content("tree-moved/leaf.txt", "leaf") && !exists("tree"));
}

/* ---------------- remove ---------------- */

static void test_remove(void)
{
    char a[CF_PATH_MAX];
    char b[CF_PATH_MAX];

    write_file("r1.txt", "bye", 3);
    path_in(a, "r1.txt");
    CHECK(cf_file_remove(a) == CF_OK);
    CHECK(!exists("r1.txt"));
    CHECK_ERRNO(cf_file_remove(a), ENOENT);
    CHECK_ERRNO(cf_file_remove(""), EINVAL);

    /* Directories are refused and keep their contents. */
    cf_dir_make((path_in(a, "rdir"), a), 0);
    write_file("rdir/inner.txt", "i", 1);
    CHECK_ERRNO(cf_file_remove(a), EISDIR);
    CHECK(exists("rdir/inner.txt"));

    /* Removing a symlink must not touch its target. */
    write_file("target.txt", "safe", 4);
    path_in(a, "target.txt");
    path_in(b, "link.txt");
    CHECK(symlink(a, b) == 0);
    CHECK(cf_file_remove(b) == CF_OK);
    CHECK(!exists("link.txt") && has_content("target.txt", "safe"));
}

/* ---------------- info, list, size, search ---------------- */

static void test_info(void)
{
    char a[CF_PATH_MAX];
    char b[CF_PATH_MAX];
    cf_info info;

    write_file("i.txt", "12345", 5);
    path_in(a, "i.txt");
    chmod(a, 0600);
    CHECK(cf_file_info(a, &info) == CF_OK);
    CHECK(S_ISREG(info.mode) && info.size == 5);
    CHECK((info.mode & 07777) == 0600 && !info.is_symlink);

    path_in(b, "i-link");
    CHECK(symlink("i.txt", b) == 0);
    CHECK(cf_file_info(b, &info) == CF_OK);
    CHECK(info.is_symlink && strcmp(info.link_target, "i.txt") == 0);

    path_in(a, "absent");
    CHECK_ERRNO(cf_file_info(a, &info), ENOENT);
}

static void test_list_size_search(void)
{
    char p[CF_PATH_MAX];
    char sub[CF_PATH_MAX];
    cf_dirlist list;
    cf_dirstats stats;

    cf_dir_make((path_in(p, "lt"), p), 0);
    cf_dir_make((path_in(p, "lt/Zdir"), p), 0);
    cf_dir_make((path_in(p, "lt/adir"), p), 0);
    write_file("lt/b.txt", "bbb", 3);
    write_file("lt/A.txt", "aa", 2);
    write_file("lt/.hidden", "h", 1);
    path_in(sub, "lt/adir/Report-2024.txt");
    write_file("lt/adir/Report-2024.txt", "1234567890", 10);
    path_in(p, "lt/b.txt");
    path_in(sub, "lt/link");
    CHECK(symlink("b.txt", sub) == 0);

    path_in(p, "lt");
    CHECK(cf_dir_list(p, 0, &list) == CF_OK);
    CHECK(list.count == 5); /* adir Zdir A.txt b.txt link */
    CHECK(strcmp(list.items[0].name, "adir") == 0);
    CHECK(strcmp(list.items[1].name, "Zdir") == 0);
    CHECK(strcmp(list.items[2].name, "A.txt") == 0);
    CHECK(strcmp(list.items[3].name, "b.txt") == 0);
    CHECK(strcmp(list.items[4].name, "link") == 0);
    CHECK(S_ISLNK(list.items[4].mode) &&
          strcmp(list.items[4].link_target, "b.txt") == 0);
    cf_dirlist_free(&list);
    CHECK(list.items == NULL && list.count == 0);

    CHECK(cf_dir_list(p, 1, &list) == CF_OK && list.count == 6);
    cf_dirlist_free(&list);

    path_in(sub, "lt/b.txt");
    CHECK_ERRNO(cf_dir_list(sub, 0, &list), ENOTDIR);
    path_in(sub, "lt/none");
    CHECK_ERRNO(cf_dir_list(sub, 0, &list), ENOENT);

    /* Recursive size: files b.txt(3) A.txt(2) .hidden(1) Report(10) + link. */
    CHECK(cf_dir_size(p, &stats) == CF_OK);
    CHECK(stats.bytes == 16);
    CHECK(stats.dirs == 2);
    CHECK(stats.files == 5);
    CHECK(stats.errors == 0);
}

typedef struct {
    int count;
    int stop_after;
    char first[CF_PATH_MAX];
} search_ctx;

static int search_cb(const char *path, const struct stat *st, void *vctx)
{
    search_ctx *c = vctx;

    (void)st;
    if (c->count == 0)
        snprintf(c->first, sizeof c->first, "%s", path);
    c->count++;
    return c->stop_after > 0 && c->count >= c->stop_after;
}

static void test_search(void)
{
    char p[CF_PATH_MAX];
    char q[CF_PATH_MAX];
    search_ctx ctx = {0, 0, ""};
    unsigned long skipped = 99;

    /* lt/ from the previous test contains adir/Report-2024.txt. */
    path_in(p, "lt");
    CHECK(cf_dir_search(p, "report", 8, search_cb, &ctx, &skipped) == CF_OK);
    CHECK(ctx.count == 1 && skipped == 0);
    CHECK(strstr(ctx.first, "adir/Report-2024.txt") != NULL);

    ctx = (search_ctx){0, 0, ""};
    CHECK(cf_dir_search(p, ".txt", 8, search_cb, &ctx, NULL) == CF_OK);
    CHECK(ctx.count == 3); /* A.txt b.txt Report-2024.txt */

    ctx = (search_ctx){0, 0, ""};
    CHECK(cf_dir_search(p, "zzz-no-match", 8, search_cb, &ctx, NULL) == CF_OK);
    CHECK(ctx.count == 0);

    ctx = (search_ctx){0, 1, ""};
    CHECK(cf_dir_search(p, ".txt", 8, search_cb, &ctx, NULL) == CF_OK);
    CHECK(ctx.count == 1); /* callback asked to stop */

    /* Depth limit: Report is two levels below the root. */
    ctx = (search_ctx){0, 0, ""};
    CHECK(cf_dir_search(p, "report", 0, search_cb, &ctx, NULL) == CF_OK);
    CHECK(ctx.count == 0);

    /* A symlink loop must not make the search recurse forever. */
    path_in(q, "loop");
    CHECK(cf_dir_make(q, 0) == CF_OK);
    path_in(p, "loop/self");
    CHECK(symlink(q, p) == 0);
    ctx = (search_ctx){0, 0, ""};
    CHECK(cf_dir_search(q, "self", CF_SEARCH_MAX_DEPTH, search_cb, &ctx, NULL) == CF_OK);
    CHECK(ctx.count == 1);

    path_in(p, "plain.txt");
    CHECK_ERRNO(cf_dir_search(p, "x", 4, search_cb, &ctx, NULL), ENOTDIR);
    path_in(p, "no-such-dir");
    CHECK_ERRNO(cf_dir_search(p, "x", 4, search_cb, &ctx, NULL), ENOENT);
}

static void test_change_dir(void)
{
    char orig[CF_PATH_MAX];
    char now[CF_PATH_MAX];
    char p[CF_PATH_MAX];

    CHECK(cf_dir_cwd(orig, sizeof orig) == CF_OK);
    path_in(p, "dest");
    CHECK(cf_dir_change(p) == CF_OK);
    CHECK(cf_dir_cwd(now, sizeof now) == CF_OK);
    CHECK(strstr(now, "dest") != NULL);
    path_in(p, "plain.txt");
    CHECK_ERRNO(cf_dir_change(p), ENOTDIR);
    CHECK(cf_dir_change(orig) == CF_OK);
}

/* ---------------- main ---------------- */

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    char tmpl[512];

    if (tmp == NULL || *tmp == '\0')
        tmp = "/tmp";
    snprintf(tmpl, sizeof tmpl, "%s/cfile-test-XXXXXX", tmp);
    if (mkdtemp(tmpl) == NULL) {
        perror("mkdtemp");
        return 2;
    }
    snprintf(g_root, sizeof g_root, "%s", tmpl);
    printf("CFile unit tests (sandbox: %s)\n", g_root);

    test_utils();
    test_mkdir_rmdir();
    test_copy();
    test_rename();
    test_move();
    test_remove();
    test_info();
    test_list_size_search();
    test_search();
    test_change_dir();

    cleanup_root();
    CHECK(access(g_root, F_OK) != 0); /* sandbox really removed */

    printf("%d checks, %d failed\n", g_run, g_failed);
    return g_failed == 0 ? 0 : 1;
}
