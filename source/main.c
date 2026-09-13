/*
 * image_conv - Image to embedded display array converter (CLI)
 *
 * A command line re-implementation of the SGLToolKit "ImageConvert" page
 * (src/components/ImageConvert.vue + src/utils/imageConversion.js).
 *
 * Supported features:
 *   - Input formats : PNG / JPEG / BMP (via stb_image)
 *   - Color formats : RGB888, RGB565, RGB332, ARGB8888, ARGB8565, ARGB4444, ARGB2222
 *   - Output        : .c source file or .bin binary file
 *   - Compression   : none / RLE / QOI (RGB565 only)
 *   - Transparent fill (alpha blend with a fixed color)
 *   - Resolution override (--width/--height)
 *   - "combine as array", "swap bytes", BIN start address,
 *     batch single-image BIN generation
 *   - Converted-image preview written as binary PPM (P6)
 *
 * Examples:
 *   image_conv logo.png -f RGB565 -c rle -o out_dir
 *   image_conv a.png b.png --combine --name images --format bin --start 0x1000
 */

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_GIF
#define STBI_NO_TGA
#define STBI_NO_PSD
#define STBI_WINDOWS_UTF8
#include "stb_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include "types.h"
#include "util.h"
#include "bitmap.h"
#include "compress.h"
#include "output.h"

#define MAX_IMAGE_PIXELS        (16u * 1024u * 1024u)
#define MAX_TOTAL_OUTPUT_BYTES  (256u * 1024u * 1024u)
#define MAX_PREVIEW_PIXELS      (1024u * 1024u)

typedef struct {
    char path[1024];
    char name[256];       /* original file name           */
    char base_name[256];  /* stripped / safe identifier   */
    int width;
    int height;           /* 0 = auto                     */
} input_image_t;

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static void log_info(const char *fmt, ...)
{
    va_list args;
    printf("[INFO] ");
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

static void log_error(const char *fmt, ...)
{
    va_list args;
    fprintf(stderr, "[ERROR] ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static void print_usage(const char *prog)
{
    printf(
"image_conv - SGLToolKit 图片转数组命令行工具\n"
"\n"
"用法: %s <图片...> [选项]\n"
"\n"
"选项:\n"
"  -f, --format <fmt>        颜色格式: RGB888|RGB565|RGB332|ARGB8888|ARGB8565|ARGB4444|ARGB2222 (默认 RGB888)\n"
"  -O, --out <fmt>           输出格式: c|bin (默认 c)\n"
"  -c, --compression <alg>   压缩算法: none|rle|qoi (默认 none, qoi 仅 RGB565)\n"
"  -t, --transparent <#RRGGBB>\n"
"                            透明填充颜色 (默认 #FFFFFF, 使用 --no-fill 关闭)\n"
"  --no-fill                 关闭透明填充\n"
"  -n, --name <name>         输出文件名/数组名 (默认根据第一张图片自动生成)\n"
"  -d, --dir <dir>           输出文件夹 (默认当前目录)\n"
"  --start <0xADDR>          BIN 格式起始地址 (hex, 默认 0x0000)\n"
"  --width <w>               覆盖宽度 (像素)\n"
"  --height <h>              覆盖高度 (像素)\n"
"  --no-combine              不组合为数组 (bin 模式下逐图生成独立结构体地址)\n"
"  --swap-bytes              交换字节 (每 2 字节高低位互换)\n"
"  --batch-single-bin        批量单图片 BIN 生成 (仅 bin 输出有效)\n"
"  --preview                 同时输出转换后预览 (P6 PPM 文件)\n"
"  -h, --help                显示本帮助\n"
"\n"
"示例:\n"
"  %s logo.png -f RGB565 -c rle -d out_dir\n"
"  %s a.png b.png --out bin --name images --start 0x1000\n",
        prog, prog, prog);
}

static int parse_color_format(const char *value, color_format_t *out)
{
    if (strcmp(value, "RGB888") == 0)   { *out = FMT_RGB888;   return 1; }
    if (strcmp(value, "RGB565") == 0)   { *out = FMT_RGB565;   return 1; }
    if (strcmp(value, "RGB332") == 0)   { *out = FMT_RGB332;   return 1; }
    if (strcmp(value, "ARGB8888") == 0) { *out = FMT_ARGB8888; return 1; }
    if (strcmp(value, "ARGB8565") == 0) { *out = FMT_ARGB8565; return 1; }
    if (strcmp(value, "ARGB4444") == 0) { *out = FMT_ARGB4444; return 1; }
    if (strcmp(value, "ARGB2222") == 0) { *out = FMT_ARGB2222; return 1; }
    return 0;
}

static int parse_compression(const char *value, compression_t *out)
{
    if (strcmp(value, "none") == 0) { *out = COMP_NONE; return 1; }
    if (strcmp(value, "rle") == 0)  { *out = COMP_RLE;  return 1; }
    if (strcmp(value, "qoi") == 0)  { *out = COMP_QOI;  return 1; }
    return 0;
}

static int is_supported_image(const char *name)
{
    return util_ends_with_ci(name, ".png")
        || util_ends_with_ci(name, ".jpg")
        || util_ends_with_ci(name, ".jpeg")
        || util_ends_with_ci(name, ".bmp");
}

static void apply_swap_bytes(uint8_t *data, size_t size)
{
    size_t i;
    for (i = 0; i + 1 < size; i += 2) {
        uint8_t tmp = data[i];
        data[i] = data[i + 1];
        data[i + 1] = tmp;
    }
}

static void join_path(const char *dir, const char *file, char *out, size_t out_size)
{
    size_t len;
    if (dir == NULL || dir[0] == '\0') {
        snprintf(out, out_size, "%s", file);
        return;
    }
    len = strlen(dir);
    if (dir[len - 1] == '/' || dir[len - 1] == '\\') {
        snprintf(out, out_size, "%s%s", dir, file);
    } else {
        snprintf(out, out_size, "%s/%s", dir, file);
    }
}

static int ensure_dir_exists(const char *dir)
{
    if (dir == NULL || dir[0] == '\0') {
        return 0;
    }
#ifdef _WIN32
    if (CreateDirectoryA(dir, NULL)) {
        return 0;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
#else
    {
        /* single-level mkdir; parents must exist */
        return mkdir(dir, 0755) == 0 || errno == EEXIST ? 0 : -1;
    }
#endif
}

/* ------------------------------------------------------------------ */
/* conversion of a single image                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t *bitmap;     /* final (possibly compressed) data */
    size_t   bitmap_size;
    size_t   original_size;
    int      width;      /* final resolved dimensions        */
    int      height;
} convert_result_t;

static void convert_result_free(convert_result_t *r)
{
    free(r->bitmap);
    r->bitmap = NULL;
    r->bitmap_size = 0;
    r->original_size = 0;
    r->width = 0;
    r->height = 0;
}

static int convert_single(const input_image_t *img, const conv_settings_t *settings,
                          int write_preview, const char *preview_dir,
                          convert_result_t *result, char *err, size_t err_size)
{
    int w = 0, h = 0, channels = 0;
    uint8_t *rgba;
    uint8_t *encoded = NULL;
    size_t encoded_size = 0;
    int target_w;
    int target_h;

    if (img->width > 0 && img->height > 0) {
        target_w = img->width;
        target_h = img->height;
    } else {
        if (stbi_info(img->path, &w, &h, &channels) == 0) {
            snprintf(err, err_size, "无法读取图片信息: %.300s", img->path);
            return -1;
        }
        target_w = w;
        target_h = h;
    }

    if (bitmap_validate_dimensions(target_w, target_h, MAX_IMAGE_PIXELS,
                                   err, err_size) != 0) {
        return -1;
    }

    result->width = target_w;
    result->height = target_h;

    rgba = stbi_load(img->path, &w, &h, &channels, 4);
    if (rgba == NULL) {
        snprintf(err, err_size, "无法加载图片: %.200s (%.128s)",
                 img->path, stbi_failure_reason());
        return -1;
    }

    /* The web UI resizes via canvas drawImage when the user overrides
     * width/height. Here we only support native size loading: when the
     * user set explicit dimensions that differ from the file, the image
     * must be resampled. Use simple box sampling. */
    if (w != target_w || h != target_h) {
        uint8_t *resized = (uint8_t *)malloc((size_t)target_w * (size_t)target_h * 4u);
        if (resized != NULL) {
            int x, y, c;
            for (y = 0; y < target_h; y++) {
                int sy = (int)((double)y * (double)h / (double)target_h);
                if (sy > h - 1) sy = h - 1;
                for (x = 0; x < target_w; x++) {
                    int sx = (int)((double)x * (double)w / (double)target_w);
                    if (sx > w - 1) sx = w - 1;
                    for (c = 0; c < 4; c++) {
                        resized[((size_t)y * target_w + x) * 4u + c] =
                            rgba[((size_t)sy * w + sx) * 4u + c];
                    }
                }
            }
            stbi_image_free(rgba);
            rgba = resized;
            w = target_w;
            h = target_h;
        }
    }

    encoded = bitmap_encode_rgba(rgba, (size_t)w * (size_t)h, settings,
                                 &encoded_size);
    stbi_image_free(rgba);

    if (encoded == NULL) {
        snprintf(err, err_size, "像素格式编码失败: %s", img->name);
        return -1;
    }

    result->bitmap = encoded;
    result->bitmap_size = encoded_size;
    result->original_size = encoded_size;

    if (settings->compression == COMP_RLE) {
        uint8_t *compressed = rle_compress_data(encoded, encoded_size,
                                                bytes_per_pixel(settings->format),
                                                &result->bitmap_size);
        if (compressed == NULL) {
            snprintf(err, err_size, "RLE 压缩失败: %s", img->name);
            return -1;
        }
        result->bitmap = compressed;
    } else if (settings->compression == COMP_QOI) {
        uint8_t *compressed = qoi_compress_rgb565(encoded, w, h,
                                                  &result->bitmap_size);
        if (compressed == NULL) {
            snprintf(err, err_size, "QOI 压缩失败: %s", img->name);
            return -1;
        }
        result->bitmap = compressed;
    }

    if (settings->swap_bytes) {
        apply_swap_bytes(result->bitmap, result->bitmap_size);
    }

    if (write_preview) {
        char preview_name[300];
        char preview_path[1200];
        snprintf(preview_name, sizeof(preview_name), "%s_preview.ppm", img->base_name);
        join_path(preview_dir, preview_name, preview_path, sizeof(preview_path));
        if (!bitmap_write_preview_ppm(preview_path, result->bitmap,
                                      w, h, settings->format,
                                      MAX_PREVIEW_PIXELS)) {
            snprintf(err, err_size, "预览文件写入失败: %.300s", preview_path);
            return -1;
        }
        log_info("预览已生成: %s", preview_path);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* main pipeline                                                      */
/* ------------------------------------------------------------------ */

static int validate_batch_names(const input_image_t *images, int count)
{
    int i;
    int valid = 1;
    for (i = 0; i < count; i++) {
        if (!util_is_valid_identifier(images[i].base_name)) {
            log_error("图片命名不规范: %s (去掉扩展名后必须符合 C 标识符规则)",
                      images[i].name);
            valid = 0;
        }
    }
    /* duplicate check */
    for (i = 0; i < count && valid; i++) {
        int j;
        for (j = i + 1; j < count; j++) {
            if (strcmp(images[i].base_name, images[j].base_name) == 0) {
                log_error("图片名称重复: %s", images[i].base_name);
                valid = 0;
            }
        }
    }
    return valid;
}

static int run_conversion(input_image_t *images, int count,
                          conv_settings_t *settings, int write_preview)
{
    image_info_t *infos;
    int i;
    int failed = 0;
    size_t maximum_output_bytes = 0;
    int bpp = bytes_per_pixel(settings->format);
    char size_text[32];
    const char *output_dir = settings->output_dir;

    infos = (image_info_t *)calloc((size_t)count, sizeof(image_info_t));
    if (infos == NULL) {
        log_error("内存分配失败");
        return 1;
    }

    for (i = 0; i < count; i++) {
        convert_result_t result;
        char err[512];
        input_image_t *img = &images[i];

        memset(&result, 0, sizeof(result));

        log_info("正在转换: %s", img->name);

        if (convert_single(img, settings, write_preview, output_dir,
                           &result, err, sizeof(err)) != 0) {
            log_error("%s", err);
            failed = 1;
            break;
        }

        /* total output size guard, mirrors convertImages() */
        maximum_output_bytes += (size_t)img->width * (size_t)img->height
            * (size_t)bpp
            * ((settings->compression == COMP_RLE
                || settings->compression == COMP_QOI) ? (bpp + 1) : 1);
        if (maximum_output_bytes > MAX_TOTAL_OUTPUT_BYTES) {
            util_format_bytes(MAX_TOTAL_OUTPUT_BYTES, size_text, sizeof(size_text));
            log_error("转换结果最大可能超过 %s，请减少图片或分辨率后重试",
                      size_text);
            convert_result_free(&result);
            failed = 1;
            break;
        }

        strncpy(infos[i].source_file_name, img->name,
                sizeof(infos[i].source_file_name) - 1);
        strncpy(infos[i].name, img->base_name, sizeof(infos[i].name) - 1);
        snprintf(infos[i].bitmap_name, sizeof(infos[i].bitmap_name),
                 "%s_bitmap", img->base_name);
        infos[i].width = result.width;
        infos[i].height = result.height;
        infos[i].bitmap_data = result.bitmap;
        infos[i].bitmap_size = result.bitmap_size;
        infos[i].original_size = result.original_size;

        util_format_bytes(result.bitmap_size, size_text, sizeof(size_text));
        log_info("  -> %s (%u 字节)", img->base_name, (unsigned)result.bitmap_size);

        /* bitmap ownership moved into infos[i], avoid double free */
        result.bitmap = NULL;
        convert_result_free(&result);
    }

    if (!failed) {
        char c_path[1200];
        char bin_path[1200];
        char base_name[300];
        FILE *f;

        /* derive the base output name */
        if (settings->array_name[0] != '\0') {
            util_make_c_identifier(settings->array_name, base_name, sizeof(base_name));
        } else if (count == 1) {
            snprintf(base_name, sizeof(base_name), "%.250s", infos[0].name);
        } else {
            snprintf(base_name, sizeof(base_name), "combined_results");
        }

        if (settings->output_format == OUT_C) {
            const char *c_name =
                (settings->array_name[0] != '\0') ? base_name : "combined_results";
            join_path(output_dir, c_name, c_path, sizeof(c_path));
            strcat(c_path, ".c");
            f = util_fopen(c_path, "wb");
            if (f == NULL) {
                log_error("无法创建文件: %s (%s)", c_path, strerror(errno));
                failed = 1;
            } else {
                if (output_write_c_code(f, infos, count, settings) != 0) {
                    failed = 1;
                }
                fclose(f);
                if (!failed) {
                    log_info("C 文件已保存: %s", c_path);
                }
            }
        } else if (settings->batch_single_bin) {
            for (i = 0; i < count; i++) {
                char bin_name[300];
                snprintf(bin_name, sizeof(bin_name), "%s.bin", infos[i].name);
                join_path(output_dir, bin_name, bin_path, sizeof(bin_path));
                if (output_write_binary_file(bin_path, infos[i].bitmap_data,
                                             infos[i].bitmap_size) != 0) {
                    log_error("BIN 文件写入失败: %s", bin_path);
                    failed = 1;
                } else {
                    log_info("BIN 文件已保存: %s (%u 字节)", bin_path,
                             (unsigned)infos[i].bitmap_size);
                }
            }
            if (!failed) {
                const char *decl_name = (settings->array_name[0] != '\0')
                    ? base_name : "images";
                join_path(output_dir, decl_name, c_path, sizeof(c_path));
                strcat(c_path, ".c");
                f = util_fopen(c_path, "wb");
                if (f == NULL) {
                    log_error("无法创建文件: %s (%s)", c_path, strerror(errno));
                    failed = 1;
                } else {
                    if (output_write_batch_single_bin_code(f, infos, count,
                                                           settings) != 0) {
                        failed = 1;
                    }
                    fclose(f);
                    if (!failed) {
                        log_info("结构体声明文件已保存: %s", c_path);
                    }
                }
            }
        } else {
            const char *bin_name =
                (settings->array_name[0] != '\0') ? base_name : "flash_image";
            uint8_t *bin_data = NULL;
            size_t bin_size = 0;

            join_path(output_dir, bin_name, bin_path, sizeof(bin_path));
            strcat(bin_path, ".bin");
            bin_data = output_build_bin(infos, count, &bin_size);
            if (bin_data == NULL) {
                log_error("BIN 数据构建失败");
                failed = 1;
            } else if (output_write_binary_file(bin_path, bin_data, bin_size) != 0) {
                log_error("BIN 文件写入失败: %s", bin_path);
                failed = 1;
            } else {
                log_info("BIN 文件已保存: %s (%u 字节)", bin_path,
                         (unsigned)bin_size);
            }
            free(bin_data);

            if (!failed) {
                join_path(output_dir, bin_name, c_path, sizeof(c_path));
                strcat(c_path, ".c");
                f = util_fopen(c_path, "wb");
                if (f == NULL) {
                    log_error("无法创建文件: %s (%s)", c_path, strerror(errno));
                    failed = 1;
                } else {
                    if (output_write_bin_code(f, infos, count, settings) != 0) {
                        failed = 1;
                    }
                    fclose(f);
                    if (!failed) {
                        log_info("地址表文件已保存: %s", c_path);
                    }
                }
            }
        }
    }

    for (i = 0; i < count; i++) {
        free(infos[i].bitmap_data);
    }
    free(infos);

    if (!failed) {
        log_info("转换成功！");
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* argument parsing & entry                                           */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    input_image_t images[512];
    int image_count = 0;
    conv_settings_t settings;
    int write_preview = 0;
    int override_width = 0;
    int override_height = 0;
    int i;
    int result;
    char color_hex[16] = "#FFFFFF";

#ifdef _WIN32
    /* enable UTF-8 output + arguments on Windows console */
    SetConsoleOutputCP(CP_UTF8);
    {
        LPWSTR *wargv;
        int wargc;
        wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        if (wargv != NULL && wargc == argc) {
            for (i = 0; i < argc; i++) {
                int need = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                               NULL, 0, NULL, NULL);
                if (need > 0) {
                    char *converted = (char *)malloc((size_t)need);
                    if (converted != NULL) {
                        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                            converted, need, NULL, NULL);
                        argv[i] = converted; /* leaks by design: lives for program lifetime */
                    }
                }
            }
            argv[argc] = NULL;
        }
    }
#endif

    memset(images, 0, sizeof(images));
    memset(&settings, 0, sizeof(settings));
    settings.format = FMT_RGB888;
    settings.output_format = OUT_C;
    settings.compression = COMP_NONE;
    settings.enable_transparent_fill = 1;
    settings.transparent_fill_r = 0xFF;
    settings.transparent_fill_g = 0xFF;
    settings.transparent_fill_b = 0xFF;
    settings.bin_start_address = 0;
    settings.combine_as_array = 1;
    settings.output_dir[0] = '\0';

    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }

    /* collect positional image args and options */
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--format") == 0) {
            if (++i >= argc) {
                log_error("%s 需要一个参数", arg);
                return 1;
            }
            if (!parse_color_format(argv[i], &settings.format)) {
                log_error("不支持的颜色格式: %s", argv[i]);
                return 1;
            }
        } else if (strcmp(arg, "-O") == 0 || strcmp(arg, "--out") == 0) {
            if (++i >= argc) {
                log_error("%s 需要一个参数", arg);
                return 1;
            }
            if (strcmp(argv[i], "c") == 0) {
                settings.output_format = OUT_C;
            } else if (strcmp(argv[i], "bin") == 0) {
                settings.output_format = OUT_BIN;
            } else {
                log_error("不支持的输出格式: %s", argv[i]);
                return 1;
            }
        } else if (strcmp(arg, "-c") == 0 || strcmp(arg, "--compression") == 0) {
            if (++i >= argc) {
                log_error("%s 需要一个参数", arg);
                return 1;
            }
            if (!parse_compression(argv[i], &settings.compression)) {
                log_error("不支持的压缩算法: %s", argv[i]);
                return 1;
            }
        } else if (strcmp(arg, "-t") == 0 || strcmp(arg, "--transparent") == 0) {
            if (++i >= argc) {
                log_error("%s 需要一个参数", arg);
                return 1;
            }
            snprintf(color_hex, sizeof(color_hex), "%s", argv[i]);
            settings.enable_transparent_fill = 1;
        } else if (strcmp(arg, "--no-fill") == 0) {
            settings.enable_transparent_fill = 0;
        } else if (strcmp(arg, "-n") == 0 || strcmp(arg, "--name") == 0) {
            if (++i >= argc) {
                log_error("%s 需要一个参数", arg);
                return 1;
            }
            snprintf(settings.array_name, sizeof(settings.array_name),
                     "%s", argv[i]);
        } else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--dir") == 0) {
            if (++i >= argc) {
                log_error("%s 需要一个参数", arg);
                return 1;
            }
            snprintf(settings.output_dir, sizeof(settings.output_dir),
                     "%s", argv[i]);
        } else if (strcmp(arg, "--start") == 0) {
            if (++i >= argc) {
                log_error("%s 需要一个参数", arg);
                return 1;
            }
            if (!util_parse_hex_address(argv[i], &settings.bin_start_address)) {
                log_error("无效的起始地址: %s", argv[i]);
                return 1;
            }
        } else if (strcmp(arg, "--width") == 0) {
            if (++i >= argc) {
                log_error("%s 需要一个参数", arg);
                return 1;
            }
            override_width = atoi(argv[i]);
        } else if (strcmp(arg, "--height") == 0) {
            if (++i >= argc) {
                log_error("%s 需要一个参数", arg);
                return 1;
            }
            override_height = atoi(argv[i]);
        } else if (strcmp(arg, "--no-combine") == 0) {
            settings.combine_as_array = 0;
        } else if (strcmp(arg, "--swap-bytes") == 0) {
            settings.swap_bytes = 1;
        } else if (strcmp(arg, "--batch-single-bin") == 0) {
            settings.batch_single_bin = 1;
        } else if (strcmp(arg, "--preview") == 0) {
            write_preview = 1;
        } else if (arg[0] == '-' && arg[1] != '\0') {
            log_error("未知选项: %s", arg);
            return 1;
        } else {
            input_image_t *img;

            if (image_count >= (int)(sizeof(images) / sizeof(images[0]))) {
                log_error("图片数量超出限制");
                return 1;
            }
            if (!is_supported_image(arg)) {
                log_error("不支持的图片类型: %s (仅支持 png/jpg/jpeg/bmp)", arg);
                return 1;
            }
            img = &images[image_count++];
            snprintf(img->path, sizeof(img->path), "%s", arg);
            util_basename(arg, img->name, sizeof(img->name));
            {
                char stripped[256];
                util_strip_extension(img->name, stripped, sizeof(stripped));
                util_make_c_identifier(stripped, img->base_name,
                                       sizeof(img->base_name));
            }
            img->width = 0;
            img->height = 0;
        }
    }

    if (image_count == 0) {
        log_error("未指定任何图片文件");
        print_usage(argv[0]);
        return 1;
    }

    /* QOI compression only supports RGB565 (mirrors the web UI) */
    if (settings.compression == COMP_QOI && settings.format != FMT_RGB565) {
        log_error("QOI压缩仅支持RGB565颜色格式");
        return 1;
    }

    /* batch single bin only valid for bin output */
    if (settings.batch_single_bin && settings.output_format != OUT_BIN) {
        settings.batch_single_bin = 0;
    }
    if (settings.batch_single_bin) {
        settings.combine_as_array = 0;
        if (!validate_batch_names(images, image_count)) {
            return 1;
        }
    }

    if (settings.enable_transparent_fill) {
        int r, g, b;
        if (!util_parse_color(color_hex, &r, &g, &b)) {
            log_error("无效的透明填充颜色: %s (应为 #RRGGBB)", color_hex);
            return 1;
        }
        settings.transparent_fill_r = r;
        settings.transparent_fill_g = g;
        settings.transparent_fill_b = b;
    }

    /* width/height validation for overrides */
    if (override_width < 0 || override_height < 0) {
        log_error("--width/--height 必须为正整数");
        return 1;
    }

    /* apply global dimension overrides to every image (auto-size when 0) */
    if (override_width > 0 || override_height > 0) {
        int def_w = override_width > 0 ? override_width : 0;
        int def_h = override_height > 0 ? override_height : 0;
        for (i = 0; i < image_count; i++) {
            if (images[i].width == 0 && images[i].height == 0) {
                images[i].width = def_w;
                images[i].height = def_h;
            }
        }
        /* if only one dimension is given, read the other from the file */
        if (def_w == 0 || def_h == 0) {
            for (i = 0; i < image_count; i++) {
                int sw, sh, sc;
                if (stbi_info(images[i].path, &sw, &sh, &sc)) {
                    if (images[i].width == 0) {
                        images[i].width = sw;
                    }
                    if (images[i].height == 0) {
                        images[i].height = sh;
                    }
                }
            }
        }
    }

    if (settings.output_dir[0] != '\0') {
        if (ensure_dir_exists(settings.output_dir) != 0) {
            log_error("无法创建输出文件夹: %s", settings.output_dir);
            return 1;
        }
    }

    /* default output name from the first image, mirrors the web UI */
    if (settings.array_name[0] == '\0' && image_count > 0) {
        char stripped[256];
        util_strip_extension(images[0].name, stripped, sizeof(stripped));
        util_make_c_identifier(stripped, stripped, sizeof(stripped));
        snprintf(settings.array_name, sizeof(settings.array_name),
                 "img_%.120s", stripped);
    }

    result = run_conversion(images, image_count, &settings, write_preview);

    for (i = 0; i < image_count; i++) {
        /* nothing to free here */
    }

    return result;
}
