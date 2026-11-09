/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t length;
    uint16_t width, height;
    bool inserted_standard_dht;
} rm_jpeg_info_t;

/* Accept a complete baseline JPEG of the required dimensions, trim trailing
 * UVC padding, and supply Annex K Huffman tables when all DHTs were omitted.
 * Existing custom DHTs are preserved; partial/missing selected tables fail. */
bool rm_jpeg_normalize(const uint8_t *source, size_t length, uint8_t *output,
                       size_t capacity, unsigned width, unsigned height,
                       rm_jpeg_info_t *info);
bool rm_jpeg_pixels_to_gray(const uint8_t *pixels, size_t length, bool rgb888,
                            uint8_t *half, uint8_t *full);
