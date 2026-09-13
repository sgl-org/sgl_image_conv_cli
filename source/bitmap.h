#ifndef IMAGE_CONV_BITMAP_H
#define IMAGE_CONV_BITMAP_H

#include "types.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* Encode RGBA8888 pixel data into the selected color format.
 * Applies alpha blending with the transparent fill color the same way as
 * ImageConvert.vue (alpha < 255 && fill enabled => blend with fill color).
 * Returns a malloc'd buffer of pixel_count * bytes_per_pixel(format),
 * or NULL on failure. Caller must free(). */
uint8_t *bitmap_encode_rgba(const uint8_t *rgba, size_t pixel_count,
                            const conv_settings_t *settings, size_t *out_size);

/* Resample an RGBA8888 image with bilinear interpolation (pixel-center
 * alignment). Returns a malloc'd dst_w * dst_h * 4 buffer, or NULL on
 * failure. Caller must free(). */
uint8_t *bitmap_resample_rgba_bilinear(const uint8_t *src, int src_w, int src_h,
                                       int dst_w, int dst_h);

/* Decode a single pixel of the native bitmap format back to RGBA.
 * (Used for the converted-image preview.) */
void bitmap_decode_pixel(const uint8_t *source, size_t offset,
                         color_format_t format,
                         uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);

/* Write a nearest-neighbor down-scaled preview of the converted bitmap as
 * binary PPM (P6). max_pixels caps the preview size like the web UI.
 * Returns 1 on success. */
int bitmap_write_preview_ppm(const char *path,
                             const uint8_t *bitmap_data,
                             int width, int height,
                             color_format_t format,
                             size_t max_pixels);

/* Validate that width/height are positive and pixel count <= max_pixels.
 * Returns 0 when valid, otherwise an error message is written to err
 * (err_size bytes) and a non-zero code is returned. */
int bitmap_validate_dimensions(int width, int height, size_t max_pixels,
                               char *err, size_t err_size);

#endif /* IMAGE_CONV_BITMAP_H */
