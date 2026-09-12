#include "compress.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* RLE                                                                */
/* ------------------------------------------------------------------ */

static int pixels_equal(const uint8_t *data, size_t a, size_t b, int bpp)
{
    int i;
    for (i = 0; i < bpp; i++) {
        if (data[a + i] != data[b + i]) {
            return 0;
        }
    }
    return 1;
}

uint8_t *rle_compress_data(const uint8_t *data, size_t data_size,
                           int bytes_per_pixel_, size_t *out_size)
{
    int bpp = bytes_per_pixel_;
    size_t output_length = 0;
    uint8_t *compressed;
    size_t offset = 0;
    size_t out_pos = 0;

    if (bpp <= 0) {
        return NULL;
    }

    /* first pass: compute exact output size */
    for (offset = 0; offset < data_size;) {
        size_t count = 1;
        while (count < 255
               && offset + (count + 1) * (size_t)bpp <= data_size
               && pixels_equal(data, offset, offset + count * (size_t)bpp, bpp)) {
            count++;
        }
        output_length += (size_t)bpp + 1;
        offset += count * (size_t)bpp;
    }

    compressed = (uint8_t *)malloc(output_length);
    if (compressed == NULL) {
        return NULL;
    }

    /* second pass: emit */
    for (offset = 0; offset < data_size;) {
        size_t count = 1;
        while (count < 255
               && offset + (count + 1) * (size_t)bpp <= data_size
               && pixels_equal(data, offset, offset + count * (size_t)bpp, bpp)) {
            count++;
        }
        compressed[out_pos++] = (uint8_t)count;
        memcpy(compressed + out_pos, data + offset, (size_t)bpp);
        out_pos += (size_t)bpp;
        offset += count * (size_t)bpp;
    }

    *out_size = output_length;
    return compressed;
}

/* ------------------------------------------------------------------ */
/* QOI-RGB565 (SGL variant)                                           */
/* ------------------------------------------------------------------ */

#define QOI_MAGIC      0x51u
#define QOI_OP_RUN     0xC0u
#define QOI_OP_DIFF    0x40u
#define QOI_OP_LUMA    0x80u
#define QOI_OP_RGB565  0xFEu
#define QOI_HDR_SIZE   13u
#define QOI_MAX_RUN    62u

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} bytebuf_t;

static int bytebuf_init(bytebuf_t *buf, size_t initial)
{
    buf->data = (uint8_t *)malloc(initial);
    buf->size = 0;
    buf->capacity = initial;
    return buf->data != NULL;
}

static int bytebuf_push(bytebuf_t *buf, uint8_t value)
{
    if (buf->size + 1 > buf->capacity) {
        size_t new_cap = buf->capacity * 2;
        uint8_t *p;
        if (new_cap == 0) {
            new_cap = 64;
        }
        p = (uint8_t *)realloc(buf->data, new_cap);
        if (p == NULL) {
            return 0;
        }
        buf->data = p;
        buf->capacity = new_cap;
    }
    buf->data[buf->size++] = value;
    return 1;
}

static void bytebuf_free(bytebuf_t *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

static void write_u16be(uint8_t *out, size_t pos, unsigned value)
{
    out[pos]     = (uint8_t)((value >> 8) & 0xFF);
    out[pos + 1] = (uint8_t)(value & 0xFF);
}

static void write_u24be(uint8_t *out, size_t pos, unsigned value)
{
    out[pos]     = (uint8_t)((value >> 16) & 0xFF);
    out[pos + 1] = (uint8_t)((value >> 8) & 0xFF);
    out[pos + 2] = (uint8_t)(value & 0xFF);
}

static void write_u32be(uint8_t *out, size_t pos, uint32_t value)
{
    out[pos]     = (uint8_t)((value >> 24) & 0xFF);
    out[pos + 1] = (uint8_t)((value >> 16) & 0xFF);
    out[pos + 2] = (uint8_t)((value >> 8) & 0xFF);
    out[pos + 3] = (uint8_t)(value & 0xFF);
}

/* 5/6/5-bit wrap-around legal DIFF deltas (-2..+1) matching the decoder
 * semantics of (prev + delta) & mask. Returns the count written to deltas. */
static int valid_qoi_deltas(int prev, int target, int mask, int deltas[4])
{
    static const int candidates[4] = { 0, -1, 1, -2 };
    int count = 0;
    int i;

    for (i = 0; i < 4; i++) {
        if (((prev + candidates[i]) & mask) == target) {
            deltas[count++] = candidates[i];
        }
    }
    return count;
}

/* Encode one row of little-endian RGB565 pixels into a QOI byte stream. */
static int encode_qoi_row(const uint8_t *data, size_t start, int width,
                          bytebuf_t *out)
{
    int pr = 0, pg = 0, pb = 0;
    int run = 0;
    int i = 0;

    while (i < width) {
        uint8_t lo = data[start + (size_t)i * 2 + 0];
        uint8_t hi = data[start + (size_t)i * 2 + 1];
        int r = (hi >> 3) & 0x1F;
        int g = ((hi & 0x07) << 3) | ((lo >> 5) & 0x07);
        int b = lo & 0x1F;

        i++;

        if (r == pr && g == pg && b == pb) {
            run++;
            continue;
        }

        /* flush run */
        while (run > 0) {
            int count = run < (int)QOI_MAX_RUN ? run : (int)QOI_MAX_RUN;
            if (!bytebuf_push(out, (uint8_t)(QOI_OP_RUN | (count - 1)))) {
                return 0;
            }
            run -= count;
        }

        {
            int dvalid_r[4], dvalid_g[4], dvalid_b[4];
            int nr = valid_qoi_deltas(pr, r, 0x1F, dvalid_r);
            int ng = valid_qoi_deltas(pg, g, 0x3F, dvalid_g);
            int nb = valid_qoi_deltas(pb, b, 0x1F, dvalid_b);
            int matched = 0;

            if (nr > 0 && ng > 0 && nb > 0) {
                int dr = dvalid_r[0], dg = dvalid_g[0], db = dvalid_b[0];
                if (!bytebuf_push(out, (uint8_t)(QOI_OP_DIFF
                        | (((dr + 2) & 0x03) << 4)
                        | (((dg + 2) & 0x03) << 2)
                        |  ((db + 2) & 0x03)))) {
                    return 0;
                }
                pr = r;
                pg = g;
                pb = b;
                matched = 1;
            }

            if (!matched) {
                int dg = (g - pg) & 0x3F;
                int dr_dg, db_dg;
                if (dg > 31) {
                    dg -= 64;
                }
                dr_dg = ((r - pr) - dg) % 32;
                if (dr_dg < 0) {
                    dr_dg += 32;
                }
                if (dr_dg > 7) {
                    dr_dg -= 32;
                }
                db_dg = ((b - pb) - dg) % 32;
                if (db_dg < 0) {
                    db_dg += 32;
                }
                if (db_dg > 7) {
                    db_dg -= 32;
                }
                if (dr_dg >= -8 && db_dg >= -8) {
                    if (!bytebuf_push(out, (uint8_t)(QOI_OP_LUMA | (dg + 32)))
                        || !bytebuf_push(out, (uint8_t)(((dr_dg + 8) << 4) | (db_dg + 8)))) {
                        return 0;
                    }
                    pr = r;
                    pg = g;
                    pb = b;
                    matched = 1;
                }

                if (!matched) {
                    if (!bytebuf_push(out, (uint8_t)QOI_OP_RGB565)
                        || !bytebuf_push(out, hi)
                        || !bytebuf_push(out, lo)) {
                        return 0;
                    }
                    pr = r;
                    pg = g;
                    pb = b;
                }
            }
        }
    }

    /* flush trailing run */
    while (run > 0) {
        int count = run < (int)QOI_MAX_RUN ? run : (int)QOI_MAX_RUN;
        if (!bytebuf_push(out, (uint8_t)(QOI_OP_RUN | (count - 1)))) {
            return 0;
        }
        run -= count;
    }

    return 1;
}

uint8_t *qoi_compress_rgb565(const uint8_t *rgb565_data,
                             int width, int height, size_t *out_size)
{
    bytebuf_t *rows;
    uint32_t *offsets;
    size_t data_size = 0;
    size_t n16 = 0, n24 = 0, n32;
    uint8_t *out;
    size_t pos;
    int y;

    if (rgb565_data == NULL || width <= 0 || height <= 0) {
        return NULL;
    }
    /* caller guarantees rgb565_data has pixel_count*2 bytes */

    rows = (bytebuf_t *)calloc((size_t)height, sizeof(bytebuf_t));
    offsets = (uint32_t *)calloc((size_t)height, sizeof(uint32_t));
    if (rows == NULL || offsets == NULL) {
        free(rows);
        free(offsets);
        return NULL;
    }

    for (y = 0; y < height; y++) {
        if (!bytebuf_init(&rows[y], 64)) {
            goto fail;
        }
        if (!encode_qoi_row(rgb565_data, (size_t)y * (size_t)width * 2u,
                            width, &rows[y])) {
            goto fail;
        }
    }

    for (y = 0; y < height; y++) {
        offsets[y] = (uint32_t)data_size;
        data_size += rows[y].size;
    }

    for (y = 0; y < height; y++) {
        if (offsets[y] < 0x10000u) {
            n16++;
        } else if (offsets[y] < 0x1000000u) {
            n24++;
        }
    }
    n32 = (size_t)height - n16 - n24;

    out = (uint8_t *)malloc(QOI_HDR_SIZE + n16 * 2 + n24 * 3 + n32 * 4 + data_size);
    if (out == NULL) {
        goto fail;
    }

    out[0] = (uint8_t)QOI_MAGIC;
    write_u16be(out, 1, (unsigned)width);
    write_u16be(out, 3, (unsigned)height);
    write_u16be(out, 7, (unsigned)(n16 * 2));
    write_u16be(out, 9, (unsigned)(n24 * 3));
    write_u16be(out, 11, (unsigned)(n32 * 4));

    pos = QOI_HDR_SIZE;
    for (y = 0; y < height; y++) {
        uint32_t offset = offsets[y];
        if ((size_t)y < n16) {
            write_u16be(out, pos, offset);
            pos += 2;
        } else if ((size_t)y < n16 + n24) {
            write_u24be(out, pos, offset);
            pos += 3;
        } else {
            write_u32be(out, pos, offset);
            pos += 4;
        }
    }

    for (y = 0; y < height; y++) {
        memcpy(out + pos, rows[y].data, rows[y].size);
        pos += rows[y].size;
    }

    for (y = 0; y < height; y++) {
        bytebuf_free(&rows[y]);
    }
    free(rows);
    free(offsets);

    *out_size = pos;
    return out;

fail:
    for (y = 0; y < height; y++) {
        bytebuf_free(&rows[y]);
    }
    free(rows);
    free(offsets);
    return NULL;
}
