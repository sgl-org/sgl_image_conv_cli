# image_conv

SGLToolKit「图片转换」功能（`src/components/ImageConvert.vue`）的 C 语言命令行实现。

## 功能

与 Web 版 `ImageConvert` 页面对齐：

- **输入格式**：PNG / JPEG / BMP（通过内置 `stb_image.h` 解码，无外部依赖）
- **颜色格式**：`RGB888`、`RGB565`、`RGB332`、`ARGB8888`、`ARGB4444`、`ARGB2222`
- **输出格式**：`.c` 源文件 或 `.bin` 二进制文件
- **压缩算法**：无压缩 / RLE / QOI（仅 RGB565，字节格式与 SGL 解码器一致）
- **透明填充**：半透明像素按 alpha 与指定颜色混合（同 Web 端逻辑）
- **分辨率覆盖**：`--width` / `--height`（使用最近邻重采样）
- **杂项**：组合为数组、交换字节、BIN 起始地址、批量单图片 BIN 生成
- **转换后预览**：`--preview` 额外输出 P6 PPM 预览图

## 构建

```sh
cd image_conv/source
make
```

Windows (MinGW) / Linux / macOS 通用，仅需 C99 编译器。

## 用法

```
image_conv <图片...> [选项]

选项:
  -f, --format <fmt>        颜色格式: RGB888|RGB565|RGB332|ARGB8888|ARGB4444|ARGB2222 (默认 RGB888)
  -o, --output <fmt>        输出格式: c|bin (默认 c)
  -c, --compression <alg>   压缩算法: none|rle|qoi (默认 none, qoi 仅 RGB565)
  -t, --transparent <#RRGGBB>
                            透明填充颜色 (默认 #FFFFFF, 使用 --no-fill 关闭)
  --no-fill                 关闭透明填充
  -n, --name <name>         输出文件名/数组名 (默认根据第一张图片自动生成)
  -d, --dir <dir>           输出文件夹 (默认当前目录)
  --start <0xADDR>          BIN 格式起始地址 (hex, 默认 0x0000)
  --width <w>               覆盖宽度 (像素)
  --height <h>              覆盖高度 (像素)
  --no-combine              不组合为数组 (bin 模式下逐图生成独立结构体地址)
  --swap-bytes              交换字节 (每 2 字节高低位互换)
  --batch-single-bin        批量单图片 BIN 生成 (仅 bin 输出有效)
  --preview                 同时输出转换后预览 (P6 PPM 文件)
  -h, --help                显示帮助
```

## 示例

```sh
# 单图转 RGB565 + RLE 压缩的 C 数组
./image_conv logo.png -f RGB565 -c rle -d out_dir

# 多图合并为一个 BIN + flash 地址表，起始地址 0x1000
./image_conv a.png b.png --out bin --name images --start 0x1000 -d flash_out

# 批量单图片 BIN（要求文件名为合法 C 标识符）
./image_conv icon_home.png icon_back.png --out bin --batch-single-bin

# RGB565 + QOI 压缩，输出预览
./image_conv photo.jpg -f RGB565 -c qoi --preview -d qoi_out
```

## 输出文件说明

| 输出格式 | 生成文件 |
| -------- | -------- |
| `c`      | `<name>.c`（位图数组 + `sgl_pixmap_t` 定义） |
| `bin`    | `<name>.bin`（所有图片数据顺序拼接） + `<name>.c`（带起始地址的 `sgl_pixmap_t` 地址表） |
| `bin` + `--batch-single-bin` | 每张图独立 `<图片名>.bin` + `<name>.c`（pixmap 声明，bitmap.addr = NULL） |

`sgl_pixmap_t` 的 `format` 字段取值与 Web 端一致，例如
`SGL_PIXMAP_FMT_RGB565`、`SGL_PIXMAP_FMT_RLE_RGB888`、`SGL_PIXMAP_FMT_QOI_RGB565`。

## 目录结构

```
image_conv/source/
├── stb_image.h   # 第三方单头图片解码库 (public domain / MIT)
├── types.h/.c    # 颜色格式与设置定义
├── util.h/.c     # 路径 / 标识符 / 颜色解析等工具
├── bitmap.h/.c   # RGBA->各格式编码、预览下采样
├── compress.h/.c # RLE 与 QOI-RGB565 压缩
├── output.h/.c   # .c 源码与 .bin 文件生成
├── main.c        # CLI 入口与转换流程
└── Makefile
```

## License

工具代码遵循仓库 LICENSE；`stb_image.h` 为 public domain / MIT 双许可，
详情见该文件头部说明。
