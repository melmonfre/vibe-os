#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

#define IMAGE_MAX_TARGET_W 120
#define IMAGE_MAX_TARGET_H 80

int image_decode_to_palette(const uint8_t *data, int size,
                            uint8_t *out_pixels, int out_stride,
                            int max_w, int max_h,
                            int *out_w, int *out_h);
int image_decode_to_palette_cover(const uint8_t *data, int size,
                                  uint8_t *out_pixels, int out_stride,
                                  int target_w, int target_h,
                                  int *out_w, int *out_h);
int image_decode_node_to_palette(int node,
                                 uint8_t *out_pixels,
                                 int out_stride,
                                 int max_w,
                                 int max_h,
                                 int *out_w,
                                 int *out_h);
int image_decode_node_to_palette_cover(int node,
                                       uint8_t *out_pixels,
                                       int out_stride,
                                       int target_w,
                                       int target_h,
                                       int *out_w,
                                       int *out_h);
int image_node_is_supported(int node);

#endif
