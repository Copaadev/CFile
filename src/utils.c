#include "utils.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_color;
static int g_color256;

void cf_color_init(int disable)
{
    const char *term = getenv("TERM");
    const char *no_color = getenv("NO_COLOR");

    g_color = !disable && isatty(STDOUT_FILENO) && term != NULL &&
              strcmp(term, "dumb") != 0 && !(no_color != NULL && *no_color);
    g_color256 = (term != NULL && strstr(term, "256") != NULL) ||
                 getenv("COLORTERM") != NULL;
}

int cf_color_enabled(void)
{
    return g_color;
}

const char *cf_style_code(cf_style style)
{
    if (!g_color)
        return "";
    switch (style) {
    case CF_RESET:  return "\033[0m";
    case CF_BOLD:   return "\033[1m";
    case CF_DIM:    return "\033[90m";
    case CF_ACCENT: return g_color256 ? "\033[1;38;5;39m" : "\033[1;94m";
    case CF_OKAY:   return "\033[32m";
    case CF_ERR:    return "\033[1;31m";
    case CF_WARN:   return "\033[33m";
    case CF_DIR:    return g_color256 ? "\033[38;5;39m" : "\033[94m";
    case CF_LINK:   return "\033[36m";
    case CF_EXEC:   return "\033[32m";
    }
    return "";
}

const char *cf_error_string(int status)
{
    switch (status) {
    case CF_ESAME:
        return "source and destination are the same file";
    case CF_EBADNAME:
        return "invalid name (must not be empty, '.', '..' or contain '/')";
    case CF_EXDEVDIR:
        return "moving directories across filesystems is not supported";
    default:
        return strerror(errno);
    }
}

void cf_format_size(unsigned long long bytes, char *buf, size_t len)
{
    static const char *const units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double value = (double)bytes;
    size_t unit = 0;

    if (len == 0)
        return;
    while (value >= 1024.0 && unit < sizeof units / sizeof units[0] - 1) {
        value /= 1024.0;
        unit++;
    }
    if (unit == 0)
        snprintf(buf, len, "%llu B", bytes);
    else
        snprintf(buf, len, "%.1f %s", value, units[unit]);
}

void cf_format_mode(mode_t m, char *buf, size_t len)
{
    char s[11];

    if (S_ISREG(m))        s[0] = '-';
    else if (S_ISDIR(m))   s[0] = 'd';
    else if (S_ISLNK(m))   s[0] = 'l';
    else if (S_ISCHR(m))   s[0] = 'c';
    else if (S_ISBLK(m))   s[0] = 'b';
    else if (S_ISFIFO(m))  s[0] = 'p';
    else if (S_ISSOCK(m))  s[0] = 's';
    else                   s[0] = '?';

    s[1] = (m & S_IRUSR) ? 'r' : '-';
    s[2] = (m & S_IWUSR) ? 'w' : '-';
    s[3] = (m & S_ISUID) ? ((m & S_IXUSR) ? 's' : 'S')
                         : ((m & S_IXUSR) ? 'x' : '-');
    s[4] = (m & S_IRGRP) ? 'r' : '-';
    s[5] = (m & S_IWGRP) ? 'w' : '-';
    s[6] = (m & S_ISGID) ? ((m & S_IXGRP) ? 's' : 'S')
                         : ((m & S_IXGRP) ? 'x' : '-');
    s[7] = (m & S_IROTH) ? 'r' : '-';
    s[8] = (m & S_IWOTH) ? 'w' : '-';
    s[9] = (m & S_ISVTX) ? ((m & S_IXOTH) ? 't' : 'T')
                         : ((m & S_IXOTH) ? 'x' : '-');
    s[10] = '\0';

    if (len > 0)
        snprintf(buf, len, "%s", s);
}

void cf_format_time(time_t t, char *buf, size_t len)
{
    struct tm tmv;

    if (len == 0)
        return;
    if (localtime_r(&t, &tmv) == NULL ||
        strftime(buf, len, "%Y-%m-%d %H:%M", &tmv) == 0)
        snprintf(buf, len, "-");
}

const char *cf_type_name(mode_t mode)
{
    if (S_ISREG(mode))  return "regular file";
    if (S_ISDIR(mode))  return "directory";
    if (S_ISLNK(mode))  return "symbolic link";
    if (S_ISCHR(mode))  return "character device";
    if (S_ISBLK(mode))  return "block device";
    if (S_ISFIFO(mode)) return "FIFO";
    if (S_ISSOCK(mode)) return "socket";
    return "unknown";
}

void cf_sanitize(const char *in, char *out, size_t len)
{
    size_t j = 0;

    if (len == 0)
        return;
    for (size_t i = 0; in[i] != '\0' && j + 1 < len; i++) {
        unsigned char c = (unsigned char)in[i];

        if (c < 0x20 || c == 0x7f) {
            out[j++] = '?';
        } else if (c == 0xC2 && (unsigned char)in[i + 1] >= 0x80 &&
                   (unsigned char)in[i + 1] <= 0x9F) {
            out[j++] = '?'; /* C1 control codes encoded as UTF-8 */
            i++;
        } else {
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
}

int cf_tokenize(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *r = line;
    char *w = line;

    while (*r != '\0') {
        char quote = 0;

        while (*r != '\0' && isspace((unsigned char)*r))
            r++;
        if (*r == '\0')
            break;
        if (argc >= max_args)
            return -2;
        argv[argc++] = w;

        while (*r != '\0') {
            if (quote != 0) {
                if (*r == quote) {
                    quote = 0;
                    r++;
                } else if (quote == '"' && *r == '\\' &&
                           (r[1] == '"' || r[1] == '\\')) {
                    r++;
                    *w++ = *r++;
                } else {
                    *w++ = *r++;
                }
            } else if (*r == '\'' || *r == '"') {
                quote = *r++;
            } else if (*r == '\\' && r[1] != '\0') {
                r++;
                *w++ = *r++;
            } else if (isspace((unsigned char)*r)) {
                r++;
                break;
            } else {
                *w++ = *r++;
            }
        }
        if (quote != 0)
            return -1;
        *w++ = '\0';
    }
    return argc;
}

int cf_expand_tilde(const char *in, char *out, size_t len)
{
    const char *home = getenv("HOME");
    int n;

    if (in[0] == '~' && (in[1] == '\0' || in[1] == '/') &&
        home != NULL && *home != '\0')
        n = snprintf(out, len, "%s%s", home, in + 1);
    else
        n = snprintf(out, len, "%s", in);
    return (n < 0 || (size_t)n >= len) ? -1 : 0;
}

int cf_path_join(char *out, size_t len, const char *dir, const char *name)
{
    size_t dl = strlen(dir);
    int need_sep = dl > 0 && dir[dl - 1] != '/';
    int n = snprintf(out, len, "%s%s%s", dir, need_sep ? "/" : "", name);

    return (n < 0 || (size_t)n >= len) ? -1 : 0;
}

int cf_basename(const char *path, char *out, size_t len)
{
    size_t n = strlen(path);
    size_t start;

    while (n > 1 && path[n - 1] == '/')
        n--;
    start = n;
    while (start > 0 && path[start - 1] != '/')
        start--;
    if (n == start || n - start >= len)
        return -1;
    memcpy(out, path + start, n - start);
    out[n - start] = '\0';
    return 0;
}

int cf_dirname(const char *path, char *out, size_t len)
{
    size_t n = strlen(path);
    size_t end;

    while (n > 1 && path[n - 1] == '/')
        n--;
    end = n;
    while (end > 0 && path[end - 1] != '/')
        end--;
    if (end == 0) {
        if (len < 2)
            return -1;
        out[0] = '.';
        out[1] = '\0';
        return 0;
    }
    while (end > 1 && path[end - 1] == '/')
        end--;
    if (end >= len)
        return -1;
    memcpy(out, path, end);
    out[end] = '\0';
    return 0;
}

int cf_valid_filename(const char *name)
{
    size_t n = strlen(name);

    if (n == 0 || n > NAME_MAX)
        return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;
    return strchr(name, '/') == NULL;
}

int cf_icontains(const char *haystack, const char *needle)
{
    size_t nlen = strlen(needle);

    if (nlen == 0)
        return 1;
    for (; *haystack != '\0'; haystack++) {
        size_t i = 0;

        while (i < nlen && haystack[i] != '\0' &&
               tolower((unsigned char)haystack[i]) ==
                   tolower((unsigned char)needle[i]))
            i++;
        if (i == nlen)
            return 1;
        if (haystack[i] == '\0')
            return 0;
    }
    return 0;
}
