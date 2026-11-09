/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RM_COLOR_COUNT 3
#define RM_INPUT_COUNT 3
#define RM_DEBOUNCE_US 20000LL

typedef enum { RM_RED, RM_GREEN, RM_BLUE } rm_color_t;
typedef enum { RM_UNSEEN, RM_PENDING, RM_SELECTED, RM_USED } rm_record_state_t;
typedef enum { RM_ENTER, RM_BATCH_DONE, RM_NEXT_FOUR } rm_event_t;
typedef enum { RM_LOCK_REJECTED, RM_LOCK_DUPLICATE, RM_LOCK_REGISTERED } rm_lock_result_t;

typedef struct {
    rm_record_state_t state;
    uint8_t multiplier;
    uint8_t order;
    int tag_id;
    int64_t mode_since_us;
} rm_record_t;

typedef struct {
    bool high_tracking;
    bool low_tracking;
    bool blocked;
    int64_t high_since_us;
    int64_t low_since_us;
} rm_pulse_t;

typedef struct {
    rm_record_t colors[RM_COLOR_COUNT];
    rm_pulse_t inputs[RM_INPUT_COUNT];
    uint32_t epoch;
    bool enabled;
    bool next_four;
    bool batch_active;  /* Also true for an empty ENTER, until DONE. */
    int selected;       /* -1 for no selected color. */
    uint8_t registered_count;
} rm_relay_t;

/* Caller serializes every access, including GPIO writes, with tag_action_mutex.
 * now_us is a nonnegative, monotonic esp_timer timestamp (host tests inject it).
 * No function sleeps, allocates, logs, or accesses hardware. */
void rm_relay_init(rm_relay_t *state, bool enabled);
void rm_relay_reset(rm_relay_t *state, bool enabled);
rm_lock_result_t rm_relay_lock(rm_relay_t *state, int color, int tag_id,
                               uint32_t expected_epoch, int64_t now_us);
bool rm_relay_event(rm_relay_t *state, rm_event_t event,
                    uint32_t expected_epoch, int64_t now_us);
bool rm_relay_sample(rm_relay_t *state, rm_event_t input, bool high,
                     uint32_t expected_epoch, int64_t now_us);
bool rm_relay_output(const rm_relay_t *state, int color, int64_t now_us);
int rm_relay_level(bool logical_on, int active_level);
