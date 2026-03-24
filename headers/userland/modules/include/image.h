#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

#define IMAGE_MAX_TARGET_W 80
#define IMAGE_MAX_TARGET_H 60

int image_decode_to_palette(const uint8_t *data, int size,
                            uint8_t *out_pixels, int out_stride,
                            int max_w, int max_h,
                            int *out_w, int *out_h);
int image_decode_node_to_palette(int node,
                                 uint8_t *out_pixels,
                                 int out_stride,
                                 int max_w,
                                 int max_h,
                                 int *out_w,
                                 int *out_h);
int image_node_is_supported(int node);

#endif
