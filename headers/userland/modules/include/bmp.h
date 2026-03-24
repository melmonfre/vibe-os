#ifndef BMP_H
#define BMP_H

#include <stdint.h>
#include <userland/modules/include/image.h>

#define BMP_MAX_TARGET_W IMAGE_MAX_TARGET_W
#define BMP_MAX_TARGET_H IMAGE_MAX_TARGET_H

void bmp_palette_color(uint8_t index, uint8_t *r, uint8_t *g, uint8_t *b);
int bmp_decode_to_palette(const uint8_t *data, int size,
                          uint8_t *out_pixels, int out_stride,
                          int max_w, int max_h,
                          int *out_w, int *out_h);
int bmp_encode_8bit(const uint8_t *pixels, int width, int height, int stride,
                    uint8_t *out, int out_cap);

#endif
