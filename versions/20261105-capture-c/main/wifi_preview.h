#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Read-only logical lamp commands, not relay contact or voltage measurements.
 * state matches RM_UNSEEN/PENDING/SELECTED/USED (0..3). */
typedef struct {
    uint8_t state;
    uint8_t multiplier;
    bool on;
} wifi_preview_lamp_t;

typedef struct {
    wifi_preview_lamp_t channels[3];
    uint32_t epoch;
    bool enabled;
    bool batch_active;
    int selected;
    int four_color;
    int64_t sampled_us;
} wifi_preview_lights_t;

void wifi_preview_publish_lights(const wifi_preview_lights_t *snapshot);
void wifi_preview_start(void);
void wifi_preview_submit_yuy2(const uint8_t *data, size_t length, int64_t received_us);

void wifi_preview_set_capture_fps(float fps);
void wifi_preview_submit_mjpeg(const uint8_t *data, size_t length, int64_t received_us);
