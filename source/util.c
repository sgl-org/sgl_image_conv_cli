#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

/* Case-insensitive strcmp (strcasecmp is not available on MSVC). */
static int stricmp_compat(const char *a, const char *b)
{
#ifdef _WIN32
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}

void util_basename(const char *path, char *out, size_t out_size)
{
    const char *slash1;
    const char *slash2;
    const char *base;

    if (path == NULL || out == NULL || out_size == 0) {
        return;
    }

    slash1 = strrchr(path, '/');
    slash2 = strrchr(path, '\\');
    base = path;
    if (slash1 != NULL && slash1 + 1 > base) {
        base = slash1 + 1;
    }
    if (slash2 != NULL && slash2 + 1 > base) {
        base = slash2 + 1;
    }

    strncpy(out, base, out_size - 1);
    out[out_size - 1] = '\0';
}

void util_strip_extension(const char *name, char *out, size_t out_size)
{
    const char *dot;
    size_t len;

    if (name == NULL || out == NULL || out_size == 0) {
        return;
    }

    dot = strrchr(name, '.');
    /* Mirror the JS rule: strip the final ".something" (dot not at pos 0) */
    if (dot != NULL && dot != name && dot[1] != '\0') {
        len = (size_t)(dot - name);
    } else {
        len = strlen(name);
    }

    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, name, len);
    out[len] = '\0';
}

void util_make_c_identifier(const char *name, char *out, size_t out_size)
{
    size_t i;
    size_t pos = 0;

    if (name == NULL || out == NULL || out_size == 0) {
        return;
    }

    for (i = 0; name[i] != '\0' && pos < out_size - 1; i++) {
        unsigned char c = (unsigned char)name[i];
        if (isalnum(c) || c == '_') {
            out[pos++] = (char)c;
        } else {
            out[pos++] = '_';
        }
    }
    out[pos] = '\0';

    /* First character must not be a digit */
    if (out[0] != '\0' && isdigit((unsigned char)out[0])) {
        if (out_size >= 2) {
            memmove(out + 1, out, strlen(out) + 1);
            out[0] = '_';
        } else {
            out[0] = '\0';
        }
    }
}

int util_ends_with_ci(const char *str, const char *suffix)
{
    size_t str_len;
    size_t suffix_len;

    if (str == NULL || suffix == NULL) {
        return 0;
    }

    str_len = strlen(str);
    suffix_len = strlen(suffix);
    if (suffix_len > str_len) {
        return 0;
    }

    return stricmp_compat(str + str_len - suffix_len, suffix);
}

int util_is_valid_identifier(const char *name)
{
    size_t i;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (!(isalpha((unsigned char)name[0]) || name[0] == '_')) {
        return 0;
    }
    for (i = 1; name[i] != '\0'; i++) {
        if (!(isalnum((unsigned char)name[i]) || name[i] == '_')) {
            return 0;
        }
    }
    return 1;
}

int util_parse_color(const char *hex, int *r, int *g, int *b)
{
    size_t len;
    char clean[8];
    size_t i;
    size_t pos = 0;
    unsigned value;

    if (hex == NULL) {
        return 0;
    }

    len = strlen(hex);
    for (i = 0; i < len && pos < sizeof(clean) - 1; i++) {
        if (hex[i] != '#') {
            clean[pos++] = hex[i];
        }
    }
    clean[pos] = '\0';

    if (strlen(clean) != 6) {
        return 0;
    }
    for (i = 0; i < 6; i++) {
        if (!isxdigit((unsigned char)clean[i])) {
            return 0;
        }
    }

    value = (unsigned)strtoul(clean, NULL, 16);
    *r = (int)((value >> 16) & 0xFF);
    *g = (int)((value >> 8) & 0xFF);
    *b = (int)(value & 0xFF);
    return 1;
}

int util_parse_hex_address(const char *value, uint32_t *address)
{
    const char *p;
    size_t i;
    size_t len;
    unsigned long result;

    if (value == NULL || address == NULL) {
        return 0;
    }

    p = value;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    if (p[0] == '\0') {
        return 0;
    }

    len = strlen(p);
    for (i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)p[i])) {
            return 0;
        }
    }

    result = strtoul(p, NULL, 16);
    *address = (uint32_t)result;
    return 1;
}

void util_format_bytes(size_t bytes, char *out, size_t out_size)
{
    if (bytes < 1024u) {
        snprintf(out, out_size, "%u B", (unsigned)bytes);
    } else if (bytes < 1024u * 1024u) {
        snprintf(out, out_size, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(out, out_size, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    }
}

FILE *util_fopen(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t wpath[4096];
    wchar_t wmode[16];

    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath,
                            (int)(sizeof(wpath) / sizeof(wpath[0]))) == 0) {
        return fopen(path, mode);
    }
    if (MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode,
                            (int)(sizeof(wmode) / sizeof(wmode[0]))) == 0) {
        return fopen(path, mode);
    }
    return _wfopen(wpath, wmode);
#else
    return fopen(path, mode);
#endif
}
