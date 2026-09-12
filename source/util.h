#ifndef IMAGE_CONV_UTIL_H
#define IMAGE_CONV_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Extract the file name part from a path (handles '/' and '\\'). */
void util_basename(const char *path, char *out, size_t out_size);

/* Remove the last extension (last '.' followed by non-dot chars) from name. */
void util_strip_extension(const char *name, char *out, size_t out_size);

/* Sanitize a string into a valid C identifier:
 * [^A-Za-z0-9_] -> '_', and the first char must not be a digit. */
void util_make_c_identifier(const char *name, char *out, size_t out_size);

/* Case-insensitive suffix check. Returns 1 if str ends with suffix. */
int util_ends_with_ci(const char *str, const char *suffix);

/* Validate a C identifier: only [A-Za-z_][A-Za-z0-9_]* allowed. */
int util_is_valid_identifier(const char *name);

/* Parse "#RRGGBB" into r/g/b. Returns 1 on success. */
int util_parse_color(const char *hex, int *r, int *g, int *b);

/* Parse "0x1A2B" (or bare hex digits) into an address. Returns 1 on success. */
int util_parse_hex_address(const char *value, uint32_t *address);

/* Format a byte count the same way as the web UI (B / KB / MB). */
void util_format_bytes(size_t bytes, char *out, size_t out_size);

/* Convert multi-byte (UTF-8) input path to UTF-8 safe fopen string.
 * On Windows this keeps the raw UTF-8 bytes; callers use util_fopen. */
FILE *util_fopen(const char *path, const char *mode);

#endif /* IMAGE_CONV_UTIL_H */
