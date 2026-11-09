/* SPDX-License-Identifier: Apache-2.0 */
#include "rm_mjpeg.h"
#include "rm_jpeg.h"
#include "rm_capture_config.h"
#include <stdlib.h>
#include "driver/jpeg_decode.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "RM_MJPEG";
static jpeg_decoder_handle_t decoder;
static uint8_t *input_buffer, *pixel_buffer;
static size_t input_capacity, pixel_capacity, input_length;
static portMUX_TYPE stats_mux = portMUX_INITIALIZER_UNLOCKED;
static rm_mjpeg_stats_t stats;

bool rm_mjpeg_init(void)
{
    if (stats.ready) return true;
    const jpeg_decode_memory_alloc_cfg_t in_cfg = {.buffer_direction=JPEG_DEC_ALLOC_INPUT_BUFFER};
    const jpeg_decode_memory_alloc_cfg_t out_cfg = {.buffer_direction=JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    input_buffer = jpeg_alloc_decoder_mem(RM_CAPTURE_FRAME_CAPACITY + 1024U, &in_cfg, &input_capacity);
    pixel_buffer = jpeg_alloc_decoder_mem(640U * 480U * 3U, &out_cfg, &pixel_capacity);
    const jpeg_decode_engine_cfg_t cfg = {.intr_priority=1, .timeout_ms=40};
    if (!input_buffer || !pixel_buffer || jpeg_new_decoder_engine(&cfg, &decoder) != ESP_OK) {
        free(input_buffer); free(pixel_buffer);
        input_buffer = pixel_buffer = NULL;
        ESP_LOGE(TAG, "Decoder initialization failed; MJPEG capture will not start");
        return false;
    }
    portENTER_CRITICAL(&stats_mux);
    stats.ready = true;
    portEXIT_CRITICAL(&stats_mux);
    return true;
}

const uint8_t *rm_mjpeg_prepare(const uint8_t *source, size_t length, size_t *prepared_length)
{
    input_length = 0;
    if (prepared_length) *prepared_length = 0;
    rm_jpeg_info_t info;
    const bool valid = stats.ready && prepared_length && length <= RM_CAPTURE_FRAME_CAPACITY &&
        rm_jpeg_normalize(source, length, input_buffer, input_capacity, 640, 480, &info);
    portENTER_CRITICAL(&stats_mux);
    if (valid) { ++stats.prepared; stats.dht_insertions += info.inserted_standard_dht; }
    else ++stats.prepare_errors;
    portEXIT_CRITICAL(&stats_mux);
    if (!valid) return NULL;
    input_length = info.length;
    *prepared_length = input_length;
    return input_buffer;
}

bool rm_mjpeg_copy_gray(uint8_t *half, uint8_t *full)
{
    if (!stats.ready || !input_length || !half) return false;
    const int64_t started = esp_timer_get_time();
    jpeg_decode_picture_info_t info;
    esp_err_t err = jpeg_decoder_get_info(input_buffer, (uint32_t)input_length, &info);
    bool gray = false;
    uint32_t output_length = 0;
    if (err == ESP_OK && info.width == 640 && info.height == 480) {
        gray = info.sample_method == JPEG_DOWN_SAMPLING_GRAY;
        const jpeg_decode_cfg_t cfg = {
            .output_format=gray ? JPEG_DECODE_OUT_FORMAT_GRAY : JPEG_DECODE_OUT_FORMAT_RGB888,
            .rgb_order=JPEG_DEC_RGB_ELEMENT_ORDER_RGB, .conv_std=JPEG_YUV_RGB_CONV_STD_BT601,
        };
        err = jpeg_decoder_process(decoder, &cfg, input_buffer, (uint32_t)input_length,
            pixel_buffer, (uint32_t)pixel_capacity, &output_length);
    } else if (err == ESP_OK) err = ESP_ERR_INVALID_ARG;
    const bool ok = err == ESP_OK && rm_jpeg_pixels_to_gray(pixel_buffer, output_length, !gray, half, full);
    portENTER_CRITICAL(&stats_mux);
    stats.last_decode_us = (uint32_t)(esp_timer_get_time() - started);
    if (ok) ++stats.decoded;
    else ++stats.decode_errors;
    portEXIT_CRITICAL(&stats_mux);
    return ok;
}

void rm_mjpeg_get_stats(rm_mjpeg_stats_t *snapshot)
{
    if (!snapshot) return;
    portENTER_CRITICAL(&stats_mux);
    *snapshot = stats;
    portEXIT_CRITICAL(&stats_mux);
}
