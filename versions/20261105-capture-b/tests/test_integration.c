/* SPDX-License-Identifier: Apache-2.0
 * Windows host harness. The runner extracts the exact production signal,
 * commit/clear/pause/status/input functions into production_integration.inc.
 * Only hardware, time, audio and FreeRTOS primitives are replaced by stubs. */
#include <windows.h>
#include <assert.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "rm_relay.h"
#include "rm_relay_config.h"
#include "wifi_preview.h"

static unsigned checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
typedef int gpio_num_t;
typedef int esp_err_t;
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
enum { GPIO_MODE_OUTPUT, GPIO_MODE_INPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE,
       GPIO_PULLDOWN_ENABLE, GPIO_INTR_DISABLE };
#define TAG_SIGNAL_RED_GPIO 20
#define TAG_SIGNAL_GREEN_GPIO 21
#define TAG_SIGNAL_BLUE_GPIO 23
#define TAG_SIGNAL_CLOSE_GPIO 22
#define TAG_CLOSE_ACTIVE_LEVEL 1
#define TAG_CLOSE_DEBOUNCE_MS 20
#define TAG_CLOSE_POLL_MS 5
#define ESP_ERROR_CHECK(expr) CHECK((expr) == 0)
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(ms) (ms)
typedef enum { TAG_GROUP_NONE, TAG_GROUP_RED, TAG_GROUP_GREEN, TAG_GROUP_BLUE } tag_group_t;

static CRITICAL_SECTION action_lock;
static void *tag_action_mutex = &action_lock;
static volatile bool tag_detection_enabled = true;
static volatile uint32_t tag_clear_epoch;
static int gpio_levels[55];
static int gpio_reads[55];
static bool initializing;
static int lock_depth;
static int64_t fake_time;
static unsigned audio_requests, audio_cancels;
static jmp_buf poll_return;
static int polls_remaining;

static void host_log(const char *tag, const char *fmt, ...)
{
    (void)tag;
    (void)fmt;
}
#define ESP_LOGI(...) host_log(__VA_ARGS__)
#define ESP_LOGW(...) host_log(__VA_ARGS__)
static void xSemaphoreTake(void *mutex, int wait)
{
    (void)wait;
    EnterCriticalSection(mutex);
    assert(lock_depth == 0);
    lock_depth = 1;
}
static void xSemaphoreGive(void *mutex)
{
    assert(lock_depth == 1);
    lock_depth = 0;
    LeaveCriticalSection(mutex);
}
static int64_t esp_timer_get_time(void) { return fake_time; }
static esp_err_t gpio_set_level(gpio_num_t pin, int level)
{
    assert(initializing || lock_depth == 1);
    assert(pin >= 0 && pin < 55 && (level == 0 || level == 1));
    gpio_levels[pin] = level;
    return 0;
}
static int gpio_get_level(gpio_num_t pin) { ++gpio_reads[pin]; return gpio_levels[pin]; }
static esp_err_t gpio_config(const gpio_config_t *cfg)
{
    CHECK(cfg->pin_bit_mask != 0);
    if (cfg->mode == GPIO_MODE_INPUT) {
        CHECK(cfg->pin_bit_mask == ((1ULL << 22) | (1ULL << 4) | (1ULL << 5)));
    }
    return 0;
}
static const char *tag_group_name(tag_group_t group) { (void)group; return "host"; }
static void tag_audio_cancel_locked(void) { assert(lock_depth == 1); ++audio_cancels; }
static void tag_audio_request_group_locked(tag_group_t group)
{
    (void)group;
    assert(lock_depth == 1);
    ++audio_requests;
}
static void vTaskDelay(int ms)
{
    assert(lock_depth == 0);
    fake_time += (int64_t)ms * 1000;
    if (--polls_remaining == 0) { longjmp(poll_return, 1); }
}

static wifi_preview_lights_t observed_lights;
void wifi_preview_publish_lights(const wifi_preview_lights_t *snapshot)
{
    assert(initializing || lock_depth == 1);
    observed_lights = *snapshot;
}

#include "production_integration.inc"

static void init_fixture(void)
{
    memset(gpio_levels, 0, sizeof(gpio_levels));
    memset(gpio_reads, 0, sizeof(gpio_reads));
    tag_detection_enabled = true;
    tag_clear_epoch = 0;
    fake_time = 1000000;
    audio_requests = audio_cancels = 0;
    for (int i = 0; i < RM_COLOR_COUNT; ++i) { tag_relay_last_levels[i] = -1; }
    initializing = true;
    tag_signal_init();
    tag_close_input_init();
    initializing = false;
}

static void assert_off(void)
{
    for (int i = 0; i < RM_COLOR_COUNT; ++i) {
        CHECK(gpio_levels[tag_relay_outputs[i]] == !tag_relay_active_levels[i]);
        CHECK(tag_relay.colors[i].state == RM_UNSEEN);
    }
    CHECK(!tag_relay.batch_active && tag_relay.selected == -1);
    CHECK(tag_relay.epoch == tag_clear_epoch);
    CHECK(observed_lights.epoch == tag_relay.epoch);
    CHECK(observed_lights.enabled == tag_relay.enabled);
    for (int i = 0; i < RM_COLOR_COUNT; ++i) {
        CHECK(observed_lights.channels[i].state == RM_UNSEEN);
        CHECK(!observed_lights.channels[i].on);
    }
}

static void poll_times(int n)
{
    for (int i = 0; i < n; ++i) {
        tag_relay_poll_inputs();
        fake_time += 5000;
    }
}

static void test_real_poll_and_clear(void)
{
    init_fixture();
    assert_off();
    CHECK(tag_commit_lock(TAG_GROUP_RED, 530, tag_clear_epoch));
    CHECK(tag_commit_lock(TAG_GROUP_BLUE, 568, tag_clear_epoch));
    gpio_levels[RM_ENTER_GPIO] = 1;
    poll_times(6);
    CHECK(tag_relay.selected == RM_RED);
    gpio_levels[RM_BATCH_DONE_GPIO] = 1;
    poll_times(6);
    CHECK(!tag_relay.batch_active && tag_relay.colors[RM_RED].state == RM_USED);
    poll_times(100); /* Both held high: no repeated ENTER after DONE. */
    CHECK(!tag_relay.batch_active && tag_relay.colors[RM_BLUE].state == RM_PENDING);
    gpio_levels[RM_ENTER_GPIO] = gpio_levels[RM_BATCH_DONE_GPIO] = 0;
    poll_times(6);
    gpio_levels[RM_ENTER_GPIO] = 1;
    poll_times(6);
    CHECK(tag_relay.selected == RM_BLUE);
    tag_request_clear("host_uart_r_or_c");
    assert_off();
    poll_times(100); /* Old held ENTER cannot re-open an empty batch. */
    assert_off();

    init_fixture();
    CHECK(tag_commit_lock(TAG_GROUP_GREEN, 533, tag_clear_epoch));
    gpio_levels[TAG_SIGNAL_CLOSE_GPIO] = 1;
    gpio_levels[6] = 1; /* Removed event pin must never be sampled. */
    polls_remaining = 100;
    if (setjmp(poll_return) == 0) { tag_close_input_task(NULL); }
    CHECK(tag_clear_epoch == 1); /* Real original GPIO22 task: long high once. */
    assert_off();
    CHECK(audio_cancels == 1);
    CHECK(gpio_reads[6] == 0);
    puts("PASS real production GPIO poll/GPIO22 clear/held inputs/no stale output");
}

static void test_real_pause_resume(void)
{
    init_fixture();
    CHECK(tag_commit_lock(TAG_GROUP_RED, 530, tag_clear_epoch));
    tag_set_detection_enabled(false, "host_uart_p");
    assert_off();
    CHECK(!tag_detection_enabled && !tag_relay.enabled);
    CHECK(!tag_commit_lock(TAG_GROUP_BLUE, 568, tag_clear_epoch));
    tag_set_detection_enabled(true, "host_uart_u");
    assert_off();
    CHECK(tag_detection_enabled && tag_relay.enabled);
    tag_report_detection_status();
#if RM_UART_EVENT_INJECTION
    CHECK(tag_commit_lock(TAG_GROUP_BLUE, 568, tag_clear_epoch));
    CHECK(tag_relay.colors[RM_BLUE].multiplier == 2);
    tag_relay_inject_event(RM_ENTER);
    CHECK(tag_relay.selected == RM_BLUE);
    tag_relay_inject_event(RM_BATCH_DONE);
    CHECK(tag_relay.colors[RM_BLUE].state == RM_USED);
#endif
    puts("PASS real production p/u/status/audio cancellation/debug event handler if enabled");
}

static void test_p2_before_p1_gpio_path(void)
{
    init_fixture();
    CHECK(tag_commit_lock(TAG_GROUP_BLUE, 568, tag_clear_epoch));
    CHECK(tag_commit_lock(TAG_GROUP_GREEN, 533, tag_clear_epoch));
    gpio_levels[RM_ENTER_GPIO] = 1;
    poll_times(6);
    CHECK(tag_relay.selected == RM_GREEN); /* P2 wins over earlier P1. */
    CHECK(observed_lights.channels[RM_GREEN].state == RM_SELECTED);
    CHECK(observed_lights.channels[RM_BLUE].state == RM_PENDING);
    CHECK(tag_commit_lock(TAG_GROUP_RED, 530, tag_clear_epoch));
    CHECK(tag_relay.selected == RM_GREEN && tag_relay.colors[RM_RED].state == RM_PENDING);
    gpio_levels[RM_BATCH_DONE_GPIO] = 1;
    poll_times(6);
    CHECK(!tag_relay.batch_active && tag_relay.colors[RM_GREEN].state == RM_USED);
    poll_times(10);
    CHECK(!tag_relay.batch_active); /* Held ENTER cannot select another role. */
    gpio_levels[RM_ENTER_GPIO] = gpio_levels[RM_BATCH_DONE_GPIO] = 0;
    poll_times(6);
    gpio_levels[RM_ENTER_GPIO] = 1;
    poll_times(6);
    CHECK(tag_relay.selected == RM_RED);
    CHECK(observed_lights.channels[RM_RED].state == RM_SELECTED);
    CHECK(observed_lights.channels[RM_BLUE].state == RM_PENDING);
    puts("PASS P1-first/P2-later via actual GPIO handler; late P3 waits for next batch; web snapshot agrees");
}

typedef struct { HANDLE gate; uint32_t old_epoch; bool result; } old_commit_t;
static DWORD WINAPI old_commit_thread(void *arg)
{
    old_commit_t *job = arg;
    assert(WaitForSingleObject(job->gate, 5000) == WAIT_OBJECT_0);
    job->result = tag_commit_lock(TAG_GROUP_RED, 530, job->old_epoch);
    return 0;
}

static void test_real_thread_stale_commit(void)
{
    for (int i = 0; i < 16; ++i) {
        init_fixture();
        CHECK(tag_commit_lock(TAG_GROUP_RED, 530, tag_clear_epoch));
        old_commit_t job = {CreateEvent(NULL, TRUE, FALSE, NULL), tag_clear_epoch, true};
        CHECK(job.gate != NULL);
        HANDLE worker = CreateThread(NULL, 0, old_commit_thread, &job, 0, NULL);
        CHECK(worker != NULL);
        tag_request_clear("host_concurrent_clear");
        SetEvent(job.gate);
        CHECK(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0);
        CHECK(!job.result);
        assert_off();
        CloseHandle(worker);
        CloseHandle(job.gate);
    }
    puts("PASS real production mutex/epoch with 16 delayed old LOCK threads");
}

int main(void)
{
    InitializeCriticalSection(&action_lock);
    test_real_poll_and_clear();
    test_real_pause_resume();
    test_p2_before_p1_gpio_path();
    test_real_thread_stale_commit();
    DeleteCriticalSection(&action_lock);
    printf("PASS integration assertions=%u polarity=%d/%d/%d injection=%d\n", checks,
           RM_RED_ACTIVE_LEVEL, RM_GREEN_ACTIVE_LEVEL, RM_BLUE_ACTIVE_LEVEL, RM_UART_EVENT_INJECTION);
    return 0;
}
