#include "bitmap.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Round-half helper matching JS Math.round() for non-negative values. */
static int round_half_up(double v)
{
    return (int)floor(v + 0.5);
}

/* ------------------------------------------------------------------
 * Resampling pipeline (see bitmap.h)
 * ------------------------------------------------------------------ */

/* Area-average (box) resample for downscaling. Output is premultiplied
 * alpha so transparent pixels do not bleed their RGB into edges. */
static uint8_t *box_resample_rgba_pm(const uint8_t *src, int sw, int sh,
                                     int dw, int dh)
{
    uint8_t *dst;
    int x, y;

    dst = (uint8_t *)malloc((size_t)dw * (size_t)dh * 4u);
    if (dst == NULL) {
        return NULL;
    }

    for (y = 0; y < dh; y++) {
        double fy0 = (double)y * (double)sh / (double)dh;
        double fy1 = (double)(y + 1) * (double)sh / (double)dh;
        int iy0 = (int)floor(fy0);
        int iy1 = (int)ceil(fy1);
        if (iy1 > sh) iy1 = sh;

        for (x = 0; x < dw; x++) {
            double fx0 = (double)x * (double)sw / (double)dw;
            double fx1 = (double)(x + 1) * (double)sw / (double)dw;
            int ix0 = (int)floor(fx0);
            int ix1 = (int)ceil(fx1);
            double acc[4] = {0.0, 0.0, 0.0, 0.0};
            double area = 0.0;
            uint8_t *out;
            int xx, yy, c;

            if (ix1 > sw) ix1 = sw;

            for (yy = iy0; yy < iy1; yy++) {
                double wy = ((double)yy + 1.0 < fy1) ? 1.0 : fy1 - (double)yy;
                if (yy < fy0) wy -= (fy0 - (double)yy);
                if (wy <= 0.0) continue;
                {
                    const uint8_t *row = src + (size_t)yy * (size_t)sw * 4u;
                    for (xx = ix0; xx < ix1; xx++) {
                        double wx = ((double)xx + 1.0 < fx1) ? 1.0 : fx1 - (double)xx;
                        double w;
                        if (xx < fx0) wx -= (fx0 - (double)xx);
                        if (wx <= 0.0) continue;
                        w = wx * wy;
                        acc[0] += (double)row[xx * 4 + 0] * w;
                        acc[1] += (double)row[xx * 4 + 1] * w;
                        acc[2] += (double)row[xx * 4 + 2] * w;
                        acc[3] += (double)row[xx * 4 + 3] * w;
                        area += w;
                    }
                }
            }

            out = dst + ((size_t)y * (size_t)dw + (size_t)x) * 4u;
            if (area <= 0.0) {
                out[0] = out[1] = out[2] = out[3] = 0;
                continue;
            }
            for (c = 0; c < 4; c++) {
                double v = acc[c] / area + 0.5;
                out[c] = (uint8_t)((v > 255.0) ? 255.0 : v); /* premultiplied clamp */
            }
        }
    }

    return dst;
}

/* 1D Lanczos3 kernel, x in pixels (scaled by the caller). */
static double lanczos3_kernel(double x)
{
    double px;
    if (x < 0.0) x = -x;
    if (x < 1e-9) return 1.0;
    if (x >= 3.0) return 0.0;
    px = M_PI * x;
    return 3.0 * sin(px) * sin(px / 3.0) / (px * px);
}

typedef struct {
    int first;          /* first source index (clamped)        */
    int count;          /* number of taps                      */
    double *weights;    /* normalized tap weights              */
} resample_contrib_t;

static void free_contribs(resample_contrib_t *cs, int len)
{
    int i;
    if (cs == NULL) return;
    for (i = 0; i < len; i++) {
        free(cs[i].weights);
    }
    free(cs);
}

/* Build normalized Lanczos3 contribution lists for one axis.
 * When downscaling (scale > 1) the kernel window is widened by the scale
 * factor so every source pixel keeps contributing (no aliasing). */
static resample_contrib_t *build_contribs(int src_len, int dst_len)
{
    double scale = (double)src_len / (double)dst_len;
    double filt = (scale > 1.0) ? scale : 1.0;
    double support = 3.0 * filt;
    resample_contrib_t *cs;
    int i;

    cs = (resample_contrib_t *)calloc((size_t)dst_len, sizeof(*cs));
    if (cs == NULL) {
        return NULL;
    }

    for (i = 0; i < dst_len; i++) {
        double center = ((double)i + 0.5) * scale - 0.5;
        int left = (int)floor(center - support);
        int right = (int)ceil(center + support);
        int count, k, nearest;
        double sum = 0.0;

        if (left < 0) left = 0;
        if (right > src_len - 1) right = src_len - 1;
        if (right < left) right = left;
        count = right - left + 1;

        cs[i].first = left;
        cs[i].count = count;
        cs[i].weights = (double *)malloc((size_t)count * sizeof(double));
        if (cs[i].weights == NULL) {
            free_contribs(cs, dst_len);
            return NULL;
        }

        for (k = 0; k < count; k++) {
            double w = lanczos3_kernel(((double)(left + k) - center) / filt);
            cs[i].weights[k] = w;
            sum += w;
        }

        if (sum > 0.0) {
            for (k = 0; k < count; k++) {
                cs[i].weights[k] /= sum;
            }
        } else {
            nearest = (int)(center + 0.5);
            if (nearest < left) nearest = left;
            if (nearest > right) nearest = right;
            for (k = 0; k < count; k++) cs[i].weights[k] = 0.0;
            cs[i].weights[nearest - left] = 1.0;
        }
    }

    return cs;
}

/* Lanczos3 resample of a premultiplied input; output is unpremultiplied. */
static uint8_t *lanczos_resample_pm(const uint8_t *src, int sw, int sh,
                                    int dw, int dh)
{
    resample_contrib_t *cx = NULL;
    resample_contrib_t *cy = NULL;
    float *tmp = NULL;
    uint8_t *dst = NULL;
    int x, y, c, k;

    cx = build_contribs(sw, dw);
    cy = build_contribs(sh, dh);
    if (cx == NULL || cy == NULL) {
        goto done;
    }

    /* Horizontal pass: (sw, sh) -> (dw, sh), kept as float premultiplied. */
    tmp = (float *)calloc((size_t)dw * (size_t)sh * 4u, sizeof(float));
    if (tmp == NULL) {
        goto done;
    }

    for (y = 0; y < sh; y++) {
        const uint8_t *row = src + (size_t)y * (size_t)sw * 4u;
        for (x = 0; x < dw; x++) {
            const resample_contrib_t *cc = &cx[x];
            double acc[4] = {0.0, 0.0, 0.0, 0.0};
            float *out;

            for (k = 0; k < cc->count; k++) {
                const uint8_t *p = row + (size_t)(cc->first + k) * 4u;
                double w = cc->weights[k];
                acc[0] += (double)p[0] * w;
                acc[1] += (double)p[1] * w;
                acc[2] += (double)p[2] * w;
                acc[3] += (double)p[3] * w;
            }
            out = tmp + ((size_t)y * (size_t)dw + (size_t)x) * 4u;
            for (c = 0; c < 4; c++) {
                out[c] = (float)acc[c];
            }
        }
    }

    /* Vertical pass: (dw, sh) -> (dw, dh), then unpremultiply. */
    dst = (uint8_t *)malloc((size_t)dw * (size_t)dh * 4u);
    if (dst == NULL) {
        goto done;
    }

    for (y = 0; y < dh; y++) {
        const resample_contrib_t *cc = &cy[y];
        for (x = 0; x < dw; x++) {
            double acc[4] = {0.0, 0.0, 0.0, 0.0};
            double alpha;
            uint8_t *out;

            for (k = 0; k < cc->count; k++) {
                const float *p = tmp + (size_t)(cc->first + k) * (size_t)dw * 4u
                               + (size_t)x * 4u;
                double w = cc->weights[k];
                acc[0] += (double)p[0] * w;
                acc[1] += (double)p[1] * w;
                acc[2] += (double)p[2] * w;
                acc[3] += (double)p[3] * w;
            }

            out = dst + ((size_t)y * (size_t)dw + (size_t)x) * 4u;
            alpha = acc[3];
            if (alpha <= 0.5) {
                out[0] = out[1] = out[2] = out[3] = 0;
            } else {
                for (c = 0; c < 3; c++) {
                    double v = acc[c] * 255.0 / alpha + 0.5;
                    out[c] = (uint8_t)((v > 255.0) ? 255.0 : v);
                }
                out[3] = (uint8_t)((alpha > 255.0) ? 255.0 : (alpha + 0.5));
            }
        }
    }

done:
    free(tmp);
    free_contribs(cx, dw);
    free_contribs(cy, dh);
    return dst;
}

uint8_t *bitmap_resample_rgba(const uint8_t *src, int src_w, int src_h,
                              int dst_w, int dst_h)
{
    const uint8_t *cur;
    uint8_t *owned = NULL;
    uint8_t *out;
    int cw, ch;

    if (src == NULL || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return NULL;
    }

    cur = src;
    cw = src_w;
    ch = src_h;

    /* Stage 1: area-average halving until within 2x of the target.
     * Each pass sees every source pixel, so no detail is skipped. */
    while (cw > dst_w * 2 || ch > dst_h * 2) {
        int nw = cw / 2;
        int nh = ch / 2;
        uint8_t *next;

        if (nw < dst_w) nw = dst_w;
        if (nh < dst_h) nh = dst_h;

        next = box_resample_rgba_pm(cur, cw, ch, nw, nh);
        if (next == NULL) {
            free(owned);
            return NULL;
        }
        free(owned);
        owned = next;
        cur = next;
        cw = nw;
        ch = nh;
    }

    /* Stage 2: Lanczos3 to the exact target size. */
    out = lanczos_resample_pm(cur, cw, ch, dst_w, dst_h);
    free(owned);
    return out;
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
    /* Formats keeping an alpha channel must NOT pre-blend the transparent
     * fill color: the renderer blends again by alpha, which would cause
     * visible double blending (colors washed out / too bright). */
    int keep_alpha = (format == FMT_ARGB8888 || format == FMT_ARGB8565
                      || format == FMT_ARGB4444 || format == FMT_ARGB2222);

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

        if (alpha < 255 && fill_enabled && !keep_alpha) {
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
        case FMT_ARGB8565: {
            unsigned value = (unsigned)(round_half_up(red / 255.0 * 31.0) << 11)
                           | (unsigned)(round_half_up(green / 255.0 * 63.0) << 5)
                           | (unsigned) round_half_up(blue / 255.0 * 31.0);
            out[dst++] = (uint8_t)(value & 0xFF);
            out[dst++] = (uint8_t)((value >> 8) & 0xFF);
            out[dst++] = (uint8_t)alpha;
            break;
        }
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
        /* bit replication for faithful preview brightness: 5bit 31 -> 255 */
        red   = ((((value >> 11) & 0x1F) << 3)) | ((value >> 13) & 0x07);
        green = ((((value >> 5) & 0x3F) << 2)) | ((value >> 11) & 0x03);
        blue  = (((value & 0x1F) << 3)) | ((value >> 2) & 0x07);
        break;
    }
    case FMT_RGB332: {
        unsigned value = source[offset];
        unsigned r3 = (value >> 5) & 0x07;
        unsigned g3 = (value >> 2) & 0x07;
        unsigned b2 = value & 0x03;
        red   = (uint8_t)((r3 << 5) | (r3 << 2) | (r3 >> 1));
        green = (uint8_t)((g3 << 5) | (g3 << 2) | (g3 >> 1));
        blue  = (uint8_t)((b2 << 6) | (b2 << 4) | (b2 << 2) | b2);
        break;
    }
    case FMT_ARGB8888:
        blue  = source[offset];
        green = source[offset + 1];
        red   = source[offset + 2];
        alpha = source[offset + 3];
        break;
    case FMT_ARGB8565: {
        unsigned value = (unsigned)source[offset] | ((unsigned)source[offset + 1] << 8);
        red   = ((((value >> 11) & 0x1F) << 3)) | ((value >> 13) & 0x07);
        green = ((((value >> 5) & 0x3F) << 2)) | ((value >> 11) & 0x03);
        blue  = (((value & 0x1F) << 3)) | ((value >> 2) & 0x07);
        alpha = source[offset + 2];
        break;
    }
    case FMT_ARGB4444: {
        unsigned value = (unsigned)source[offset] | ((unsigned)source[offset + 1] << 8);
        unsigned a4 = (value >> 12) & 0x0F;
        unsigned r4 = (value >> 8) & 0x0F;
        unsigned g4 = (value >> 4) & 0x0F;
        unsigned b4 = value & 0x0F;
        alpha = (uint8_t)((a4 << 4) | a4);
        red   = (uint8_t)((r4 << 4) | r4);
        green = (uint8_t)((g4 << 4) | g4);
        blue  = (uint8_t)((b4 << 4) | b4);
        break;
    }
    case FMT_ARGB2222: {
        unsigned value = source[offset];
        unsigned a2 = (value >> 6) & 0x03;
        unsigned r2 = (value >> 4) & 0x03;
        unsigned g2 = (value >> 2) & 0x03;
        unsigned b2 = value & 0x03;
        alpha = (uint8_t)((a2 << 6) | (a2 << 4) | (a2 << 2) | a2);
        red   = (uint8_t)((r2 << 6) | (r2 << 4) | (r2 << 2) | r2);
        green = (uint8_t)((g2 << 6) | (g2 << 4) | (g2 << 2) | g2);
        blue  = (uint8_t)((b2 << 6) | (b2 << 4) | (b2 << 2) | b2);
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
