/* SPDX-License-Identifier: Apache-2.0
 * BT.601 conversion adapted from Espressif's Apache-2.0 usb_host_uvc 2.6.0
 * examples/camera_display/main/yuy2.c (Espressif Systems, 2024).
 * Retains 8-bit channels and writes the JPEG driver's BGR24 byte order. */
#include "preview_color.h"

static uint8_t color_component(int scaled)
{
    if (scaled <= 0) return 0;
    if (scaled >= 255 * 256) return 255;
    return (uint8_t)(scaled / 256);
}

bool preview_color_convert(const uint8_t *yuy2, size_t length,
                           uint8_t *bgr888, size_t capacity)
{
    if (!yuy2 || !bgr888 || length != PREVIEW_COLOR_INPUT_SIZE ||
        capacity < PREVIEW_COLOR_RAW_SIZE) return false;
    for (size_t y = 0; y < PREVIEW_COLOR_HEIGHT; ++y) {
        const uint8_t *row = yuy2 + y * 2U * PREVIEW_COLOR_INPUT_WIDTH * 2U;
        uint8_t *out = bgr888 + y * PREVIEW_COLOR_WIDTH * 3U;
        for (size_t x = 0; x < PREVIEW_COLOR_WIDTH; ++x) {
            const int c = (int)row[x * 4U] - 16;
            const int d = (int)row[x * 4U + 1U] - 128;
            const int e = (int)row[x * 4U + 3U] - 128;
            out[x * 3U] = color_component(298 * c + 516 * d + 128);
            out[x * 3U + 1U] = color_component(298 * c - 100 * d - 208 * e + 128);
            out[x * 3U + 2U] = color_component(298 * c + 409 * e + 128);
        }
    }
    return true;
}
