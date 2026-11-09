/* SPDX-License-Identifier: Apache-2.0 */
#include "rm_capture_mode.h"
#include <limits.h>

bool rm_capture_take_preview_slot(int64_t now_us, int64_t *next_us, unsigned limit_fps)
{
    if (!next_us || now_us < 0 || !limit_fps || limit_fps > 1000000U || now_us < *next_us)
        return false;
    const int64_t period = 1000000U / limit_fps;
    *next_us = *next_us > 0 && now_us - *next_us < period ? *next_us + period : now_us + period;
    return true;
}

bool rm_capture_choose_interval(const rm_capture_intervals_t *caps, unsigned target_fps,
                                uint32_t *selected_interval, bool *above_target)
{
    if (!caps || !selected_interval || !above_target || !target_fps ||
        target_fps > 10000000U) return false;
    /* UVC's rounded 333333 interval is the usual nominal 30 FPS mode. */
    const uint32_t wanted = 10000000U / target_fps;
    uint32_t best = 0;
    if (caps->continuous) {
        if (!caps->minimum || caps->maximum < caps->minimum) return false;
        uint64_t candidate = wanted > caps->minimum ? wanted : caps->minimum;
        if (caps->step && candidate > caps->minimum) {
            const uint64_t delta = candidate - caps->minimum;
            candidate = caps->minimum + ((delta + caps->step - 1) / caps->step) * caps->step;
        }
        if (candidate <= caps->maximum && candidate <= UINT32_MAX) best = (uint32_t)candidate;
    } else {
        if (caps->count && !caps->intervals) return false;
        for (size_t i = 0; i < caps->count; ++i) {
            const uint32_t value = caps->intervals[i];
            if (value >= wanted && (!best || value < best)) best = value;
        }
        if (caps->default_interval >= wanted &&
            (!best || caps->default_interval < best)) best = caps->default_interval;
    }
    if (!best) {
        /* A device may only expose 60 FPS MJPEG. Use its valid default rather
         * than requesting an invented 30 FPS; preview is separately rate limited. */
        best = caps->default_interval;
        if (caps->continuous && (best < caps->minimum || best > caps->maximum ||
            (caps->step && (best - caps->minimum) % caps->step))) best = 0;
        if (!best && !caps->continuous) {
            for (size_t i = 0; i < caps->count; ++i)
                if (caps->intervals[i] > best) best = caps->intervals[i];
        }
        if (!best && caps->continuous) best = caps->minimum;
    }
    if (!best) return false;
    *selected_interval = best;
    *above_target = best < wanted;
    return true;
}
