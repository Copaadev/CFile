#include "interface.h"

#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "directory.h"
#include "files.h"
#include "utils.h"

#define MAX_ARGS 64
#define INPUT_MAX 4096
#define FIND_LIMIT 200
#define CMD_EXIT 99

static char g_prev_dir[CF_PATH_MAX];
static int g_stdin_tty;

/* ------------------------------------------------------------------ */
/* Output helpers                                                      */
/* ------------------------------------------------------------------ */

#define ST(x) cf_style_code(x)

static void print_tagged(FILE *stream, cf_style style, const char *tag,
                         const char *fmt, va_list ap)
{
    if (stream == stderr)
        fflush(stdout);
    fprintf(stream, "  %s[%s]%s ", ST(style), tag, ST(CF_RESET));
    vfprintf(stream, fmt, ap);
    fputc('\n', stream);
    if (stream == stderr)
        fflush(stderr);
}

static void ui_ok(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    print_tagged(stdout, CF_OKAY, " OK ", fmt, ap);
    va_end(ap);
}

static void ui_info(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    print_tagged(stdout, CF_ACCENT, "INFO", fmt, ap);
    va_end(ap);
}

static void ui_warn(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    print_tagged(stdout, CF_WARN, "WARN", fmt, ap);
    va_end(ap);
}

static void ui_err(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    print_tagged(stderr, CF_ERR, "FAIL", fmt, ap);
    va_end(ap);
}

/* Reports the failure of an operation. Must be called before anything
 * else can modify errno. */
static void report(const char *cmd, const char *path, int status)
{
    const char *msg = cf_error_string(status);

    if (path != NULL)
        ui_err("%s: '%s': %s", cmd, path, msg);
    else
        ui_err("%s: %s", cmd, msg);
}

static void print_banner(void)
{
    static const char *const art[5] = {
        "  ____ _____ _ _      ",
        " / ___|  ___(_) | ___ ",
        "| |   | |_  | | |/ _ \\",
        "| |___|  _| | | |  __/",
        " \\____|_|   |_|_|\\___|",
    };

    printf("\n");
    for (size_t i = 0; i < 5; i++)
        printf("  %s%s%s\n", ST(CF_ACCENT), art[i], ST(CF_RESET));
    printf("\n  %sProfessional CLI file manager%s  %sv%s%s\n",
           ST(CF_BOLD), ST(CF_RESET), ST(CF_DIM), CF_VERSION, ST(CF_RESET));
}

/* ------------------------------------------------------------------ */
/* Input helpers                                                       */
/* ------------------------------------------------------------------ */

/* Returns 0 on success, -1 on EOF/error, -2 when the line was too long. */
static int read_line(char *buf, size_t len)
{
    size_t n;

    if (fgets(buf, (int)len, stdin) == NULL)
        return -1;
    n = strlen(buf);
    if (n > 0 && buf[n - 1] == '\n') {
        buf[n - 1] = '\0';
        return 0;
    }
    if (n == len - 1) {
        int ch;

        while ((ch = getchar()) != '\n' && ch != EOF)
            ;
        return -2;
    }
    return 0;
}

/* Asks a yes/no question; anything but y/yes counts as "no". */
static int confirm(const char *fmt, ...)
{
    char answer[64];
    va_list ap;
    int rc;

    printf("  %s[ ?? ]%s ", ST(CF_WARN), ST(CF_RESET));
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf(" %s[y/N]%s ", ST(CF_DIM), ST(CF_RESET));
    fflush(stdout);

    rc = read_line(answer, sizeof answer);
    if (!g_stdin_tty)
        printf("%s\n", rc == 0 ? answer : "");
    if (rc != 0)
        return 0;
    return strcasecmp(answer, "y") == 0 || strcasecmp(answer, "yes") == 0;
}

/* Parses leading single-letter options. Returns the index of the first
 * operand, or -1 on an unknown option (already reported). */
static int parse_opts(int argc, char **argv, const char *allowed, unsigned *mask)
{
    int i = 1;

    *mask = 0;
    for (; i < argc; i++) {
        const char *a = argv[i];

        if (a[0] != '-' || a[1] == '\0')
            break;
        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        for (const char *c = a + 1; *c != '\0'; c++) {
            const char *pos = strchr(allowed, *c);

            if (pos == NULL) {
                ui_err("%s: unknown option '-%c' (use '--' before names "
                       "that start with '-')", argv[0], *c);
                return -1;
            }
            *mask |= 1u << (pos - allowed);
        }
    }
    return i;
}

static int arg_path(const char *cmd, const char *arg, char *out)
{
    if (cf_expand_tilde(arg, out, CF_PATH_MAX) != 0) {
        ui_err("%s: path is too long", cmd);
        return -1;
    }
    return 0;
}

static void usage_error(const char *cmd, const char *usage)
{
    ui_err("usage: %s", usage);
    (void)cmd;
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

static int cmd_ls(int argc, char **argv)
{
    char path[CF_PATH_MAX];
    char real[PATH_MAX];
    cf_dirlist list;
    unsigned mask;
    int first = parse_opts(argc, argv, "a", &mask);
    unsigned long dirs = 0;
    unsigned long others = 0;
    unsigned long long bytes = 0;
    char buf[64];
    int rc;

    if (first < 0)
        return 1;
    if (argc - first > 1) {
        usage_error("ls", "ls [-a] [dir]");
        return 1;
    }
    if (arg_path("ls", argc > first ? argv[first] : ".", path) != 0)
        return 1;
    rc = cf_dir_list(path, (int)(mask & 1u), &list);
    if (rc != CF_OK) {
        report("ls", path, rc);
        return 1;
    }

    if (realpath(path, real) == NULL)
        snprintf(real, sizeof real, "%s", path);
    printf("\n  %sDirectory%s  %s\n\n", ST(CF_DIM), ST(CF_RESET), real);
    printf("  %s%-10s  %10s  %-16s  %s%s\n", ST(CF_DIM), "PERMISSIONS",
           "SIZE", "MODIFIED", "NAME", ST(CF_RESET));

    for (size_t i = 0; i < list.count; i++) {
        const cf_entry *e = &list.items[i];
        char perm[11];
        char size[32];
        char when[32];
        char safe[NAME_MAX + 1];
        cf_style style = CF_RESET;
        const char *suffix = "";

        cf_format_mode(e->mode, perm, sizeof perm);
        cf_format_time(e->mtime, when, sizeof when);
        cf_sanitize(e->name, safe, sizeof safe);
        if (S_ISREG(e->mode)) {
            cf_format_size((unsigned long long)e->size, size, sizeof size);
            bytes += (unsigned long long)e->size;
            if (e->mode & (S_IXUSR | S_IXGRP | S_IXOTH))
                style = CF_EXEC;
        } else {
            snprintf(size, sizeof size, "-");
        }
        if (S_ISDIR(e->mode)) {
            style = CF_DIR;
            suffix = "/";
            dirs++;
        } else {
            if (S_ISLNK(e->mode))
                style = CF_LINK;
            others++;
        }

        printf("  %-11s  %10s  %-16s  %s%s%s%s", perm, size, when,
               ST(style), safe, ST(CF_RESET), suffix);
        if (S_ISLNK(e->mode)) {
            char target[sizeof e->link_target];

            cf_sanitize(e->link_target, target, sizeof target);
            printf(" %s-> %s%s", ST(CF_DIM), target, ST(CF_RESET));
        }
        putchar('\n');
    }

    cf_format_size(bytes, buf, sizeof buf);
    printf("\n  %s%lu %s, %lu %s, %s%s\n\n", ST(CF_DIM), dirs,
           dirs == 1 ? "directory" : "directories", others,
           others == 1 ? "file" : "files", buf, ST(CF_RESET));
    if (list.skipped > 0)
        ui_warn("%zu entries could not be read and were skipped", list.skipped);
    cf_dirlist_free(&list);
    return 0;
}

static int cmd_cd(int argc, char **argv)
{
    char path[CF_PATH_MAX];
    char old[CF_PATH_MAX];
    const char *arg = argc > 1 ? argv[1] : "~";
    int rc;

    if (argc > 2) {
        usage_error("cd", "cd [dir|-]");
        return 1;
    }
    if (strcmp(arg, "-") == 0) {
        if (g_prev_dir[0] == '\0') {
            ui_err("cd: no previous directory");
            return 1;
        }
        snprintf(path, sizeof path, "%s", g_prev_dir);
    } else if (arg_path("cd", arg, path) != 0) {
        return 1;
    }
    if (cf_dir_cwd(old, sizeof old) != CF_OK)
        old[0] = '\0';
    rc = cf_dir_change(path);
    if (rc != CF_OK) {
        report("cd", path, rc);
        return 1;
    }
    snprintf(g_prev_dir, sizeof g_prev_dir, "%s", old);
    return 0;
}

static int cmd_pwd(int argc, char **argv)
{
    char cwd[CF_PATH_MAX];

    (void)argc;
    (void)argv;
    if (cf_dir_cwd(cwd, sizeof cwd) != CF_OK) {
        report("pwd", NULL, CF_ERRNO);
        return 1;
    }
    printf("  %s\n", cwd);
    return 0;
}

static int cmd_mkdir(int argc, char **argv)
{
    unsigned mask;
    int first = parse_opts(argc, argv, "p", &mask);
    int failed = 0;

    if (first < 0)
        return 1;
    if (first >= argc) {
        usage_error("mkdir", "mkdir [-p] <dir>...");
        return 1;
    }
    for (int i = first; i < argc; i++) {
        char path[CF_PATH_MAX];
        int rc;

        if (arg_path("mkdir", argv[i], path) != 0) {
            failed = 1;
            continue;
        }
        rc = cf_dir_make(path, (int)(mask & 1u));
        if (rc == 1) {
            ui_info("'%s' already exists", path);
        } else if (rc != CF_OK) {
            report("mkdir", path, rc);
            failed = 1;
        } else {
            ui_ok("Created directory '%s'", path);
        }
    }
    return failed;
}

/* Shared implementation of rm and rmdir with a single confirmation. */
static int remove_many(int argc, char **argv, int directories)
{
    const char *cmd = argv[0];
    unsigned char valid[MAX_ARGS] = {0};
    unsigned mask;
    int first = parse_opts(argc, argv, "", &mask);
    int count = 0;
    int failed = 0;

    if (first < 0)
        return 1;
    if (first >= argc) {
        usage_error(cmd, directories ? "rmdir <dir>..." : "rm <file>...");
        return 1;
    }

    for (int i = first; i < argc; i++) {
        char path[CF_PATH_MAX];
        struct stat st;

        if (arg_path(cmd, argv[i], path) != 0) {
            failed = 1;
            continue;
        }
        if (lstat(path, &st) != 0) {
            report(cmd, path, CF_ERRNO);
            failed = 1;
            continue;
        }
        if (!directories && S_ISDIR(st.st_mode)) {
            ui_err("rm: '%s' is a directory (use rmdir for empty directories)",
                   path);
            failed = 1;
            continue;
        }
        if (directories && !S_ISDIR(st.st_mode)) {
            ui_err("rmdir: '%s' is not a directory", path);
            failed = 1;
            continue;
        }
        if (count == 0)
            printf("\n");
        valid[i] = 1;
        count++;
        if (S_ISREG(st.st_mode)) {
            char size[32];

            cf_format_size((unsigned long long)st.st_size, size, sizeof size);
            printf("    - %s (%s)\n", path, size);
        } else {
            printf("    - %s (%s)\n", path, cf_type_name(st.st_mode));
        }
    }
    if (count == 0)
        return 1;

    printf("\n");
    if (!confirm("Permanently remove %d item%s?", count, count == 1 ? "" : "s")) {
        ui_info("Cancelled. Nothing was removed.");
        return 0;
    }
    for (int i = first; i < argc; i++) {
        char path[CF_PATH_MAX];
        int rc;

        if (!valid[i] || arg_path(cmd, argv[i], path) != 0)
            continue;
        rc = directories ? cf_dir_remove(path) : cf_file_remove(path);
        if (rc != CF_OK) {
            report(cmd, path, rc);
            failed = 1;
        } else {
            ui_ok("Removed '%s'", path);
        }
    }
    return failed;
}

static int cmd_rm(int argc, char **argv)
{
    return remove_many(argc, argv, 0);
}

static int cmd_rmdir(int argc, char **argv)
{
    return remove_many(argc, argv, 1);
}

/* Shared implementation of cp and mv. */
static int transfer(int argc, char **argv, int move)
{
    const char *cmd = argv[0];
    char src[CF_PATH_MAX];
    char dst[CF_PATH_MAX];
    char target[CF_PATH_MAX];
    unsigned mask;
    int first = parse_opts(argc, argv, "", &mask);
    int rc;

    if (first < 0)
        return 1;
    if (argc - first != 2) {
        usage_error(cmd, move ? "mv <src> <dst>" : "cp <src> <dst>");
        return 1;
    }
    if (arg_path(cmd, argv[first], src) != 0 ||
        arg_path(cmd, argv[first + 1], dst) != 0)
        return 1;

    rc = move ? cf_file_move(src, dst, 0) : cf_file_copy(src, dst, 0);
    if (rc == CF_ERRNO && errno == EEXIST) {
        if (cf_files_resolve_dest(src, dst, target, sizeof target) != CF_OK)
            snprintf(target, sizeof target, "%s", dst);
        if (!confirm("'%s' already exists. Overwrite?", target)) {
            ui_info("Cancelled. Nothing was changed.");
            return 0;
        }
        rc = move ? cf_file_move(src, dst, 1) : cf_file_copy(src, dst, 1);
    }
    if (rc != CF_OK) {
        report(cmd, src, rc);
        return 1;
    }
    if (cf_files_resolve_dest(src, dst, target, sizeof target) != CF_OK)
        snprintf(target, sizeof target, "%s", dst);
    ui_ok("%s '%s' -> '%s'", move ? "Moved" : "Copied", src, target);
    return 0;
}

static int cmd_cp(int argc, char **argv)
{
    return transfer(argc, argv, 0);
}

static int cmd_mv(int argc, char **argv)
{
    return transfer(argc, argv, 1);
}

static int cmd_rename(int argc, char **argv)
{
    char path[CF_PATH_MAX];
    unsigned mask;
    int first = parse_opts(argc, argv, "", &mask);
    int rc;

    if (first < 0)
        return 1;
    if (argc - first != 2) {
        usage_error("rename", "rename <path> <new-name>");
        return 1;
    }
    if (arg_path("rename", argv[first], path) != 0)
        return 1;
    rc = cf_file_rename(path, argv[first + 1]);
    if (rc != CF_OK) {
        report("rename", path, rc);
        return 1;
    }
    ui_ok("Renamed '%s' to '%s'", path, argv[first + 1]);
    return 0;
}

static void info_row(const char *label, const char *fmt, ...)
{
    va_list ap;

    printf("  %s%-12s%s ", ST(CF_DIM), label, ST(CF_RESET));
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

static void print_info(const char *path, const cf_info *in)
{
    char perm[11];
    char size[32];
    char when[32];
    char safe[CF_PATH_MAX];
    char name[CF_PATH_MAX];
    char real[PATH_MAX];
    const struct passwd *pw = getpwuid(in->uid);
    const struct group *gr = getgrgid(in->gid);

    cf_format_mode(in->mode, perm, sizeof perm);
    cf_format_size((unsigned long long)in->size, size, sizeof size);
    if (cf_basename(path, name, sizeof name) != 0)
        snprintf(name, sizeof name, "%s", path);
    cf_sanitize(name, safe, sizeof safe);

    printf("\n  %s%s%s\n\n", ST(CF_ACCENT), safe, ST(CF_RESET));
    if (!in->is_symlink && realpath(path, real) != NULL)
        cf_sanitize(real, safe, sizeof safe);
    else
        cf_sanitize(path, safe, sizeof safe);
    info_row("Path", "%s", safe);
    info_row("Type", "%s", cf_type_name(in->mode));
    info_row("Size", "%s (%llu bytes)", size, (unsigned long long)in->size);
    info_row("Permissions", "%s (%04o)", perm, (unsigned)(in->mode & 07777));
    if (pw != NULL && gr != NULL)
        info_row("Owner", "%s:%s", pw->pw_name, gr->gr_name);
    else
        info_row("Owner", "%u:%u", (unsigned)in->uid, (unsigned)in->gid);
    info_row("Links", "%lu", (unsigned long)in->nlink);
    info_row("Inode", "%llu", (unsigned long long)in->ino);
    cf_format_time(in->mtime, when, sizeof when);
    info_row("Modified", "%s", when);
    cf_format_time(in->atime, when, sizeof when);
    info_row("Accessed", "%s", when);

    if (in->is_symlink) {
        cf_sanitize(in->link_target, safe, sizeof safe);
        info_row("Target", "%s", safe);
    }
    if (S_ISDIR(in->mode)) {
        cf_dirstats stats;

        if (cf_dir_size(path, &stats) == CF_OK) {
            cf_format_size(stats.bytes, size, sizeof size);
            info_row("Contents", "%lu files, %lu directories, %s total",
                     stats.files, stats.dirs, size);
            if (stats.errors > 0)
                info_row("Warning", "%lu entries could not be read",
                         stats.errors);
        }
    }
    putchar('\n');
}

static int cmd_info(int argc, char **argv)
{
    unsigned mask;
    int first = parse_opts(argc, argv, "", &mask);
    int failed = 0;

    if (first < 0)
        return 1;
    if (first >= argc) {
        usage_error("info", "info <path>...");
        return 1;
    }
    for (int i = first; i < argc; i++) {
        char path[CF_PATH_MAX];
        cf_info info;
        int rc;

        if (arg_path("info", argv[i], path) != 0) {
            failed = 1;
            continue;
        }
        rc = cf_file_info(path, &info);
        if (rc != CF_OK) {
            report("info", path, rc);
            failed = 1;
            continue;
        }
        print_info(path, &info);
    }
    return failed;
}

typedef struct {
    size_t count;
    int truncated;
} find_ctx;

static int find_callback(const char *path, const struct stat *st, void *vctx)
{
    find_ctx *ctx = vctx;
    char perm[11];
    char safe[CF_PATH_MAX];
    cf_style style = CF_RESET;

    if (ctx->count >= FIND_LIMIT) {
        ctx->truncated = 1;
        return 1;
    }
    if (strncmp(path, "./", 2) == 0)
        path += 2;
    cf_format_mode(st->st_mode, perm, sizeof perm);
    cf_sanitize(path, safe, sizeof safe);
    if (S_ISDIR(st->st_mode))
        style = CF_DIR;
    else if (S_ISLNK(st->st_mode))
        style = CF_LINK;
    printf("  %s%c%s  %s%s%s%s\n", ST(CF_DIM), perm[0], ST(CF_RESET),
           ST(style), safe, ST(CF_RESET), S_ISDIR(st->st_mode) ? "/" : "");
    ctx->count++;
    return 0;
}

static int cmd_find(int argc, char **argv)
{
    char root[CF_PATH_MAX];
    unsigned mask;
    int first = parse_opts(argc, argv, "", &mask);
    unsigned long skipped = 0;
    find_ctx ctx = {0, 0};
    int rc;

    if (first < 0)
        return 1;
    if (argc - first < 1 || argc - first > 2) {
        usage_error("find", "find <name> [dir]");
        return 1;
    }
    if (arg_path("find", argc - first == 2 ? argv[first + 1] : ".", root) != 0)
        return 1;
    if (argv[first][0] == '\0') {
        ui_err("find: the search pattern must not be empty");
        return 1;
    }

    printf("\n");
    rc = cf_dir_search(root, argv[first], CF_SEARCH_MAX_DEPTH,
                       find_callback, &ctx, &skipped);
    if (rc != CF_OK) {
        report("find", root, rc);
        return 1;
    }
    if (ctx.count == 0)
        ui_info("No entries matching '%s' under '%s'", argv[first], root);
    else
        printf("\n  %s%zu match%s%s\n", ST(CF_DIM), ctx.count,
               ctx.count == 1 ? "" : "es", ST(CF_RESET));
    if (ctx.truncated)
        ui_warn("Showing the first %d matches only; refine the pattern",
                FIND_LIMIT);
    if (skipped > 0)
        ui_warn("%lu unreadable entries were skipped", skipped);
    putchar('\n');
    return 0;
}

static int cmd_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (cf_color_enabled())
        fputs("\033[H\033[2J", stdout);
    return 0;
}

static int cmd_help(int argc, char **argv);

static int cmd_quit(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return CMD_EXIT;
}

/* ------------------------------------------------------------------ */
/* Command table (single source of truth for dispatch and the menu)    */
/* ------------------------------------------------------------------ */

typedef int (*cmd_fn)(int, char **);

typedef struct {
    const char *name;
    const char *usage;
    const char *desc;
    const char *group; /* NULL hides the entry from the menu (alias) */
    cmd_fn fn;
} cmd_def;

static const cmd_def g_commands[] = {
    {"ls",     "ls [-a] [dir]",         "List contents with permissions and sizes", "Navigation",  cmd_ls},
    {"cd",     "cd [dir|-]",            "Change directory ('-' = previous)",        "Navigation",  cmd_cd},
    {"pwd",    "pwd",                   "Print the current directory",              "Navigation",  cmd_pwd},
    {"find",   "find <name> [dir]",     "Search files by name, recursively",        "Navigation",  cmd_find},
    {"info",   "info <path>...",        "Show type, size, permissions and dates",   "Files",       cmd_info},
    {"cp",     "cp <src> <dst>",        "Copy a file",                              "Files",       cmd_cp},
    {"mv",     "mv <src> <dst>",        "Move a file or directory",                 "Files",       cmd_mv},
    {"rename", "rename <path> <name>",  "Rename a file or directory",               "Files",       cmd_rename},
    {"rm",     "rm <file>...",          "Remove files (asks for confirmation)",     "Files",       cmd_rm},
    {"mkdir",  "mkdir [-p] <dir>...",   "Create directories",                       "Directories", cmd_mkdir},
    {"rmdir",  "rmdir <dir>...",        "Remove empty directories (asks first)",    "Directories", cmd_rmdir},
    {"clear",  "clear",                 "Clear the screen",                         "General",     cmd_clear},
    {"help",   "help [command]",        "Show this menu or help for a command",     "General",     cmd_help},
    {"quit",   "quit",                  "Leave CFile",                              "General",     cmd_quit},
    {"menu",   "menu",                  "Alias of help",                            NULL,          cmd_help},
    {"exit",   "exit",                  "Alias of quit",                            NULL,          cmd_quit},
    {NULL, NULL, NULL, NULL, NULL}
};

static const cmd_def *find_command(const char *name)
{
    for (const cmd_def *c = g_commands; c->name != NULL; c++)
        if (strcmp(c->name, name) == 0)
            return c;
    return NULL;
}

static void print_menu(void)
{
    const char *group = NULL;

    printf("\n");
    for (const cmd_def *c = g_commands; c->name != NULL; c++) {
        if (c->group == NULL)
            continue;
        if (group == NULL || strcmp(group, c->group) != 0) {
            if (group != NULL)
                putchar('\n');
            group = c->group;
            printf("  %s%s%s\n", ST(CF_ACCENT), group, ST(CF_RESET));
        }
        printf("    %-22s %s%s%s\n", c->usage, ST(CF_DIM), c->desc,
               ST(CF_RESET));
    }
    printf("\n  %sPaths accept ~ and quotes. Destructive commands always "
           "ask first.%s\n\n", ST(CF_DIM), ST(CF_RESET));
}

static int cmd_help(int argc, char **argv)
{
    if (argc > 1) {
        const cmd_def *c = find_command(argv[1]);

        if (c == NULL) {
            ui_err("help: unknown command '%s'", argv[1]);
            return 1;
        }
        printf("\n  %s%s%s\n  %s\n\n", ST(CF_ACCENT), c->usage, ST(CF_RESET),
               c->desc);
        return 0;
    }
    print_menu();
    return 0;
}

/* ------------------------------------------------------------------ */
/* Shell loop                                                          */
/* ------------------------------------------------------------------ */

static void print_prompt(void)
{
    char cwd[CF_PATH_MAX];
    const char *home = getenv("HOME");
    const char *shown = cwd;
    char abbreviated[CF_PATH_MAX];

    if (cf_dir_cwd(cwd, sizeof cwd) != CF_OK)
        snprintf(cwd, sizeof cwd, "?");
    if (home != NULL && home[0] != '\0' && strcmp(home, "/") != 0) {
        size_t hl = strlen(home);

        if (strncmp(cwd, home, hl) == 0 && (cwd[hl] == '/' || cwd[hl] == '\0')) {
            snprintf(abbreviated, sizeof abbreviated, "~%s", cwd + hl);
            shown = abbreviated;
        }
    }
    printf("%scfile%s %s%s%s %s>%s ", ST(CF_ACCENT), ST(CF_RESET),
           ST(CF_DIM), shown, ST(CF_RESET), ST(CF_ACCENT), ST(CF_RESET));
    fflush(stdout);
}

int cf_ui_run(int show_banner)
{
    char line[INPUT_MAX];
    char *argv[MAX_ARGS];

    g_stdin_tty = isatty(STDIN_FILENO);
    if (show_banner) {
        print_banner();
        print_menu();
    }

    for (;;) {
        const cmd_def *cmd;
        int argc;
        int rc;

        print_prompt();
        rc = read_line(line, sizeof line);

        if (rc == -1) {
            printf("\n");
            break;
        }
        if (!g_stdin_tty)
            printf("%s\n", rc == 0 ? line : "");
        if (rc == -2) {
            ui_err("input line too long (limit %d characters)", INPUT_MAX - 1);
            continue;
        }
        argc = cf_tokenize(line, argv, MAX_ARGS);
        if (argc == -1) {
            ui_err("unterminated quote");
            continue;
        }
        if (argc == -2) {
            ui_err("too many arguments (limit %d)", MAX_ARGS);
            continue;
        }
        if (argc == 0)
            continue;

        cmd = find_command(argv[0]);
        if (cmd == NULL) {
            ui_err("unknown command '%s' (type 'help' for the menu)", argv[0]);
            continue;
        }
        if (cmd->fn(argc, argv) == CMD_EXIT)
            break;
    }
    printf("  %sGoodbye.%s\n", ST(CF_DIM), ST(CF_RESET));
    return 0;
}

void cf_ui_print_usage(const char *prog)
{
    printf("CFile %s - professional CLI file manager\n\n"
           "Usage: %s [options] [directory]\n\n"
           "Options:\n"
           "  -h, --help       Show this help and exit\n"
           "  -V, --version    Show the version and exit\n"
           "      --no-color   Disable ANSI colors\n"
           "      --no-banner  Do not print the banner and menu at startup\n\n"
           "Starts an interactive session in the given directory (default: "
           "the current one).\nType 'help' inside the session for the list "
           "of commands.\n", CF_VERSION, prog);
}
