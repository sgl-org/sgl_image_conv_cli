#include "output.h"
#include "util.h"
#include "compress.h"

#include <stdlib.h>
#include <string.h>

/* Format helper: "0x%08X" style 8-digit hex address */
static void format_address(uint32_t address, char *out, size_t out_size)
{
    snprintf(out, out_size, "0x%08X", (unsigned)address);
}

static void emit_bitmap_array(FILE *f, const image_info_t *info)
{
    size_t offset;

    for (offset = 0; offset < info->bitmap_size; offset += 24) {
        size_t count = info->bitmap_size - offset;
        size_t i;
        int is_last_line;

        if (count > 24) {
            count = 24;
        }
        is_last_line = (offset + count >= info->bitmap_size);

        fprintf(f, "    ");
        for (i = 0; i < count; i++) {
            fprintf(f, "0x%02X%s", info->bitmap_data[offset + i],
                    (i + 1 < count) ? ", " : (is_last_line ? "" : ","));
        }
        fprintf(f, "\n");
    }
}

int output_write_c_code(FILE *f, const image_info_t *images, int count,
                        const conv_settings_t *settings)
{
    int idx;

    fprintf(f, "#include <stdint.h>\n#include <sgl_core.h>\n\n");

    for (idx = 0; idx < count; idx++) {
        const image_info_t *info = &images[idx];
        const char *comment = NULL;

        if (settings->compression == COMP_RLE) {
            comment = "// RLE压缩数据\n";
        } else if (settings->compression == COMP_QOI) {
            comment = "// QOI压缩数据\n";
        }
        if (comment != NULL) {
            fprintf(f, "%s", comment);
        }

        fprintf(f, "static const uint8_t %s[%u] = {\n",
                info->bitmap_name, (unsigned)info->bitmap_size);
        emit_bitmap_array(f, info);
        fprintf(f, "};\n\n");
    }

    if (settings->combine_as_array && count > 1) {
        const char *array_name = settings->array_name[0] != '\0'
            ? settings->array_name : "combined_images";
        char fmt[64];

        get_sgl_format(settings->format, settings->compression, fmt, sizeof(fmt));
        fprintf(f, "const sgl_pixmap_t %s[%d] = {\n", array_name, count);
        for (idx = 0; idx < count; idx++) {
            fprintf(f, "    {\n");
            fprintf(f, "        .width = %d,\n", images[idx].width);
            fprintf(f, "        .height = %d,\n", images[idx].height);
            fprintf(f, "        .bitmap.array = %s,\n", images[idx].bitmap_name);
            fprintf(f, "        .format = %s,\n", fmt);
            fprintf(f, "    },\n");
        }
        fprintf(f, "};\n");
        return 0;
    }

    for (idx = 0; idx < count; idx++) {
        const image_info_t *info = &images[idx];
        const char *pixmap_name;
        char fmt[64];

        get_sgl_format(settings->format, settings->compression, fmt, sizeof(fmt));

        /* JS: combineAsArray ? arrayName||"<name>_image" : "<name>_image"
         * In non-combine mode each image gets "<name>_image";
         * in combine-single mode (<=1 image) it uses arrayName for it. */
        if (settings->combine_as_array) {
            pixmap_name = settings->array_name[0] != '\0'
                ? settings->array_name : info->name;
        } else {
            pixmap_name = info->name;
        }

        fprintf(f, "const sgl_pixmap_t %s_image = {\n", pixmap_name);
        fprintf(f, "    .width = %d,\n", info->width);
        fprintf(f, "    .height = %d,\n", info->height);
        fprintf(f, "    .bitmap.array = %s,\n", info->bitmap_name);
        fprintf(f, "    .format = %s,\n", fmt);
        fprintf(f, "};\n\n");
    }

    return 0;
}

int output_write_bin_code(FILE *f, const image_info_t *images, int count,
                          const conv_settings_t *settings)
{
    const char *array_name = settings->array_name[0] != '\0'
        ? settings->array_name : "flash_image";
    uint32_t current_address = settings->bin_start_address;
    char addr[16];
    char fmt[64];
    int idx;

    get_sgl_format(settings->format, settings->compression, fmt, sizeof(fmt));

    fprintf(f, "#include <stdint.h>\n#include <sgl_core.h>\n\n");

    for (idx = 0; idx < count; idx++) {
        const image_info_t *info = &images[idx];

        if (!settings->combine_as_array) {
            format_address(current_address, addr, sizeof(addr));
            fprintf(f, "const sgl_pixmap_t %s_image = {\n", info->name);
            fprintf(f, "    .width = %d,\n", info->width);
            fprintf(f, "    .height = %d,\n", info->height);
            fprintf(f, "    .bitmap.addr = %s,\n", addr);
            fprintf(f, "    .format = %s,\n", fmt);
            fprintf(f, "};\n\n");
        }

        current_address += (uint32_t)info->bitmap_size;
    }

    if (settings->combine_as_array) {
        fprintf(f, "const sgl_pixmap_t %s[%d] = {\n", array_name, count);
        current_address = settings->bin_start_address;
        for (idx = 0; idx < count; idx++) {
            format_address(current_address, addr, sizeof(addr));
            fprintf(f, "    {\n");
            fprintf(f, "        .width = %d,\n", images[idx].width);
            fprintf(f, "        .height = %d,\n", images[idx].height);
            fprintf(f, "        .bitmap.addr = %s,\n", addr);
            fprintf(f, "        .format = %s,\n", fmt);
            fprintf(f, "    },\n");
            current_address += (uint32_t)images[idx].bitmap_size;
        }
        fprintf(f, "};\n");
    }

    return 0;
}

int output_write_batch_single_bin_code(FILE *f, const image_info_t *images,
                                       int count,
                                       const conv_settings_t *settings)
{
    const char *array_name = settings->array_name[0] != '\0'
        ? settings->array_name : "images";
    char fmt[64];
    int idx;

    get_sgl_format(settings->format, settings->compression, fmt, sizeof(fmt));

    fprintf(f, "#include <stdint.h>\n#include <stddef.h>\n#include <sgl_core.h>\n\n");
    fprintf(f, "// %s - Generated by Image To Array Tool\n\n", array_name);
    fprintf(f, "// 图片数据结构体定义\n");

    for (idx = 0; idx < count; idx++) {
        const image_info_t *info = &images[idx];
        fprintf(f, "// %s -> %s.bin\n", info->source_file_name, info->name);
        fprintf(f, "const sgl_pixmap_t %s = {\n", info->name);
        fprintf(f, "    .width = %d,\n", info->width);
        fprintf(f, "    .height = %d,\n", info->height);
        fprintf(f, "    .bitmap.addr = NULL,\n");
        fprintf(f, "    .format = %s,\n", fmt);
        fprintf(f, "};\n\n");
    }

    return 0;
}

uint8_t *output_build_bin(const image_info_t *images, int count,
                          size_t *out_size)
{
    size_t total = 0;
    uint8_t *buffer;
    size_t pos = 0;
    int idx;

    for (idx = 0; idx < count; idx++) {
        total += images[idx].bitmap_size;
    }

    buffer = (uint8_t *)malloc(total);
    if (buffer == NULL) {
        return NULL;
    }

    for (idx = 0; idx < count; idx++) {
        memcpy(buffer + pos, images[idx].bitmap_data, images[idx].bitmap_size);
        pos += images[idx].bitmap_size;
    }

    *out_size = total;
    return buffer;
}

int output_write_binary_file(const char *path, const uint8_t *data,
                             size_t size)
{
    FILE *f = util_fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    if (size > 0 && fwrite(data, 1, size, f) != size) {
        fclose(f);
        return -2;
    }
    fclose(f);
    return 0;
}
