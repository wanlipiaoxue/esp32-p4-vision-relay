/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool ready;
    uint32_t prepared, prepare_errors, dht_insertions, decoded, decode_errors, last_decode_us;
} rm_mjpeg_stats_t;
bool rm_mjpeg_init(void);
const uint8_t *rm_mjpeg_prepare(const uint8_t *source, size_t length, size_t *prepared_length);
bool rm_mjpeg_copy_gray(uint8_t *half, uint8_t *full);
void rm_mjpeg_get_stats(rm_mjpeg_stats_t *snapshot);
