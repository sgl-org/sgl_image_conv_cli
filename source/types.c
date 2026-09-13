#include "types.h"

#include <stdio.h>
#include <string.h>

int bytes_per_pixel(color_format_t format)
{
    switch (format) {
    case FMT_RGB888:   return 3;
    case FMT_RGB565:   return 2;
    case FMT_RGB332:   return 1;
    case FMT_ARGB8888: return 4;
    case FMT_ARGB8565: return 3;
    case FMT_ARGB4444: return 2;
    case FMT_ARGB2222: return 1;
    default:           return 0;
    }
}

void get_sgl_format(color_format_t format, compression_t compression,
                    char *buf, size_t buf_size)
{
    const char *prefix;
    const char *base;

    switch (compression) {
    case COMP_RLE: prefix = "RLE_"; break;
    case COMP_QOI: prefix = "QOI_"; break;
    default:       prefix = "";     break;
    }

    switch (format) {
    case FMT_RGB888:   base = "RGB888";   break;
    case FMT_RGB565:   base = "RGB565";   break;
    case FMT_RGB332:   base = "RGB332";   break;
    case FMT_ARGB8888: base = "ARGB8888"; break;
    case FMT_ARGB8565: base = "ARGB8565"; break;
    case FMT_ARGB4444: base = "ARGB4444"; break;
    case FMT_ARGB2222: base = "ARGB2222"; break;
    default:           base = "RGB888";   break;
    }

    snprintf(buf, buf_size, "SGL_PIXMAP_FMT_%s%s", prefix, base);
}
