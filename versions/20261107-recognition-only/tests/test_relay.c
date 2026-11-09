/* SPDX-License-Identifier: Apache-2.0 */
#include "rm_relay.h"
#include "rm_relay_config.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
static const int ids[] = {530, 533, 568};
static const int alternate_ids[] = {514, 542, 579};
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
#define EVENT(s,e,t) rm_relay_event(&(s),(e),(s).epoch,(t))
#define LOCK(s,c,t) rm_relay_lock(&(s),(c),ids[c],(s).epoch,(t))
#define SAMPLE(s,e,h,t) rm_relay_sample(&(s),(e),(h),(s).epoch,(t))

int main(void)
{
    const int four = (int)rm_p3_color, first = (int)rm_p1_color, last = (int)rm_p2_color;
    rm_relay_t s;
    rm_relay_init(&s, true);
    CHECK(rm_four_color == rm_p3_color && rm_p1_color == RM_P1_COLOR && rm_p2_color == RM_P2_COLOR && rm_p3_color == RM_P3_COLOR && RM_INPUT_COUNT == 2);
    for (int i = 0; i < RM_COLOR_COUNT; ++i) { CHECK(!rm_relay_output(&s, i, 0)); }
    CHECK(rm_relay_lock(&s, first, ids[four], s.epoch, 0) == RM_LOCK_REJECTED);
    CHECK(LOCK(s, first, 10) == RM_LOCK_REGISTERED);
    CHECK(LOCK(s, four, 20) == RM_LOCK_REGISTERED);
    CHECK(LOCK(s, last, 30) == RM_LOCK_REGISTERED);
    CHECK(s.colors[first].multiplier == 2 && s.colors[four].multiplier == 4 && s.colors[last].multiplier == 2);
    CHECK(s.colors[first].order == 1 && s.colors[four].order == 2 && s.colors[last].order == 3);
    CHECK(rm_relay_lock(&s, four, alternate_ids[four], s.epoch, 40) == RM_LOCK_DUPLICATE);
    CHECK(s.colors[four].tag_id == ids[four]);
    CHECK(EVENT(s, RM_ENTER, 100) && s.selected == four);
    CHECK(!EVENT(s, RM_ENTER, 200) && s.selected == four);
    CHECK(EVENT(s, RM_BATCH_DONE, 300) && s.colors[four].state == RM_USED);
    CHECK(!EVENT(s, RM_BATCH_DONE, 400));
    CHECK(LOCK(s, four, 450) == RM_LOCK_DUPLICATE && s.colors[four].state == RM_USED);
    CHECK(EVENT(s, RM_ENTER, 500) && s.selected == last);
    const int64_t dt[] = {0,99999,100000,199999,200000,499999,500000,999999,1000000};
    for (unsigned i = 0; i < sizeof(dt) / sizeof(dt[0]); ++i) {
        CHECK(rm_relay_output(&s, four, 300 + dt[i]) == (dt[i] % 1000000 < 500000));
        CHECK(rm_relay_output(&s, last, 500 + dt[i]) == (dt[i] % 200000 < 100000));
        CHECK(rm_relay_output(&s, first, 500 + dt[i]));
    }
    CHECK(EVENT(s, RM_BATCH_DONE, 2000000));
    CHECK(EVENT(s, RM_ENTER, 2000100) && s.selected == first);
    CHECK(!EVENT(s, (rm_event_t)2, 2000200));
    for (int active = 0; active <= 1; ++active) {
        CHECK(rm_relay_level(false, active) == !active);
        CHECK(rm_relay_level(true, active) == active);
    }
    puts("PASS fixed-four variable/all color records/no startup light/P3>P2>P1/used retention/time edges");

    rm_relay_init(&s, true);
    CHECK(LOCK(s, first, 0) == RM_LOCK_REGISTERED); /* P1 arrived first. */
    CHECK(LOCK(s, last, 10) == RM_LOCK_REGISTERED); /* P2 arrived later. */
    CHECK(EVENT(s, RM_ENTER, 20) && s.selected == last); /* No P3: choose P2. */
    CHECK(LOCK(s, four, 30) == RM_LOCK_REGISTERED && s.selected == last);
    CHECK(!EVENT(s, RM_ENTER, 40) && s.selected == last);
    CHECK(EVENT(s, RM_BATCH_DONE, 50));
    CHECK(EVENT(s, RM_ENTER, 60) && s.selected == four);
    CHECK(EVENT(s, RM_BATCH_DONE, 70));
    CHECK(EVENT(s, RM_ENTER, 80) && s.selected == first);
    puts("PASS P1-first/P2-later with no P3; late P3 does not replace frozen P2 batch");

    rm_relay_init(&s, true);
    CHECK(EVENT(s, RM_ENTER, 0) && s.batch_active && s.selected == -1);
    CHECK(LOCK(s, first, 100) == RM_LOCK_REGISTERED);
    CHECK(!EVENT(s, RM_ENTER, 200) && s.selected == -1);
    CHECK(EVENT(s, RM_BATCH_DONE, 300) && s.colors[first].state == RM_PENDING);
    CHECK(EVENT(s, RM_ENTER, 400) && s.selected == first);
    CHECK(LOCK(s, four, 500) == RM_LOCK_REGISTERED && s.selected == first);
    CHECK(s.colors[four].multiplier == 4);
    CHECK(!EVENT(s, RM_ENTER, 600) && s.selected == first);
    CHECK(EVENT(s, RM_BATCH_DONE, 700));
    CHECK(EVENT(s, RM_ENTER, 800) && s.selected == four);
    puts("PASS empty batch remains empty/later registration/frozen selection");

    for (int e = 0; e < RM_INPUT_COUNT; ++e) {
        rm_relay_init(&s, true);
        if (e == RM_BATCH_DONE) { EVENT(s, RM_ENTER, 0); }
        CHECK(!SAMPLE(s, e, true, 0));
        CHECK(!SAMPLE(s, e, true, 19999));
        CHECK(SAMPLE(s, e, true, 20000));
        CHECK(!SAMPLE(s, e, true, 200000)); /* 200ms high remains one event. */
        if (e == RM_ENTER) { EVENT(s, RM_BATCH_DONE, 210000); }
        if (e == RM_BATCH_DONE) { EVENT(s, RM_ENTER, 210000); }
        CHECK(!SAMPLE(s, e, false, 220000));
        CHECK(!SAMPLE(s, e, true, 225000)); /* Brief release must not rearm. */
        CHECK(!SAMPLE(s, e, true, 245000));
        CHECK(!SAMPLE(s, e, false, 400000));
        CHECK(!SAMPLE(s, e, false, 420000));
        CHECK(!SAMPLE(s, e, true, 600000));
        CHECK(SAMPLE(s, e, true, 620000));
    }
    rm_relay_reset(&s, false);
    CHECK(!s.enabled && !s.batch_active && s.registered_count == 0);
    CHECK(LOCK(s, four, 1000000) == RM_LOCK_REJECTED);
    const uint32_t stale = s.epoch;
    rm_relay_reset(&s, true);
    CHECK(rm_relay_lock(&s, four, ids[four], stale, 1100000) == RM_LOCK_REJECTED);
    CHECK(!rm_relay_event(&s, RM_ENTER, stale, 1100000));
    CHECK(!SAMPLE(s, RM_ENTER, true, 1200000));
    CHECK(!SAMPLE(s, RM_ENTER, true, 1400000) && !s.batch_active);
    CHECK(LOCK(s, four, 1500000) == RM_LOCK_REGISTERED && s.colors[four].multiplier == 4);
    CHECK(!SAMPLE(s, (rm_event_t)2, true, 1600000));
    puts("PASS two pulses/200ms contract/debounce/clear and pause/old epoch/fixed color survives reset");
    printf("PASS core assertions=%u four_color=%d (production rm_relay.c)\n", checks, (int)rm_four_color);
    return 0;
}
