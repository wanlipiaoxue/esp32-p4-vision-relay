/* SPDX-License-Identifier: Apache-2.0 */
#include "cp_frame_pool.h"
#include <limits.h>
#include <string.h>
static bool valid(int slot) { return slot >= 0 && slot < CP_POOL_SLOTS; }
void cp_pool_init(cp_pool_t *pool, uint8_t *storage[CP_POOL_SLOTS], size_t capacity)
{
    memset(pool, 0, sizeof(*pool)); pool->latest = -1;
    for (int i=0;i<CP_POOL_SLOTS;++i) { pool->slots[i].storage=storage[i];pool->slots[i].capacity=capacity; }
}
int cp_pool_reserve(cp_pool_t *pool)
{
    for (int i=0;i<CP_POOL_SLOTS;++i) {
        cp_slot_t *s=&pool->slots[i];
        if (!s->references && s->storage) {
            s->references=1;s->writing=true;s->length=0;++s->generation;return i;
        }
    }
    return -1;
}
bool cp_pool_commit(cp_pool_t *pool,int slot,size_t length,int64_t received_us)
{
    if (!valid(slot)) return false;
    cp_slot_t *s=&pool->slots[slot];
    if (!s->writing || s->references!=1 || !length || length>s->capacity || received_us<0) return false;
    s->writing=false;s->length=length;s->received_us=received_us;s->sequence=++pool->next_sequence;return true;
}
bool cp_pool_publish(cp_pool_t *pool,int slot)
{
    if (!valid(slot)) return false;
    cp_slot_t *s=&pool->slots[slot];
    if (s->writing || !s->length || !s->references || s->references==UINT_MAX) return false;
    if (pool->latest==slot) return true;
    if (valid(pool->latest)) --pool->slots[pool->latest].references;
    pool->latest=slot;++s->references;return true;
}
bool cp_pool_release(cp_pool_t *pool,int slot,uint32_t generation)
{
    if (!valid(slot)) return false;
    cp_slot_t *s=&pool->slots[slot];
    if (s->generation!=generation || !s->references || (slot==pool->latest && s->references==1)) return false;
    --s->references;
    if (!s->references) {s->writing=false;s->length=0;}
    return true;
}
bool cp_pool_acquire_latest(cp_pool_t *pool,cp_frame_ref_t *view)
{
    if (!view || !valid(pool->latest)) return false;
    cp_slot_t *s=&pool->slots[pool->latest];
    if (s->writing || !s->length || s->references==UINT_MAX) return false;
    ++s->references;
    *view=(cp_frame_ref_t){.slot=pool->latest,.generation=s->generation,.sequence=s->sequence,
        .storage=s->storage,.length=s->length,.received_us=s->received_us,.active=true};return true;
}
bool cp_pool_release_view(cp_pool_t *pool,cp_frame_ref_t *view)
{
    if (!view || !view->active) return false;
    const bool ok=cp_pool_release(pool,view->slot,view->generation);
    view->active=false;return ok;
}
unsigned cp_pool_free_slots(const cp_pool_t *pool)
{
    unsigned count=0;for (unsigned i=0;i<CP_POOL_SLOTS;++i) count+=pool->slots[i].references==0;return count;
}
