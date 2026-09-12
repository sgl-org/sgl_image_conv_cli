#ifndef IMAGE_CONV_OUTPUT_H
#define IMAGE_CONV_OUTPUT_H

#include "types.h"

#include <stdio.h>
#include <stddef.h>

/* Generate the .c source code, mirroring createCCodeLines() in the web UI.
 * Writes text to the given stream. Returns 0 on success. */
int output_write_c_code(FILE *f, const image_info_t *images, int count,
                        const conv_settings_t *settings);

/* Generate the sgl_pixmap_t address table .c code for a combined BIN,
 * mirroring createBinCode(). Returns 0 on success. */
int output_write_bin_code(FILE *f, const image_info_t *images, int count,
                          const conv_settings_t *settings);

/* Generate the pixmap declaration code for batch single-bin mode,
 * mirroring createBatchSingleBinCode(). Returns 0 on success. */
int output_write_batch_single_bin_code(FILE *f, const image_info_t *images,
                                       int count,
                                       const conv_settings_t *settings);

/* Concatenate all image bitmaps into one malloc'd BIN buffer.
 * *out_size receives the total size. Returns NULL on failure. */
uint8_t *output_build_bin(const image_info_t *images, int count,
                          size_t *out_size);

/* Write a binary file safely. Returns 0 on success. */
int output_write_binary_file(const char *path, const uint8_t *data,
                             size_t size);

#endif /* IMAGE_CONV_OUTPUT_H */
