/* SPDX-License-Identifier: Apache-2.0 */
#include "rm_relay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
#define EVENT(s, e, t) rm_relay_event(&(s), (e), (s).epoch, (t))
#define LOCK(s, c, id, t) rm_relay_lock(&(s), (c), (id), (s).epoch, (t))
#define SAMPLE(s, e, h, t) rm_relay_sample(&(s), (e), (h), (s).epoch, (t))

static void test_registration_priority(void)
{
    rm_relay_t s;
    rm_relay_init(&s, true);
    CHECK(!s.batch_active && s.selected == -1 && !s.next_four);
    CHECK(LOCK(s, RM_RED, 533, 0) == RM_LOCK_REJECTED);
    CHECK(LOCK(s, -1, 530, 0) == RM_LOCK_REJECTED);
    CHECK(LOCK(s, RM_RED, 530, 10) == RM_LOCK_REGISTERED);
    CHECK(s.colors[RM_RED].multiplier == 2 && s.colors[RM_RED].order == 1);
    CHECK(EVENT(s, RM_NEXT_FOUR, 20));
    CHECK(EVENT(s, RM_NEXT_FOUR, 30));
    CHECK(LOCK(s, RM_RED, 514, 40) == RM_LOCK_DUPLICATE);
    CHECK(s.next_four && s.colors[RM_RED].tag_id == 530);
    CHECK(s.colors[RM_RED].multiplier == 2);
    CHECK(LOCK(s, RM_BLUE, 568, 50) == RM_LOCK_REGISTERED);
    CHECK(s.colors[RM_BLUE].multiplier == 4 && !s.next_four);
    CHECK(LOCK(s, RM_GREEN, 533, 60) == RM_LOCK_REGISTERED);
    CHECK(s.colors[RM_GREEN].multiplier == 2 && s.colors[RM_GREEN].order == 3);
    CHECK(EVENT(s, RM_ENTER, 100));
    CHECK(s.selected == RM_BLUE);
    CHECK(!EVENT(s, RM_ENTER, 200) && s.selected == RM_BLUE);
    CHECK(EVENT(s, RM_BATCH_DONE, 300));
    CHECK(s.colors[RM_BLUE].state == RM_USED && !s.batch_active);
    CHECK(!EVENT(s, RM_BATCH_DONE, 400));
    CHECK(EVENT(s, RM_NEXT_FOUR, 450));
    CHECK(LOCK(s, RM_BLUE, 579, 500) == RM_LOCK_DUPLICATE);
    CHECK(s.colors[RM_BLUE].state == RM_USED && s.next_four);
    CHECK(EVENT(s, RM_ENTER, 600) && s.selected == RM_RED);
    CHECK(EVENT(s, RM_BATCH_DONE, 700));
    CHECK(EVENT(s, RM_ENTER, 800) && s.selected == RM_GREEN);
    CHECK(EVENT(s, RM_BATCH_DONE, 900));
    CHECK(EVENT(s, RM_ENTER, 1000) && s.selected == -1 && s.batch_active);
    CHECK(s.next_four);  /* No new color remains: marker stays until clear. */
    puts("PASS registration/default2/one-shot/repeated-ID/4>2/FIFO/used-retention");
}

static void test_frozen_and_empty_batches(void)
{
    rm_relay_t s;
    rm_relay_init(&s, true);
    CHECK(EVENT(s, RM_ENTER, 0));
    CHECK(s.batch_active && s.selected == -1);
    CHECK(LOCK(s, RM_RED, 530, 100) == RM_LOCK_REGISTERED);
    CHECK(!EVENT(s, RM_ENTER, 200));
    CHECK(s.selected == -1 && s.colors[RM_RED].state == RM_PENDING);
    CHECK(EVENT(s, RM_BATCH_DONE, 300));
    CHECK(s.colors[RM_RED].state == RM_PENDING);
    CHECK(!EVENT(s, RM_BATCH_DONE, 400));
    CHECK(EVENT(s, RM_ENTER, 500) && s.selected == RM_RED);
    CHECK(EVENT(s, RM_NEXT_FOUR, 600));
    CHECK(LOCK(s, RM_GREEN, 533, 700) == RM_LOCK_REGISTERED);
    CHECK(s.colors[RM_GREEN].multiplier == 4 && s.selected == RM_RED);
    CHECK(!EVENT(s, RM_ENTER, 800) && s.selected == RM_RED);
    CHECK(EVENT(s, RM_BATCH_DONE, 900));
    CHECK(s.colors[RM_GREEN].state == RM_PENDING && s.selected == -1);
    CHECK(EVENT(s, RM_ENTER, 1000) && s.selected == RM_GREEN);
    puts("PASS frozen selection/empty ENTER cycle/repeated ENTER and DONE");
}

static void test_timing_and_polarity(void)
{
    rm_relay_t s;
    rm_relay_init(&s, true);
    CHECK(!rm_relay_output(&s, RM_RED, 0));
    LOCK(s, RM_RED, 530, 0);
    LOCK(s, RM_BLUE, 568, 0);
    LOCK(s, RM_GREEN, 533, 0);
    EVENT(s, RM_ENTER, 1000);
    CHECK(s.selected == RM_RED);
    CHECK(rm_relay_output(&s, RM_RED, 1000));
    CHECK(rm_relay_output(&s, RM_RED, 100999));
    CHECK(!rm_relay_output(&s, RM_RED, 101000));
    CHECK(!rm_relay_output(&s, RM_RED, 200999));
    CHECK(rm_relay_output(&s, RM_RED, 201000));
    EVENT(s, RM_BATCH_DONE, 1000000);
    EVENT(s, RM_ENTER, 1000000);
    CHECK(s.selected == RM_BLUE);
    const int64_t offsets[] = {0, 99999, 100000, 199999, 200000, 499999, 500000, 999999, 1000000, 9999999999LL};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        const int64_t dt = offsets[i];
        CHECK(rm_relay_output(&s, RM_RED, 1000000 + dt) == (dt % 1000000 < 500000));
        CHECK(rm_relay_output(&s, RM_BLUE, 1000000 + dt) == (dt % 200000 < 100000));
        CHECK(rm_relay_output(&s, RM_GREEN, 1000000 + dt));
    }
    for (int active = 0; active <= 1; ++active) {
        CHECK(rm_relay_level(false, active) == 1 - active);
        CHECK(rm_relay_level(true, active) == active);
    }
    puts("PASS independent used1Hz/selected5Hz/pending-steady/exact time boundaries/both polarities");
}

static void test_pulse_debounce(void)
{
    for (int event = 0; event < RM_INPUT_COUNT; ++event) {
        rm_relay_t s;
        rm_relay_init(&s, true);
        if (event == RM_BATCH_DONE) { EVENT(s, RM_ENTER, 0); }
        CHECK(!SAMPLE(s, event, true, 0));
        CHECK(!SAMPLE(s, event, true, 19999));
        CHECK(!SAMPLE(s, event, false, 19999)); /* short high discarded */
        CHECK(!SAMPLE(s, event, true, 20000));
        CHECK(SAMPLE(s, event, true, 40000));
        for (int i = 1; i < 50; ++i) {
            CHECK(!SAMPLE(s, event, true, 40000 + i * 100000));
        }
        if (event == RM_ENTER) { EVENT(s, RM_BATCH_DONE, 5000000); }
        if (event == RM_BATCH_DONE) { EVENT(s, RM_ENTER, 5000000); }
        CHECK(!SAMPLE(s, event, false, 5100000));
        CHECK(!SAMPLE(s, event, true, 5105000)); /* low bounce must not rearm */
        CHECK(!SAMPLE(s, event, true, 5200000));
        CHECK(!SAMPLE(s, event, false, 5300000));
        CHECK(!SAMPLE(s, event, false, 5319999));
        CHECK(!SAMPLE(s, event, false, 5320000));
        CHECK(!SAMPLE(s, event, true, 5400000));
        CHECK(SAMPLE(s, event, true, 5420000));
    }
    puts("PASS all three pulses/20ms high and release/long-high one-shot/bounce/time-zero");
}

static void test_clear_pause_stale(void)
{
    rm_relay_t s;
    rm_relay_init(&s, true);
    LOCK(s, RM_RED, 530, 0);
    EVENT(s, RM_ENTER, 0);
    EVENT(s, RM_NEXT_FOUR, 0);
    SAMPLE(s, RM_NEXT_FOUR, true, 0);
    const uint32_t old_epoch = s.epoch;
    rm_relay_reset(&s, false); /* Same reset used by p. */
    CHECK(!s.enabled && !s.next_four && !s.batch_active && s.selected == -1);
    CHECK(s.registered_count == 0 && s.epoch == old_epoch + 1);
    CHECK(LOCK(s, RM_BLUE, 568, 100) == RM_LOCK_REJECTED);
    CHECK(!EVENT(s, RM_ENTER, 100));
    for (int i = 0; i < RM_COLOR_COUNT; ++i) {
        CHECK(s.colors[i].state == RM_UNSEEN && !rm_relay_output(&s, i, 100));
    }
    rm_relay_reset(&s, true); /* Same reset used by u, r/c, and GPIO22. */
    CHECK(rm_relay_lock(&s, RM_RED, 530, old_epoch, 200) == RM_LOCK_REJECTED);
    CHECK(!rm_relay_event(&s, RM_NEXT_FOUR, old_epoch, 200));
    CHECK(!rm_relay_sample(&s, RM_ENTER, true, old_epoch, 200));
    CHECK(!SAMPLE(s, RM_NEXT_FOUR, true, 300));
    CHECK(!SAMPLE(s, RM_NEXT_FOUR, true, 1000000));
    CHECK(!s.next_four); /* Held pre-clear input cannot resurrect marker. */
    CHECK(!SAMPLE(s, RM_NEXT_FOUR, false, 1100000));
    CHECK(!SAMPLE(s, RM_NEXT_FOUR, false, 1120000));
    CHECK(!SAMPLE(s, RM_NEXT_FOUR, true, 1200000));
    CHECK(SAMPLE(s, RM_NEXT_FOUR, true, 1220000));
    CHECK(s.next_four);
    CHECK(LOCK(s, RM_GREEN, 533, 1230000) == RM_LOCK_REGISTERED);
    CHECK(s.colors[RM_GREEN].multiplier == 4);
    rm_relay_reset(&s, true);
    CHECK(!s.next_four && !s.batch_active && s.registered_count == 0);
    puts("PASS clear/pause/resume/old epoch/partial and held pulse invalidation");
}

static void test_interleavings(void)
{
    /* Deterministic permutations of an old LOCK, old event, and clear.
     * Production serialization is exercised separately by integration tests. */
    const int permutations[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    for (int p = 0; p < 6; ++p) {
        rm_relay_t s;
        rm_relay_init(&s, true);
        const uint32_t epoch = s.epoch;
        for (int i = 0; i < 3; ++i) {
            switch (permutations[p][i]) {
            case 0: rm_relay_lock(&s, RM_RED, 530, epoch, 0); break;
            case 1: rm_relay_event(&s, RM_NEXT_FOUR, epoch, 0); break;
            case 2: rm_relay_reset(&s, true); break;
            }
        }
        CHECK(s.colors[RM_RED].state == RM_UNSEEN);
        CHECK(!s.next_four && !s.batch_active && s.selected == -1);
        CHECK(!rm_relay_output(&s, RM_RED, 1000000));
    }
    puts("PASS six clear/old-LOCK/old-event interleavings");
}

int main(void)
{
    test_registration_priority();
    test_frozen_and_empty_batches();
    test_timing_and_polarity();
    test_pulse_debounce();
    test_clear_pause_stale();
    test_interleavings();
    printf("PASS core assertions=%u (production rm_relay.c)\n", checks);
    return 0;
}
