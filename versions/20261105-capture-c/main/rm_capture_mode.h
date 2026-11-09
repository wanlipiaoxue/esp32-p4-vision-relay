/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* UVC frame intervals are in 100 ns units. Only advertised values are selected. */
typedef struct {
    uint32_t default_interval;
    bool continuous;
    uint32_t minimum, maximum, step;
    const uint32_t *intervals;
    size_t count;
} rm_capture_intervals_t;

bool rm_capture_choose_interval(const rm_capture_intervals_t *caps, unsigned target_fps,
                                uint32_t *selected_interval, bool *above_target);
/* Phase-preserving pacing; avoid halving nominal 30/60 FPS due to rounding. */
bool rm_capture_take_preview_slot(int64_t now_us, int64_t *next_us, unsigned limit_fps);

