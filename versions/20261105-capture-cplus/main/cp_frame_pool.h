/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CP_POOL_SLOTS 3
#define CP_PACKET_HEADER 32U
typedef struct {
    uint8_t *storage;
    size_t capacity, length;
    uint32_t references, generation, sequence;
    int64_t received_us;
    bool writing;
} cp_slot_t;
typedef struct {
    cp_slot_t slots[CP_POOL_SLOTS];
    int latest;
    uint32_t next_sequence;
} cp_pool_t;
typedef struct {
    int slot;
    uint32_t generation, sequence;
    uint8_t *storage;
    size_t length;
    int64_t received_us;
    bool active;
} cp_frame_ref_t;
/* Caller serializes metadata operations; payload writes/reads occur outside
 * that lock while the writer/reader owns a reference. Capacity excludes header. */
void cp_pool_init(cp_pool_t *pool, uint8_t *storage[CP_POOL_SLOTS], size_t capacity);
int cp_pool_reserve(cp_pool_t *pool);
bool cp_pool_commit(cp_pool_t *pool, int slot, size_t length, int64_t received_us);
bool cp_pool_publish(cp_pool_t *pool, int slot);
bool cp_pool_release(cp_pool_t *pool, int slot, uint32_t generation);
bool cp_pool_acquire_latest(cp_pool_t *pool, cp_frame_ref_t *view);
bool cp_pool_release_view(cp_pool_t *pool, cp_frame_ref_t *view);
unsigned cp_pool_free_slots(const cp_pool_t *pool);
