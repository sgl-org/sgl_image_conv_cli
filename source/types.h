#ifndef IMAGE_CONV_TYPES_H
#define IMAGE_CONV_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* Bitmap color formats (same set as ImageConvert.vue) */
typedef enum {
    FMT_RGB888 = 0,
    FMT_RGB565,
    FMT_RGB332,
    FMT_ARGB8888,
    FMT_ARGB4444,
    FMT_ARGB2222,
    FMT_COUNT
} color_format_t;

/* Output formats */
typedef enum {
    OUT_C = 0,
    OUT_BIN
} output_format_t;

/* Compression algorithms */
typedef enum {
    COMP_NONE = 0,
    COMP_RLE,
    COMP_QOI
} compression_t;

/* Convert settings, mirrors the settings object in ImageConvert.vue */
typedef struct {
    color_format_t format;
    output_format_t output_format;
    compression_t compression;
    int enable_transparent_fill;
    int transparent_fill_r;
    int transparent_fill_g;
    int transparent_fill_b;
    char array_name[128];
    char output_dir[1024];
    uint32_t bin_start_address;
    int combine_as_array;
    int swap_bytes;
    int batch_single_bin;
} conv_settings_t;

/* One converted image */
typedef struct {
    char source_file_name[256]; /* original file name (with extension) */
    char name[256];             /* C-safe identifier base name      */
    char bitmap_name[300];      /* C array name: <name>_bitmap      */
    int width;
    int height;
    uint8_t *bitmap_data;       /* (possibly compressed) pixel data */
    size_t bitmap_size;
    size_t original_size;       /* uncompressed size in bytes       */
} image_info_t;

/* Returns bytes per pixel for a color format, 0 on invalid format */
int bytes_per_pixel(color_format_t format);

/* Returns the SGL pixmap format macro string, e.g. SGL_PIXMAP_FMT_RLE_RGB565.
 * Writes at most buf_size bytes including the NUL terminator. */
void get_sgl_format(color_format_t format, compression_t compression,
                    char *buf, size_t buf_size);

#endif /* IMAGE_CONV_TYPES_H */
