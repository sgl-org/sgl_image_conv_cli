# image_conv - Image to array converter CLI
#
# Build:  make
# Run:    ./image_conv --help
#
# The tool uses the bundled single-header stb_image.h (public domain /
# MIT) for PNG / JPEG / BMP decoding; no external libraries are needed.

ifeq ($(origin CC),default)
CC = gcc
endif

CFLAGS  = -O2 -Wall -std=c99
LDFLAGS = -lm
TARGET  = sgl_image_conv

SRCS = main.c types.c util.c bitmap.c compress.c output.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
