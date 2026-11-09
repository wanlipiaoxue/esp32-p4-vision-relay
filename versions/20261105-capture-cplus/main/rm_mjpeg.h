/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cp_frame_pool.h"
typedef struct {
    bool ready;
    uint32_t prepared,prepare_errors,dht_insertions,decoded,decode_errors,last_decode_us;
    uint32_t pool_busy,pool_errors,free_slots;
} rm_mjpeg_stats_t;
bool rm_mjpeg_init(void);
const uint8_t *rm_mjpeg_prepare(const uint8_t *source,size_t length,int64_t received_us,size_t *prepared_length);
bool rm_mjpeg_copy_gray(uint8_t *half,uint8_t *full);
void rm_mjpeg_get_stats(rm_mjpeg_stats_t *snapshot);
bool rm_mjpeg_publish_current(const uint8_t *data,size_t length);
bool rm_mjpeg_acquire_latest(cp_frame_ref_t *view);
void rm_mjpeg_release_view(cp_frame_ref_t *view);
