#include "bitmap.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Round-half helper matching JS Math.round() for non-negative values. */
static int round_half_up(double v)
{
    return (int)floor(v + 0.5);
}

uint8_t *bitmap_encode_rgba(const uint8_t *rgba, size_t pixel_count,
                            const conv_settings_t *settings, size_t *out_size)
{
    color_format_t format = settings->format;
    int bpp = bytes_per_pixel(format);
    uint8_t *out;
    size_t i;
    size_t src = 0;
    size_t dst = 0;
    int fill_enabled = settings->enable_transparent_fill;

    if (bpp == 0) {
        return NULL;
    }

    out = (uint8_t *)malloc(pixel_count * (size_t)bpp);
    if (out == NULL) {
        return NULL;
    }

    for (i = 0; i < pixel_count; i++, src += 4) {
        double red   = rgba[src + 0];
        double green = rgba[src + 1];
        double blue  = rgba[src + 2];
        int alpha    = rgba[src + 3];

        if (alpha < 255 && fill_enabled) {
            double alpha_factor = (double)alpha / 255.0;
            red   = floor(red   * alpha_factor + settings->transparent_fill_r * (1.0 - alpha_factor) + 0.5);
            green = floor(green * alpha_factor + settings->transparent_fill_g * (1.0 - alpha_factor) + 0.5);
            blue  = floor(blue  * alpha_factor + settings->transparent_fill_b * (1.0 - alpha_factor) + 0.5);
        }

        switch (format) {
        case FMT_RGB888:
            out[dst++] = (uint8_t)blue;
            out[dst++] = (uint8_t)green;
            out[dst++] = (uint8_t)red;
            break;
        case FMT_RGB565: {
            unsigned value = (unsigned)(round_half_up(red / 255.0 * 31.0) << 11)
                           | (unsigned)(round_half_up(green / 255.0 * 63.0) << 5)
                           | (unsigned) round_half_up(blue / 255.0 * 31.0);
            out[dst++] = (uint8_t)(value & 0xFF);
            out[dst++] = (uint8_t)((value >> 8) & 0xFF);
            break;
        }
        case FMT_RGB332:
            out[dst++] = (uint8_t)((round_half_up(red / 255.0 * 7.0) << 5)
                                 | (round_half_up(green / 255.0 * 7.0) << 2)
                                 |  round_half_up(blue / 255.0 * 3.0));
            break;
        case FMT_ARGB8888:
            out[dst++] = (uint8_t)blue;
            out[dst++] = (uint8_t)green;
            out[dst++] = (uint8_t)red;
            out[dst++] = (uint8_t)alpha;
            break;
        case FMT_ARGB4444: {
            unsigned value = (unsigned)(round_half_up(alpha / 255.0 * 15.0) << 12)
                           | (unsigned)(round_half_up(red / 255.0 * 15.0) << 8)
                           | (unsigned)(round_half_up(green / 255.0 * 15.0) << 4)
                           | (unsigned) round_half_up(blue / 255.0 * 15.0);
            out[dst++] = (uint8_t)(value & 0xFF);
            out[dst++] = (uint8_t)((value >> 8) & 0xFF);
            break;
        }
        case FMT_ARGB2222:
            out[dst++] = (uint8_t)((round_half_up(alpha / 255.0 * 3.0) << 6)
                                 | (round_half_up(red / 255.0 * 3.0) << 4)
                                 | (round_half_up(green / 255.0 * 3.0) << 2)
                                 |  round_half_up(blue / 255.0 * 3.0));
            break;
        default:
            free(out);
            return NULL;
        }
    }

    *out_size = dst;
    return out;
}

void bitmap_decode_pixel(const uint8_t *source, size_t offset,
                         color_format_t format,
                         uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    int red = 0;
    int green = 0;
    int blue = 0;
    int alpha = 255;

    switch (format) {
    case FMT_RGB888:
        blue  = source[offset];
        green = source[offset + 1];
        red   = source[offset + 2];
        break;
    case FMT_RGB565: {
        unsigned value = (unsigned)source[offset] | ((unsigned)source[offset + 1] << 8);
        red   = ((value >> 11) & 0x1F) << 3;
        green = ((value >> 5) & 0x3F) << 2;
        blue  = (value & 0x1F) << 3;
        break;
    }
    case FMT_RGB332: {
        unsigned value = source[offset];
        red   = ((value >> 5) & 0x07) << 5;
        green = ((value >> 2) & 0x07) << 5;
        blue  = (value & 0x03) << 6;
        break;
    }
    case FMT_ARGB8888:
        blue  = source[offset];
        green = source[offset + 1];
        red   = source[offset + 2];
        alpha = source[offset + 3];
        break;
    case FMT_ARGB4444: {
        unsigned value = (unsigned)source[offset] | ((unsigned)source[offset + 1] << 8);
        alpha = ((value >> 12) & 0x0F) << 4;
        red   = ((value >> 8) & 0x0F) << 4;
        green = ((value >> 4) & 0x0F) << 4;
        blue  = (value & 0x0F) << 4;
        break;
    }
    case FMT_ARGB2222: {
        unsigned value = source[offset];
        alpha = ((value >> 6) & 0x03) << 6;
        red   = ((value >> 4) & 0x03) << 6;
        green = ((value >> 2) & 0x03) << 6;
        blue  = (value & 0x03) << 6;
        break;
    }
    default:
        break;
    }

    *r = (uint8_t)red;
    *g = (uint8_t)green;
    *b = (uint8_t)blue;
    *a = (uint8_t)alpha;
}

int bitmap_write_preview_ppm(const char *path,
                             const uint8_t *bitmap_data,
                             int width, int height,
                             color_format_t format,
                             size_t max_pixels)
{
    int preview_w = width;
    int preview_h = height;
    int bpp = bytes_per_pixel(format);
    FILE *f;
    int x, y;
    size_t pixels;
    uint8_t *rowbuf;

    if (bpp == 0) {
        return 0;
    }

    pixels = (size_t)width * (size_t)height;
    if (pixels > max_pixels) {
        double scale = sqrt((double)max_pixels / (double)pixels);
        preview_w = (int)floor((double)width * scale);
        preview_h = (int)floor((double)height * scale);
        if (preview_w < 1) preview_w = 1;
        if (preview_h < 1) preview_h = 1;
    }

    f = util_fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }

    fprintf(f, "P6\n%d %d\n255\n", preview_w, preview_h);

    rowbuf = (uint8_t *)malloc((size_t)preview_w * 3u);
    if (rowbuf == NULL) {
        fclose(f);
        return 0;
    }

    for (y = 0; y < preview_h; y++) {
        int source_y = (int)((double)y * (double)height / (double)preview_h);
        if (source_y > height - 1) {
            source_y = height - 1;
        }
        for (x = 0; x < preview_w; x++) {
            int source_x = (int)((double)x * (double)width / (double)preview_w);
            size_t source_offset;
            size_t target_offset = (size_t)x * 3u;
            uint8_t r, g, b, a;

            if (source_x > width - 1) {
                source_x = width - 1;
            }
            source_offset = ((size_t)source_y * (size_t)width + (size_t)source_x) * (size_t)bpp;
            bitmap_decode_pixel(bitmap_data, source_offset, format,
                                &r, &g, &b, &a);
            rowbuf[target_offset + 0] = r;
            rowbuf[target_offset + 1] = g;
            rowbuf[target_offset + 2] = b;
        }
        if (fwrite(rowbuf, 1, (size_t)preview_w * 3u, f) != (size_t)preview_w * 3u) {
            free(rowbuf);
            fclose(f);
            return 0;
        }
    }

    free(rowbuf);
    fclose(f);
    return 1;
}

int bitmap_validate_dimensions(int width, int height, size_t max_pixels,
                               char *err, size_t err_size)
{
    if (width <= 0 || height <= 0) {
        snprintf(err, err_size, "图片分辨率必须是有效的正整数");
        return -1;
    }
    if ((size_t)width * (size_t)height > max_pixels) {
        snprintf(err, err_size,
                 "图片分辨率超过 %u 像素限制", (unsigned)max_pixels);
        return -2;
    }
    return 0;
}
