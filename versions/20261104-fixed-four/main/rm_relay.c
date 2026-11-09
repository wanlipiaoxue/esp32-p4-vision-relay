/* SPDX-License-Identifier: Apache-2.0 */
#include "rm_relay.h"
#include "rm_relay_config.h"
#include <string.h>

/* Pre-match fixed choice. Changing the configuration requires rebuilding;
 * neither a GPIO event nor clearing a round can change this variable. */
const rm_color_t rm_four_color = RM_FOUR_COLOR;
_Static_assert(RM_FOUR_COLOR >= RM_RED && RM_FOUR_COLOR <= RM_BLUE,
               "RM_FOUR_COLOR must be RM_RED, RM_GREEN or RM_BLUE");

static int color_for_id(int id)
{
    switch (id) {
    case 510: case 514: case 519: case 530: return RM_RED;
    case 533: case 542: case 547: case 562: return RM_GREEN;
    case 568: case 573: case 579: case 582: return RM_BLUE;
    default: return -1;
    }
}

void rm_relay_init(rm_relay_t *state, bool enabled)
{
    memset(state, 0, sizeof(*state));
    state->enabled = enabled;
    state->selected = -1;
}

void rm_relay_reset(rm_relay_t *state, bool enabled)
{
    const uint32_t epoch = state->epoch + 1U;
    rm_relay_init(state, enabled);
    state->epoch = epoch;
    /* Clear discards a partially debounced or already-high old pulse.
     * Each input must be observed low for 20ms before a fresh high is eligible. */
    for (int i = 0; i < RM_INPUT_COUNT; ++i) {
        state->inputs[i].blocked = true;
    }
}

rm_lock_result_t rm_relay_lock(rm_relay_t *state, int color, int tag_id,
                               uint32_t expected_epoch, int64_t now_us)
{
    if (!state->enabled || state->epoch != expected_epoch ||
        color < 0 || color >= RM_COLOR_COUNT || color_for_id(tag_id) != color) {
        return RM_LOCK_REJECTED;
    }
    rm_record_t *record = &state->colors[color];
    if (record->state != RM_UNSEEN) {
        return RM_LOCK_DUPLICATE;
    }
    record->state = RM_PENDING;
    record->multiplier = color == (int)rm_four_color ? 4 : 2;
    record->order = ++state->registered_count;
    record->tag_id = tag_id;
    record->mode_since_us = now_us;
    return RM_LOCK_REGISTERED;
}

bool rm_relay_event(rm_relay_t *state, rm_event_t event,
                    uint32_t expected_epoch, int64_t now_us)
{
    if (!state->enabled || state->epoch != expected_epoch) {
        return false;
    }
    switch (event) {
    case RM_ENTER:
        if (state->batch_active) {
            return false;
        }
        state->batch_active = true;
        state->selected = -1;
        for (int i = 0; i < RM_COLOR_COUNT; ++i) {
            const rm_record_t *item = &state->colors[i];
            if (item->state != RM_PENDING) {
                continue;
            }
            if (state->selected < 0 ||
                item->multiplier > state->colors[state->selected].multiplier ||
                (item->multiplier == state->colors[state->selected].multiplier &&
                 item->order < state->colors[state->selected].order)) {
                state->selected = i;
            }
        }
        if (state->selected >= 0) {
            rm_record_t *item = &state->colors[state->selected];
            item->state = RM_SELECTED;
            item->mode_since_us = now_us;
        }
        return true;
    case RM_BATCH_DONE:
        if (!state->batch_active) {
            return false;
        }
        if (state->selected >= 0) {
            rm_record_t *item = &state->colors[state->selected];
            item->state = RM_USED;
            item->mode_since_us = now_us;
        }
        state->selected = -1;
        state->batch_active = false;
        return true;
    default:
        return false;
    }
}

bool rm_relay_sample(rm_relay_t *state, rm_event_t input, bool high,
                     uint32_t expected_epoch, int64_t now_us)
{
    if ((unsigned)input >= RM_INPUT_COUNT || state->epoch != expected_epoch) {
        return false;
    }
    rm_pulse_t *pulse = &state->inputs[input];
    if (high) {
        pulse->low_tracking = false;
        if (pulse->blocked) {
            return false;
        }
        if (!pulse->high_tracking) {
            pulse->high_tracking = true;
            pulse->high_since_us = now_us;
        } else if (now_us - pulse->high_since_us >= RM_DEBOUNCE_US) {
            pulse->blocked = true;
            pulse->high_tracking = false;
            return rm_relay_event(state, input, expected_epoch, now_us);
        }
    } else {
        pulse->high_tracking = false;
        if (pulse->blocked) {
            if (!pulse->low_tracking) {
                pulse->low_tracking = true;
                pulse->low_since_us = now_us;
            } else if (now_us - pulse->low_since_us >= RM_DEBOUNCE_US) {
                pulse->blocked = false;
                pulse->low_tracking = false;
            }
        }
    }
    return false;
}

bool rm_relay_output(const rm_relay_t *state, int color, int64_t now_us)
{
    if (!state->enabled || color < 0 || color >= RM_COLOR_COUNT) {
        return false;
    }
    const rm_record_t *record = &state->colors[color];
    if (record->state == RM_PENDING) {
        return true;
    }
    int64_t half_period_us;
    if (record->state == RM_SELECTED) {
        half_period_us = 100000;
    } else if (record->state == RM_USED) {
        half_period_us = 500000;
    } else {
        return false;
    }
    const int64_t elapsed = now_us >= record->mode_since_us ?
                            now_us - record->mode_since_us : 0;
    return elapsed % (half_period_us * 2) < half_period_us;
}

int rm_relay_level(bool logical_on, int active_level)
{
    return logical_on ? !!active_level : !active_level;
}
