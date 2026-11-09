/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PREVIEW_COLOR_INPUT_WIDTH 640U
#define PREVIEW_COLOR_INPUT_HEIGHT 480U
#define PREVIEW_COLOR_INPUT_SIZE (PREVIEW_COLOR_INPUT_WIDTH * PREVIEW_COLOR_INPUT_HEIGHT * 2U)
#define PREVIEW_COLOR_WIDTH 320U
#define PREVIEW_COLOR_HEIGHT 240U
#define PREVIEW_COLOR_RAW_SIZE (PREVIEW_COLOR_WIDTH * PREVIEW_COLOR_HEIGHT * 3U)

/* Separate buffers. Fixed 2x decimation keeps the original preview sample sites.
 * ESP-IDF 5.5.4 JPEG_ENCODE_IN_FORMAT_RGB888 consumes B,G,R bytes, not R,G,B.
 * Uses BT.601 limited-range YCbCr, matching the installed official UVC example. */
bool preview_color_convert(const uint8_t *yuy2, size_t length,
                           uint8_t *bgr888, size_t capacity);
