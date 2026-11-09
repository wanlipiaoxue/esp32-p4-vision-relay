/* SPDX-License-Identifier: Apache-2.0
 * Runs the actual production submit/encode functions with RTOS/JPEG API stubs.
 * No hardware encoding is simulated as proof of P4 JPEG output. */
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "preview_color.h"

typedef int esp_err_t;
typedef struct {
    unsigned height, width;
    int src_type, sub_sample;
    unsigned image_quality;
} jpeg_encode_cfg_t;
#define ESP_OK 0
#define ESP_ERR_TIMEOUT -1
#define JPEG_ENCODE_IN_FORMAT_RGB888 101
#define JPEG_DOWN_SAMPLING_YUV422 202
#define PREVIEW_W PREVIEW_COLOR_WIDTH
#define PREVIEW_H PREVIEW_COLOR_HEIGHT
#define PREVIEW_RAW_SIZE PREVIEW_COLOR_RAW_SIZE
#define PREVIEW_JPEG_CAPACITY (64U * 1024U)
#define PREVIEW_MAX_AGE_US 250000LL
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffU

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static int interest_mux;
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
static uint8_t input[PREVIEW_COLOR_INPUT_SIZE], raw[PREVIEW_RAW_SIZE];
static uint8_t encoded[PREVIEW_JPEG_CAPACITY], published[PREVIEW_JPEG_CAPACITY];
static uint8_t *color_buffer = raw, *encoded_buffer = encoded, *published_buffer = published;
static size_t color_capacity = sizeof(raw), encoded_capacity = sizeof(encoded), published_length;
static bool preview_ready = true, transport_failed, interested = true, raw_available = true, image_available = true;
static int raw_free = 1, image_mutex = 2, encoder = 3, encoder_task_handle = 4;
static int64_t now_us = 1000000, last_input_us, raw_received_us, published_received_us;
static uint32_t received_frames, dropped_frames, encoded_frames, published_sequence;
static uint32_t last_encode_us, encode_failures;
static unsigned notifications, notification_reads, jpeg_calls;
static int jpeg_result;
static jmp_buf task_finished;

static int64_t esp_timer_get_time(void) { return now_us; }
static bool viewer_interested(int64_t timestamp) { (void)timestamp; return interested; }
static void record_drop(void) { ++dropped_frames; }
static int xSemaphoreTake(int semaphore, unsigned timeout)
{
    (void)timeout;
    bool *available = semaphore == raw_free ? &raw_available : &image_available;
    if (!*available) return 0;
    *available = false;
    return pdTRUE;
}
static void xSemaphoreGive(int semaphore)
{
    bool *available = semaphore == raw_free ? &raw_available : &image_available;
    CHECK(!*available);
    *available = true;
}
static void xTaskNotifyGive(int task) { CHECK(task == encoder_task_handle); ++notifications; }
static void ulTaskNotifyTake(int clear, unsigned timeout)
{
    CHECK(clear == pdTRUE && timeout == portMAX_DELAY);
    if (notification_reads++) longjmp(task_finished, 1);
    CHECK(notifications > 0);
    --notifications;
}
static esp_err_t jpeg_encoder_process(int handle, const jpeg_encode_cfg_t *cfg,
    const uint8_t *in, uint32_t in_size, uint8_t *out, uint32_t capacity, uint32_t *length)
{
    ++jpeg_calls;
    CHECK(handle == encoder && cfg->width == 320 && cfg->height == 240);
    CHECK(cfg->src_type == JPEG_ENCODE_IN_FORMAT_RGB888);
    CHECK(cfg->sub_sample == JPEG_DOWN_SAMPLING_YUV422 && cfg->image_quality == 55);
    CHECK(in == color_buffer && in_size == 230400 && !raw_available);
    CHECK(in[0] == 0 && in[1] <= 2 && in[2] == 255); /* BGR red */
    CHECK(out == encoded_buffer && capacity == PREVIEW_JPEG_CAPACITY);
    *length = 20;
    memset(out, 0x5a, *length);
    now_us += 700;
    return jpeg_result;
}
#include "color_pipeline.inc"

static void run_encoder(void)
{
    notification_reads = 0;
    if (setjmp(task_finished) == 0) encode_task(NULL);
}

int main(void)
{
    for (size_t i = 0; i < sizeof(input); i += 4) {
        input[i] = 82; input[i + 1] = 90; input[i + 2] = 82; input[i + 3] = 240;
    }
    wifi_preview_submit_yuy2(NULL, sizeof(input), now_us);
    wifi_preview_submit_yuy2(input, sizeof(input) - 1, now_us);
    CHECK(received_frames == 0 && notifications == 0 && raw_available);
    wifi_preview_submit_yuy2(input, sizeof(input), now_us);
    CHECK(received_frames == 1 && notifications == 1 && !raw_available);
    CHECK(raw_received_us == now_us && last_input_us == now_us);
    const uint32_t drops_before = dropped_frames;
    wifi_preview_submit_yuy2(input, sizeof(input), now_us);
    CHECK(notifications == 1 && dropped_frames == drops_before + 1);
    memset(input, 0, sizeof(input)); /* USB frame may be returned/reused now. */
    run_encoder();
    CHECK(jpeg_calls == 1 && raw_available && image_available);
    CHECK(encoded_frames == 1 && published_sequence == 1 && published_length == 20);
    CHECK(published_received_us == 1000000 && last_encode_us == 700);
    CHECK(!memcmp(published_buffer, encoded_buffer, 20));

    color_capacity = sizeof(raw) - 1;
    wifi_preview_submit_yuy2(input, sizeof(input), now_us);
    CHECK(raw_available && notifications == 0 && dropped_frames == drops_before + 2);
    color_capacity = sizeof(raw);
    for (size_t i = 0; i < sizeof(input); i += 4) {
        input[i] = 82; input[i + 1] = 90; input[i + 2] = 82; input[i + 3] = 240;
    }
    jpeg_result = -1;
    wifi_preview_submit_yuy2(input, sizeof(input), now_us);
    run_encoder();
    CHECK(raw_available && encode_failures == 1 && encoded_frames == 1 && published_sequence == 1);
    printf("PASS actual color submit/encode pipeline: %u checks (JPEG/RTOS API stubs, not P4 encoding)\n", checks);
    return 0;
}
