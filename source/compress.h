#ifndef IMAGE_CONV_COMPRESS_H
#define IMAGE_CONV_COMPRESS_H

#include <stdint.h>
#include <stddef.h>

/* RLE compress (format-agnostic byte streams):
 * [count][pixel bytes...] where count is 1..255 pixel repetitions.
 * identical to rleCompress() in src/utils/imageConversion.js.
 * Returns a malloc'd buffer, *out_size receives the compressed size.
 * Returns NULL on failure. */
uint8_t *rle_compress_data(const uint8_t *data, size_t data_size,
                           int bytes_per_pixel, size_t *out_size);

/* QOI-RGB565 compression aligned with the SGL decoder:
 * 13-byte header + row-offset table + per-row independent encoding.
 * rgb565_data must be width*height*2 bytes (little-endian RGB565).
 * Returns a malloc'd buffer, *out_size receives the compressed size.
 * Returns NULL on failure. */
uint8_t *qoi_compress_rgb565(const uint8_t *rgb565_data,
                             int width, int height, size_t *out_size);

#endif /* IMAGE_CONV_COMPRESS_H */
