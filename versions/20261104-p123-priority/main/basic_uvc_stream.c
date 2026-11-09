/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <sdkconfig.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_timer.h"
#include "apriltag.h"
#include "tag36h11.h"
#include "golden_tag_530.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "usb/usb_host.h"
#include "usb/uvc_host.h"
#include "wifi_preview.h"
#include "rm_relay.h"
#include "rm_relay_config.h"

#define EXAMPLE_USB_HOST_PRIORITY   (15)
#define EXAMPLE_USB_DEVICE_VID      (0x0BDA)
#define EXAMPLE_USB_DEVICE_PID      (0x1376)
#if CONFIG_SPIRAM
#define EXAMPLE_FRAME_COUNT         (3)
#else
#define EXAMPLE_FRAME_COUNT         (2)
#endif
#define EXAMPLE_FRAME_TIMEOUT_MS    (5000)
#define EXAMPLE_STREAM_RETRY_MS     (2000)
#define EXAMPLE_STREAM_STATUS_US    (30000000LL)
#define EXAMPLE_NUMBER_OF_STREAMS   (1) // This example shows how to control multiple UVC streams. Set this to 1 if you camera offers only 1 stream
#define EXAMPLE_USE_SDCARD          (0) // SD card on P4 evaluation board will be initialized

#define UVC_DESC_DWFRAMEINTERVAL_TO_FPS(dwFrameInterval) (((dwFrameInterval) != 0) ? 10000000 / ((float)(dwFrameInterval)) : 0)

#define JPG_MIN_SIZE 40000
#define JPG_COMPRESSION_RATIO 6
#define max(a, b) ((a) > (b) ? (a) : (b))
#define UVC_ESTIMATE_JPG_SIZE_B(width, height, bpp) \
    (max(((width) * (height) * (bpp) / 8) / JPG_COMPRESSION_RATIO, JPG_MIN_SIZE))

static const char *TAG = "UVC example";
static uint8_t *tag_gray;
static uint8_t *tag_gray_full;
static uint8_t *tag_gray_l2;
static uint32_t *tag_l2_integral;
static SemaphoreHandle_t tag_buffer_free;
static TaskHandle_t tag_task_handle;
static volatile bool tag_full_frame_requested;
static volatile bool tag_full_frame_ready;
typedef enum {
    TAG_INPUT_LIVE,
    TAG_INPUT_GOLDEN,
    TAG_INPUT_EMPTY,
    TAG_INPUT_L2_TEST,
} tag_input_source_t;
static volatile tag_input_source_t tag_input_source = TAG_INPUT_LIVE;
static volatile bool tag_preview_requested;
static volatile bool tag_detection_enabled = true;

typedef enum {
    TAG_GROUP_NONE,
    TAG_GROUP_RED,
    TAG_GROUP_GREEN,
    TAG_GROUP_BLUE,
} tag_group_t;

typedef enum {
    TAG_PROFILE_D1_REFINE = 1,
    TAG_PROFILE_D2_REFINE = 2,
    TAG_PROFILE_D2_FAST = 3,
    TAG_PROFILE_D1_FAST = 4,
} tag_profile_t;
static volatile tag_profile_t requested_tag_profile = TAG_PROFILE_D2_FAST;
static volatile bool tag_l2_enabled = true;

#define TAG_CONFIRM_WINDOW      5
#define TAG_CONFIRM_HITS        3
#define TAG_L2_CONFIRM_HITS     4
#define TAG_L2_START_MISSES     2
#define TAG_L2_MAX_MISSES       3
#define TAG_L2_BACKOFF_MS       1200
#define TAG_LOST_TIMEOUT_US     2500000LL
#define TAG_SAMPLE_INTERVAL_MS  200
#define TAG_MIN_MARGIN          60.0f
#define TAG_L2_PREFILTER_MARGIN 40.0f
#define TAG_MIN_BOUNDING_PIXELS 40
#define TAG_MIN_VISIBLE_RATIO   0.95f
#define TAG_L2_PREPARE_WINDOW   61
#define TAG_L2_PREPARE_BIAS     16
#define TAG_L2_PAD_PIXELS       16
#define TAG_L2_IMAGE_WIDTH      (640 + TAG_L2_PAD_PIXELS * 2)
#define TAG_L2_IMAGE_HEIGHT     (480 + TAG_L2_PAD_PIXELS * 2)
#define TAG_SIGNAL_RED_GPIO     GPIO_NUM_20
#define TAG_SIGNAL_GREEN_GPIO   GPIO_NUM_21
#define TAG_SIGNAL_BLUE_GPIO    GPIO_NUM_23
#define TAG_SIGNAL_CLOSE_GPIO   GPIO_NUM_22

#define TAG_CLOSE_ACTIVE_LEVEL      1
#define TAG_CLOSE_DEBOUNCE_MS       20
#define TAG_CLOSE_POLL_MS           5

#define TAG_AUDIO_I2C_PORT          I2C_NUM_0
#define TAG_AUDIO_I2C_SDA_GPIO      GPIO_NUM_7
#define TAG_AUDIO_I2C_SCL_GPIO      GPIO_NUM_8
#define TAG_AUDIO_I2S_PORT          I2S_NUM_0
#define TAG_AUDIO_I2S_MCLK_GPIO     GPIO_NUM_13
#define TAG_AUDIO_I2S_BCLK_GPIO     GPIO_NUM_12
#define TAG_AUDIO_I2S_WS_GPIO       GPIO_NUM_10
#define TAG_AUDIO_I2S_DOUT_GPIO     GPIO_NUM_9
#define TAG_AUDIO_I2S_DIN_GPIO      GPIO_NUM_11
#define TAG_AUDIO_PA_GPIO           GPIO_NUM_53
#define TAG_AUDIO_SAMPLE_RATE_HZ    16000
#define TAG_AUDIO_MCLK_MULTIPLE     384
#define TAG_AUDIO_CHANNELS          2
#define TAG_AUDIO_FRAME_CHUNK       256
#define TAG_AUDIO_REPEAT_COUNT      3
#define TAG_AUDIO_REPEAT_GAP_MS     150

static int tag_speaker_volume_percent = 100;

extern const uint8_t voice_red_pcm_start[] asm("_binary_voice_red_pcm_start");
extern const uint8_t voice_red_pcm_end[] asm("_binary_voice_red_pcm_end");
extern const uint8_t voice_green_pcm_start[] asm("_binary_voice_green_pcm_start");
extern const uint8_t voice_green_pcm_end[] asm("_binary_voice_green_pcm_end");
extern const uint8_t voice_blue_pcm_start[] asm("_binary_voice_blue_pcm_start");
extern const uint8_t voice_blue_pcm_end[] asm("_binary_voice_blue_pcm_end");

typedef struct {
    tag_group_t group;
    uint32_t generation;
} tag_audio_request_t;

static i2s_chan_handle_t tag_audio_tx_handle;
static i2s_chan_handle_t tag_audio_rx_handle;
static esp_codec_dev_handle_t tag_audio_codec_handle;
static QueueHandle_t tag_audio_queue;
static SemaphoreHandle_t tag_action_mutex;
static volatile bool tag_audio_ready;
static volatile uint32_t tag_audio_generation;
static volatile uint32_t tag_clear_epoch;

static tag_group_t tag_group_for_id(int id)
{
    switch (id) {
    case 510:
    case 514:
    case 519:
    case 530:
        return TAG_GROUP_RED;
    case 533:
    case 542:
    case 547:
    case 562:
        return TAG_GROUP_GREEN;
    case 568:
    case 573:
    case 579:
    case 582:
        return TAG_GROUP_BLUE;
    default:
        return TAG_GROUP_NONE;
    }
}

static const char *tag_group_name(tag_group_t group)
{
    switch (group) {
    case TAG_GROUP_RED:
        return "red";
    case TAG_GROUP_GREEN:
        return "green";
    case TAG_GROUP_BLUE:
        return "blue";
    default:
        return "none";
    }
}

static const char *tag_input_source_name(tag_input_source_t source)
{
    switch (source) {
    case TAG_INPUT_GOLDEN:
        return "golden";
    case TAG_INPUT_EMPTY:
        return "empty";
    case TAG_INPUT_L2_TEST:
        return "l2_test";
    default:
        return "live";
    }
}

/* Every state change and GPIO write is serialized by tag_action_mutex.
 * Initialization runs before any task exists. No output snapshot escapes the lock. */
static rm_relay_t tag_relay;
static const gpio_num_t tag_relay_outputs[RM_COLOR_COUNT] = {
    TAG_SIGNAL_RED_GPIO, TAG_SIGNAL_GREEN_GPIO, TAG_SIGNAL_BLUE_GPIO,
};
static const int tag_relay_active_levels[RM_COLOR_COUNT] = {
    RM_RED_ACTIVE_LEVEL, RM_GREEN_ACTIVE_LEVEL, RM_BLUE_ACTIVE_LEVEL,
};
static const gpio_num_t tag_relay_inputs[RM_INPUT_COUNT] = {
    RM_ENTER_GPIO, RM_BATCH_DONE_GPIO,
};
static int tag_relay_last_levels[RM_COLOR_COUNT] = {-1, -1, -1};

static void tag_signal_outputs_locked(int64_t now_us)
{
    wifi_preview_lights_t snapshot = {
        .epoch = tag_relay.epoch, .enabled = tag_relay.enabled,
        .batch_active = tag_relay.batch_active, .selected = tag_relay.selected,
        .four_color = (int)rm_four_color, .sampled_us = now_us,
    };
    for (int i = 0; i < RM_COLOR_COUNT; ++i) {
        snapshot.channels[i] = (wifi_preview_lamp_t){
            .state = (uint8_t)tag_relay.colors[i].state,
            .multiplier = tag_relay.colors[i].multiplier,
            .on = rm_relay_output(&tag_relay, i, now_us),
        };
        const int level = rm_relay_level(snapshot.channels[i].on,
                                         tag_relay_active_levels[i]);
        if (tag_relay_last_levels[i] != level) {
            ESP_ERROR_CHECK(gpio_set_level(tag_relay_outputs[i], level));
            tag_relay_last_levels[i] = level;
        }
    }
    wifi_preview_publish_lights(&snapshot);
}

static void tag_signal_init(void)
{
    rm_relay_init(&tag_relay, tag_detection_enabled);
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << TAG_SIGNAL_RED_GPIO) |
                        (1ULL << TAG_SIGNAL_GREEN_GPIO) |
                        (1ULL << TAG_SIGNAL_BLUE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    /* Preload the OFF latch before enabling output, also for active-low modules. */
    tag_signal_outputs_locked(esp_timer_get_time());
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_LOGI("TAG_SIGNAL",
             "Ready: red=GPIO%d green=GPIO%d blue=GPIO%d active_levels=%d/%d/%d "
             "initial=logical_off polarity=CODE_DEFAULT_UNCONFIRMED",
             TAG_SIGNAL_RED_GPIO, TAG_SIGNAL_GREEN_GPIO, TAG_SIGNAL_BLUE_GPIO,
             RM_RED_ACTIVE_LEVEL, RM_GREEN_ACTIVE_LEVEL, RM_BLUE_ACTIVE_LEVEL);
}

static void tag_signal_latch_group(tag_group_t group, int id)
{
    const int color = (int)group - (int)TAG_GROUP_RED;
    const rm_lock_result_t result = rm_relay_lock(&tag_relay, color, id,
                                                 tag_clear_epoch, esp_timer_get_time());
    tag_signal_outputs_locked(esp_timer_get_time());
    if (result != RM_LOCK_REJECTED) {
        ESP_LOGI("TAG_SIGNAL",
                 "state=%s group=%s id=%d first_id=%d multiplier=%u order=%u record=%d",
                 result == RM_LOCK_REGISTERED ? "REGISTERED" : "DUPLICATE",
                 tag_group_name(group), id, tag_relay.colors[color].tag_id,
                 tag_relay.colors[color].multiplier, tag_relay.colors[color].order,
                 tag_relay.colors[color].state);
    }
}

static void tag_signal_clear_all(void)
{
    rm_relay_reset(&tag_relay, tag_detection_enabled);
    tag_signal_outputs_locked(esp_timer_get_time());
    ESP_LOGI("TAG_SIGNAL", "state=CLEAR_ALL outputs=logical_off records=empty batch=none");
}

static esp_err_t tag_audio_init(void)
{
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(TAG_AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, &tag_audio_tx_handle,
                                        &tag_audio_rx_handle),
                        "TAG_AUDIO", "create I2S channels failed");

    i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(TAG_AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = TAG_AUDIO_I2S_MCLK_GPIO,
            .bclk = TAG_AUDIO_I2S_BCLK_GPIO,
            .ws = TAG_AUDIO_I2S_WS_GPIO,
            .dout = TAG_AUDIO_I2S_DOUT_GPIO,
            .din = TAG_AUDIO_I2S_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    standard_config.clk_cfg.mclk_multiple = TAG_AUDIO_MCLK_MULTIPLE;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tag_audio_tx_handle, &standard_config),
                        "TAG_AUDIO", "init I2S TX failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tag_audio_rx_handle, &standard_config),
                        "TAG_AUDIO", "init I2S RX failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tag_audio_tx_handle),
                        "TAG_AUDIO", "enable I2S TX failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tag_audio_rx_handle),
                        "TAG_AUDIO", "enable I2S RX failed");

    i2c_master_bus_handle_t i2c_bus = NULL;
    const i2c_master_bus_config_t i2c_config = {
        .i2c_port = TAG_AUDIO_I2C_PORT,
        .sda_io_num = TAG_AUDIO_I2C_SDA_GPIO,
        .scl_io_num = TAG_AUDIO_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_config, &i2c_bus),
                        "TAG_AUDIO", "create I2C bus failed");

    audio_codec_i2c_cfg_t i2c_codec_config = {
        .port = TAG_AUDIO_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if =
        audio_codec_new_i2c_ctrl(&i2c_codec_config);
    ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, "TAG_AUDIO",
                        "create ES8311 I2C control failed");

    audio_codec_i2s_cfg_t i2s_codec_config = {
        .port = TAG_AUDIO_I2S_PORT,
        .rx_handle = tag_audio_rx_handle,
        .tx_handle = tag_audio_tx_handle,
    };
    const audio_codec_data_if_t *data_if =
        audio_codec_new_i2s_data(&i2s_codec_config);
    ESP_RETURN_ON_FALSE(data_if, ESP_FAIL, "TAG_AUDIO",
                        "create ES8311 I2S data interface failed");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if, ESP_FAIL, "TAG_AUDIO",
                        "create codec GPIO interface failed");

    es8311_codec_cfg_t codec_config = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode = false,
        .use_mclk = true,
        .pa_pin = TAG_AUDIO_PA_GPIO,
        .pa_reverted = false,
        .hw_gain = {
            .pa_voltage = 5.0f,
            .codec_dac_voltage = 3.3f,
        },
        .mclk_div = TAG_AUDIO_MCLK_MULTIPLE,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&codec_config);
    ESP_RETURN_ON_FALSE(codec_if, ESP_FAIL, "TAG_AUDIO", "create ES8311 codec failed");

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    tag_audio_codec_handle = esp_codec_dev_new(&device_config);
    ESP_RETURN_ON_FALSE(tag_audio_codec_handle, ESP_FAIL, "TAG_AUDIO",
                        "create codec device failed");

    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .channel = TAG_AUDIO_CHANNELS,
        .channel_mask = 0x03,
        .sample_rate = TAG_AUDIO_SAMPLE_RATE_HZ,
    };
    const int open_result = esp_codec_dev_open(tag_audio_codec_handle, &sample_info);
    ESP_RETURN_ON_FALSE(open_result == ESP_CODEC_DEV_OK, ESP_FAIL, "TAG_AUDIO",
                        "open ES8311 codec failed: %d", open_result);
    const int volume_result =
        esp_codec_dev_set_out_vol(tag_audio_codec_handle, tag_speaker_volume_percent);
    ESP_RETURN_ON_FALSE(volume_result == ESP_CODEC_DEV_OK, ESP_FAIL, "TAG_AUDIO",
                        "set speaker volume failed: %d", volume_result);
    tag_audio_ready = true;
    ESP_LOGI("TAG_AUDIO",
             "Ready: ES8311 volume=%d%% repeats=%d sample_rate=%d channels=%d",
             tag_speaker_volume_percent, TAG_AUDIO_REPEAT_COUNT,
             TAG_AUDIO_SAMPLE_RATE_HZ, TAG_AUDIO_CHANNELS);
    return ESP_OK;
}

static void tag_audio_request_group_locked(tag_group_t group)
{
    const tag_audio_request_t request = {
        .group = group,
        .generation = ++tag_audio_generation,
    };
    xQueueOverwrite(tag_audio_queue, &request);
}

static void tag_audio_cancel_locked(void)
{
    const tag_audio_request_t request = {
        .group = TAG_GROUP_NONE,
        .generation = ++tag_audio_generation,
    };
    xQueueOverwrite(tag_audio_queue, &request);
}

static const uint8_t *tag_audio_pcm_for_group(tag_group_t group, size_t *size)
{
    switch (group) {
    case TAG_GROUP_RED:
        *size = (size_t)(voice_red_pcm_end - voice_red_pcm_start);
        return voice_red_pcm_start;
    case TAG_GROUP_GREEN:
        *size = (size_t)(voice_green_pcm_end - voice_green_pcm_start);
        return voice_green_pcm_start;
    case TAG_GROUP_BLUE:
        *size = (size_t)(voice_blue_pcm_end - voice_blue_pcm_start);
        return voice_blue_pcm_start;
    default:
        *size = 0;
        return NULL;
    }
}

static bool tag_audio_generation_is_current(uint32_t generation)
{
    return generation == tag_audio_generation;
}

static bool tag_audio_play_silence(uint32_t generation, uint32_t duration_ms)
{
    int16_t silence[TAG_AUDIO_FRAME_CHUNK * TAG_AUDIO_CHANNELS] = {0};
    const size_t total_frames =
        (size_t)TAG_AUDIO_SAMPLE_RATE_HZ * duration_ms / 1000U;
    for (size_t offset = 0; offset < total_frames; offset += TAG_AUDIO_FRAME_CHUNK) {
        if (!tag_audio_generation_is_current(generation)) {
            return false;
        }
        size_t frames = total_frames - offset;
        if (frames > TAG_AUDIO_FRAME_CHUNK) {
            frames = TAG_AUDIO_FRAME_CHUNK;
        }
        const int result = esp_codec_dev_write(
            tag_audio_codec_handle, silence,
            frames * TAG_AUDIO_CHANNELS * sizeof(int16_t));
        if (result != ESP_CODEC_DEV_OK) {
            ESP_LOGE("TAG_AUDIO", "silence write failed: %d", result);
            return false;
        }
    }
    return true;
}

static void tag_audio_task(void *arg)
{
    tag_audio_request_t request;
    while (true) {
        if (xQueueReceive(tag_audio_queue, &request, portMAX_DELAY) != pdPASS ||
            request.group == TAG_GROUP_NONE ||
            !tag_audio_generation_is_current(request.generation)) {
            continue;
        }
        if (!tag_audio_ready) {
            ESP_LOGE("TAG_AUDIO", "Playback request skipped: audio is not ready");
            continue;
        }

        size_t pcm_size = 0;
        const uint8_t *pcm = tag_audio_pcm_for_group(request.group, &pcm_size);
        if (!pcm || pcm_size == 0) {
            ESP_LOGE("TAG_AUDIO", "Playback request has no PCM for group=%s",
                     tag_group_name(request.group));
            continue;
        }

        ESP_LOGI("TAG_AUDIO",
                 "Playback start group=%s bytes=%u repeat=%d volume=%d%% generation=%" PRIu32,
                 tag_group_name(request.group), (unsigned)pcm_size,
                 TAG_AUDIO_REPEAT_COUNT, tag_speaker_volume_percent,
                 request.generation);
        bool cancelled = false;
        bool write_failed = false;
        for (int repeat = 0; repeat < TAG_AUDIO_REPEAT_COUNT; ++repeat) {
            for (size_t offset = 0; offset < pcm_size;
                 offset += TAG_AUDIO_FRAME_CHUNK * TAG_AUDIO_CHANNELS * sizeof(int16_t)) {
                if (!tag_audio_generation_is_current(request.generation)) {
                    cancelled = true;
                    break;
                }
                size_t bytes = pcm_size - offset;
                const size_t chunk_bytes =
                    TAG_AUDIO_FRAME_CHUNK * TAG_AUDIO_CHANNELS * sizeof(int16_t);
                if (bytes > chunk_bytes) {
                    bytes = chunk_bytes;
                }
                const int result =
                    esp_codec_dev_write(tag_audio_codec_handle, (void *)(pcm + offset), bytes);
                if (result != ESP_CODEC_DEV_OK) {
                    ESP_LOGE("TAG_AUDIO", "PCM write failed group=%s repeat=%d result=%d",
                             tag_group_name(request.group), repeat + 1, result);
                    write_failed = true;
                    break;
                }
            }
            if (cancelled || write_failed) {
                break;
            }
            if (repeat + 1 < TAG_AUDIO_REPEAT_COUNT &&
                !tag_audio_play_silence(request.generation, TAG_AUDIO_REPEAT_GAP_MS)) {
                cancelled = !tag_audio_generation_is_current(request.generation);
                write_failed = !cancelled;
                break;
            }
        }
        if (cancelled) {
            ESP_LOGI("TAG_AUDIO", "Playback cancelled group=%s generation=%" PRIu32,
                     tag_group_name(request.group), request.generation);
        } else if (!write_failed && tag_audio_generation_is_current(request.generation)) {
            ESP_LOGI("TAG_AUDIO", "Playback complete group=%s repeats=%d",
                     tag_group_name(request.group), TAG_AUDIO_REPEAT_COUNT);
        }
    }
}

static const char *tag_profile_name(tag_profile_t profile)
{
    switch (profile) {
    case TAG_PROFILE_D2_REFINE:
        return "p2_d2_refine";
    case TAG_PROFILE_D2_FAST:
        return "p3_d2_fast";
    case TAG_PROFILE_D1_FAST:
        return "p4_d1_fast";
    default:
        return "p1_d1_refine";
    }
}

static void tag_profile_apply(apriltag_detector_t *detector, tag_profile_t profile)
{
    detector->quad_decimate =
        (profile == TAG_PROFILE_D2_REFINE || profile == TAG_PROFILE_D2_FAST) ? 2.0f : 1.0f;
    detector->refine_edges =
        (profile == TAG_PROFILE_D1_REFINE || profile == TAG_PROFILE_D2_REFINE);
}

typedef struct {
    int id;
    float margin;
    float center_x;
    float center_y;
    float bbox_width;
    float bbox_height;
    float corners[4][2];
} tag_candidate_t;

static float tag_polygon_area(const float corners[4][2])
{
    float twice_area = 0;
    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) % 4;
        twice_area += corners[i][0] * corners[next][1] -
                      corners[next][0] * corners[i][1];
    }
    return fabsf(twice_area) * 0.5f;
}

static float tag_visible_ratio(const tag_candidate_t *candidate,
                               float min_x, float min_y, float max_x, float max_y)
{
    const float full_area = tag_polygon_area(candidate->corners);
    if (full_area <= 1.0f) {
        return 0;
    }
    float clipped[4][2];
    for (int i = 0; i < 4; ++i) {
        clipped[i][0] = fminf(max_x, fmaxf(min_x, candidate->corners[i][0]));
        clipped[i][1] = fminf(max_y, fmaxf(min_y, candidate->corners[i][1]));
    }
    return tag_polygon_area(clipped) / full_area;
}

static bool tag_select_candidate(zarray_t *detections, float min_margin,
                                 float image_min_x, float image_min_y,
                                 float image_max_x, float image_max_y,
                                 const char *stage, tag_candidate_t *selected)
{
    bool found = false;
    bool conflict = false;
    memset(selected, 0, sizeof(*selected));

    const int count = zarray_size(detections);
    for (int i = 0; i < count; ++i) {
        apriltag_detection_t *detection;
        zarray_get(detections, i, &detection);
        ESP_LOGI("TAG36H11",
                 "%s id=%d hamming=%d margin=%.2f center=(%.1f,%.1f) "
                 "corners=(%.1f,%.1f)(%.1f,%.1f)(%.1f,%.1f)(%.1f,%.1f)",
                 stage, detection->id, detection->hamming,
                 detection->decision_margin, detection->c[0], detection->c[1],
                 detection->p[0][0], detection->p[0][1],
                 detection->p[1][0], detection->p[1][1],
                 detection->p[2][0], detection->p[2][1],
                 detection->p[3][0], detection->p[3][1]);

        tag_candidate_t candidate = {
            .id = detection->id,
            .margin = detection->decision_margin,
            .center_x = detection->c[0],
            .center_y = detection->c[1],
        };
        float bbox_min_x = detection->p[0][0];
        float bbox_max_x = detection->p[0][0];
        float bbox_min_y = detection->p[0][1];
        float bbox_max_y = detection->p[0][1];
        for (int corner = 0; corner < 4; ++corner) {
            candidate.corners[corner][0] = detection->p[corner][0];
            candidate.corners[corner][1] = detection->p[corner][1];
            bbox_min_x = fminf(bbox_min_x, detection->p[corner][0]);
            bbox_max_x = fmaxf(bbox_max_x, detection->p[corner][0]);
            bbox_min_y = fminf(bbox_min_y, detection->p[corner][1]);
            bbox_max_y = fmaxf(bbox_max_y, detection->p[corner][1]);
        }
        candidate.bbox_width = bbox_max_x - bbox_min_x;
        candidate.bbox_height = bbox_max_y - bbox_min_y;

        if (tag_group_for_id(detection->id) == TAG_GROUP_NONE ||
            detection->hamming != 0 ||
            detection->decision_margin < min_margin ||
            candidate.bbox_width < TAG_MIN_BOUNDING_PIXELS ||
            candidate.bbox_height < TAG_MIN_BOUNDING_PIXELS) {
            continue;
        }

        const float visible_ratio =
            tag_visible_ratio(&candidate, image_min_x, image_min_y, image_max_x, image_max_y);
        if (visible_ratio < TAG_MIN_VISIBLE_RATIO) {
            ESP_LOGW("TAG36H11",
                     "%s reject_truncated id=%d visible=%.3f required=%.3f",
                     stage, detection->id, visible_ratio, TAG_MIN_VISIBLE_RATIO);
            continue;
        }

        if (!found) {
            *selected = candidate;
            found = true;
        } else {
            if (selected->id != candidate.id) {
                conflict = true;
            }
            if (candidate.margin > selected->margin) {
                *selected = candidate;
            }
        }
    }

    if (conflict) {
        ESP_LOGW("TAG36H11", "%s reject_conflict", stage);
        return false;
    }
    return found;
}

static bool tag_candidates_agree(const tag_candidate_t *fast,
                                 const tag_candidate_t *refined)
{
    if (fast->id == 0 || fast->id != refined->id) {
        return false;
    }
    const float reference_size =
        fmaxf(fmaxf(fast->bbox_width, fast->bbox_height),
              fmaxf(refined->bbox_width, refined->bbox_height));
    const float dx = fast->center_x - refined->center_x;
    const float dy = fast->center_y - refined->center_y;
    const float center_distance = sqrtf(dx * dx + dy * dy);
    const float width_delta = fabsf(fast->bbox_width - refined->bbox_width);
    const float height_delta = fabsf(fast->bbox_height - refined->bbox_height);
    return reference_size > 0 &&
           center_distance <= reference_size * 0.10f &&
           width_delta <= fmaxf(fast->bbox_width, refined->bbox_width) * 0.25f &&
           height_delta <= fmaxf(fast->bbox_height, refined->bbox_height) * 0.25f;
}

static void tag_prepare_l2_image(void)
{
    const int width = 640;
    const int height = 480;
    const int stride = width + 1;
    const int radius = TAG_L2_PREPARE_WINDOW / 2;
    uint32_t *integral = tag_l2_integral;

    integral[0] = 0;
    for (int x = 0; x < width; ++x) {
        integral[x + 1] = 0;
    }
    for (int y = 0; y < height; ++y) {
        uint32_t row_sum = 0;
        integral[(y + 1) * stride] = 0;
        for (int x = 0; x < width; ++x) {
            row_sum += tag_gray_full[y * width + x];
            integral[(y + 1) * stride + x + 1] =
                integral[y * stride + x + 1] + row_sum;
        }
    }

    for (int y = 0; y < height; ++y) {
        const int y0 = max(0, y - radius);
        const int y1 = y + radius + 1 < height ? y + radius + 1 : height;
        for (int x = 0; x < width; ++x) {
            const int x0 = max(0, x - radius);
            const int x1 = x + radius + 1 < width ? x + radius + 1 : width;
            const uint32_t sum =
                integral[y1 * stride + x1] +
                integral[y0 * stride + x0] -
                integral[y0 * stride + x1] -
                integral[y1 * stride + x0];
            const int area = (y1 - y0) * (x1 - x0);
            const int local_mean = (int)(sum / (uint32_t)area);
            const int threshold = local_mean - TAG_L2_PREPARE_BIAS;
            const uint8_t value = tag_gray_full[y * width + x];
            tag_gray_l2[(y + TAG_L2_PAD_PIXELS) * TAG_L2_IMAGE_WIDTH +
                        x + TAG_L2_PAD_PIXELS] = value > threshold ? 255 : 0;
        }
    }

    /* Mirror the frame edge into the synthetic quiet zone. This keeps a tag
     * that is one or two pixels outside the camera boundary decodable without
     * inventing a large continuation of the tag. */
    for (int y = 0; y < TAG_L2_IMAGE_HEIGHT; ++y) {
        int source_y = y - TAG_L2_PAD_PIXELS;
        source_y = source_y < 0 ? -source_y - 1 :
                   (source_y >= height ? height * 2 - source_y - 1 : source_y);
        for (int x = 0; x < TAG_L2_IMAGE_WIDTH; ++x) {
            int source_x = x - TAG_L2_PAD_PIXELS;
            source_x = source_x < 0 ? -source_x - 1 :
                       (source_x >= width ? width * 2 - source_x - 1 : source_x);
            tag_gray_l2[y * TAG_L2_IMAGE_WIDTH + x] =
                tag_gray_l2[(source_y + TAG_L2_PAD_PIXELS) * TAG_L2_IMAGE_WIDTH +
                            source_x + TAG_L2_PAD_PIXELS];
        }
    }
}

static bool tag_commit_lock(tag_group_t group, int id, uint32_t expected_clear_epoch)
{
    xSemaphoreTake(tag_action_mutex, portMAX_DELAY);
    const bool paused = !tag_detection_enabled;
    const bool clear_raced = tag_clear_epoch != expected_clear_epoch;
    if (!paused && !clear_raced) {
        tag_signal_latch_group(group, id);
        tag_audio_request_group_locked(group);
    }
    xSemaphoreGive(tag_action_mutex);
    if (paused || clear_raced) {
        ESP_LOGW("TAG_EVENT",
                 "state=LOCK_SUPPRESSED group=%s id=%d reason=%s expected_epoch=%" PRIu32
                 " current_epoch=%" PRIu32,
                 tag_group_name(group), id,
                 paused ? "recognition_paused" : "close_raced",
                 expected_clear_epoch, tag_clear_epoch);
    }
    return !paused && !clear_raced;
}

static void tag_request_clear(const char *source)
{
    xSemaphoreTake(tag_action_mutex, portMAX_DELAY);
    tag_audio_cancel_locked();
    tag_signal_clear_all();
    const uint32_t epoch = ++tag_clear_epoch;
    xSemaphoreGive(tag_action_mutex);
    ESP_LOGI("TAG_EVENT",
             "state=CLOSE source=%s audio=cancelled signals=logical_off rearm_epoch=%" PRIu32,
             source, epoch);
}

static void tag_set_detection_enabled(bool enabled, const char *source)
{
    xSemaphoreTake(tag_action_mutex, portMAX_DELAY);
    tag_detection_enabled = enabled;
    tag_audio_cancel_locked();
    tag_signal_clear_all();
    const uint32_t epoch = ++tag_clear_epoch;
    xSemaphoreGive(tag_action_mutex);
    ESP_LOGI("TAG_EVENT",
             "state=RECOGNITION_%s source=%s audio=cancelled signals=logical_off epoch=%" PRIu32,
             enabled ? "RESUMED" : "PAUSED", source, epoch);
    printf("CTRL_ACK recognition=%s epoch=%" PRIu32 "\n", enabled ? "on" : "off", epoch);
    fflush(stdout);
}

static void tag_report_detection_status(void)
{
    xSemaphoreTake(tag_action_mutex, portMAX_DELAY);
    const bool enabled = tag_detection_enabled;
    const uint32_t epoch = tag_clear_epoch;
    xSemaphoreGive(tag_action_mutex);
    printf("CTRL_STATUS recognition=%s epoch=%" PRIu32 "\n", enabled ? "on" : "off", epoch);
    fflush(stdout);
}

#if RM_UART_EVENT_INJECTION
static void tag_relay_inject_event(rm_event_t event)
{
    xSemaphoreTake(tag_action_mutex, portMAX_DELAY);
    const bool accepted = rm_relay_event(&tag_relay, event, tag_clear_epoch,
                                          esp_timer_get_time());
    tag_signal_outputs_locked(esp_timer_get_time());
    xSemaphoreGive(tag_action_mutex);
    ESP_LOGI("TAG_RELAY", "source=uart_debug event=%d accepted=%d", event, accepted);
}
#endif

static void tag_relay_poll_inputs(void)
{
    /* GPIO22 is processed first by the caller. Simultaneous new inputs have
     * deterministic ENTER -> DONE order. Samples and writes stay
     * inside the action mutex; a clear cannot be followed by an old output. */
    const rm_event_t order[RM_INPUT_COUNT] = {RM_ENTER, RM_BATCH_DONE};
    bool accepted[RM_INPUT_COUNT] = {false};
    xSemaphoreTake(tag_action_mutex, portMAX_DELAY);
    const int64_t now_us = esp_timer_get_time();
    for (int i = 0; i < RM_INPUT_COUNT; ++i) {
        const rm_event_t event = order[i];
        accepted[event] = rm_relay_sample(&tag_relay, event,
                            gpio_get_level(tag_relay_inputs[event]) == 1,
                            tag_clear_epoch, now_us);
    }
    tag_signal_outputs_locked(now_us);
    xSemaphoreGive(tag_action_mutex);
    for (int i = 0; i < RM_INPUT_COUNT; ++i) {
        if (accepted[i]) {
            ESP_LOGI("TAG_RELAY", "source=gpio event=%d gpio=%d accepted=1", i,
                     tag_relay_inputs[i]);
        }
    }
}

static void tag_close_input_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << TAG_SIGNAL_CLOSE_GPIO) |
                        (1ULL << RM_ENTER_GPIO) |
                        (1ULL << RM_BATCH_DONE_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_LOGI("TAG_CLOSE",
             "Ready: input=GPIO%d active=high idle=low pull=down debounce=%dms",
             TAG_SIGNAL_CLOSE_GPIO, TAG_CLOSE_DEBOUNCE_MS);
    ESP_LOGI("TAG_RELAY", "Ready: ENTER=GPIO%d DONE=GPIO%d four_color=%d "
             "active=high debounce_high_low=20ms poll=5ms debug_injection=%d",
             RM_ENTER_GPIO, RM_BATCH_DONE_GPIO, (int)rm_four_color, RM_UART_EVENT_INJECTION);
}

static void tag_close_input_task(void *arg)
{
    bool pulse_active = false;
    int64_t high_since_us = 0;
    int64_t pulse_started_us = 0;
    uint32_t pulse_count = 0;
    while (true) {
        const int level = gpio_get_level(TAG_SIGNAL_CLOSE_GPIO);
        const int64_t now_us = esp_timer_get_time();
        if (!pulse_active) {
            if (level == TAG_CLOSE_ACTIVE_LEVEL) {
                if (high_since_us == 0) {
                    high_since_us = now_us;
                } else if (now_us - high_since_us >= TAG_CLOSE_DEBOUNCE_MS * 1000LL) {
                    pulse_active = true;
                    pulse_started_us = high_since_us;
                    tag_request_clear("gpio22_high");
                    ESP_LOGI("TAG_CLOSE", "pulse_accepted=%" PRIu32 " gpio=%d",
                             ++pulse_count, TAG_SIGNAL_CLOSE_GPIO);
                }
            } else {
                high_since_us = 0;
            }
        } else if (level != TAG_CLOSE_ACTIVE_LEVEL) {
            const int64_t duration_ms = (now_us - pulse_started_us) / 1000;
            pulse_active = false;
            high_since_us = 0;
            ESP_LOGI("TAG_CLOSE", "pulse_released gpio=%d high_ms=%lld",
                     TAG_SIGNAL_CLOSE_GPIO, duration_ms);
        }
        tag_relay_poll_inputs();
        vTaskDelay(pdMS_TO_TICKS(TAG_CLOSE_POLL_MS));
    }
}

static void tag_detection_task(void *arg)
{
    apriltag_family_t *family = tag36h11_create();
    apriltag_detector_t *detector = apriltag_detector_create();
    assert(family && detector);
    apriltag_detector_add_family_bits(detector, family, 0);
    detector->nthreads = 1;
    detector->quad_sigma = 0;
    detector->debug = false;
    image_u8_t image = {.width = 320, .height = 240, .stride = 320, .buf = tag_gray};
    image_u8_t l2_image = {
        .width = TAG_L2_IMAGE_WIDTH,
        .height = TAG_L2_IMAGE_HEIGHT,
        .stride = TAG_L2_IMAGE_WIDTH,
        .buf = tag_gray_l2,
    };
    unsigned sequence = 0;
    int fast_history[TAG_CONFIRM_WINDOW] = {0};
    int fast_history_index = 0;
    int l2_history[TAG_CONFIRM_WINDOW] = {0};
    int l2_history_index = 0;
    int l1_miss_streak = 0;
    int l2_miss_streak = 0;
    bool l2_active = false;
    int64_t l2_retry_after_us = 0;
    int locked_id = 0;
    int64_t lost_since_us = 0;
    uint32_t local_clear_epoch = tag_clear_epoch;
    bool rearm_wait_empty = false;
    bool rearm_blocked_logged = false;
    tag_profile_t active_profile = TAG_PROFILE_D2_FAST;
    tag_profile_apply(detector, active_profile);
    ESP_LOGI("TAG36H11",
             "Ready: L1=320x240 profile=%s; L2=adaptive-threshold %dx%d "
             "d2-fast+d2-refine hamming=0",
             tag_profile_name(active_profile),
             TAG_L2_IMAGE_WIDTH, TAG_L2_IMAGE_HEIGHT);
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!tag_detection_enabled) {
            tag_full_frame_requested = false;
            tag_full_frame_ready = false;
            xSemaphoreGive(tag_buffer_free);
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        const uint32_t observed_clear_epoch = tag_clear_epoch;
        if (observed_clear_epoch != local_clear_epoch) {
            local_clear_epoch = observed_clear_epoch;
            memset(fast_history, 0, sizeof(fast_history));
            fast_history_index = 0;
            memset(l2_history, 0, sizeof(l2_history));
            l2_history_index = 0;
            l1_miss_streak = 0;
            l2_miss_streak = 0;
            l2_active = false;
            l2_retry_after_us = 0;
            locked_id = 0;
            lost_since_us = 0;
            rearm_wait_empty = true;
            rearm_blocked_logged = false;
            ESP_LOGI("TAG_EVENT",
                     "state=REARM_WAIT_EMPTY epoch=%" PRIu32
                     " rule=first_no_candidate_frame_rearms",
                     local_clear_epoch);
        }
        const uint32_t detection_clear_epoch = local_clear_epoch;
        const tag_profile_t requested_profile = requested_tag_profile;
        if (requested_profile != active_profile) {
            active_profile = requested_profile;
            tag_profile_apply(detector, active_profile);
            memset(fast_history, 0, sizeof(fast_history));
            fast_history_index = 0;
            memset(l2_history, 0, sizeof(l2_history));
            l2_history_index = 0;
            l1_miss_streak = 0;
            l2_miss_streak = 0;
            l2_active = false;
            l2_retry_after_us = 0;
            locked_id = 0;
            lost_since_us = 0;
            ESP_LOGI("TAG36H11", "PROFILE_CHANGED profile=%s", tag_profile_name(active_profile));
        }
        if (!tag_l2_enabled && l2_active) {
            l2_active = false;
            l2_miss_streak = 0;
            memset(l2_history, 0, sizeof(l2_history));
            l2_history_index = 0;
        }

        const int64_t started = esp_timer_get_time();
        const tag_input_source_t input_source = tag_input_source;
        tag_input_source = TAG_INPUT_LIVE;
        const bool full_frame_ready =
            (input_source == TAG_INPUT_LIVE ||
             input_source == TAG_INPUT_L2_TEST) && tag_full_frame_ready;
        tag_full_frame_ready = false;

        zarray_t *detections = apriltag_detector_detect(detector, &image);
        const int count = zarray_size(detections);
        ESP_LOGI("TAG36H11", "seq=%u profile=%s input=%s count=%d detect_ms=%.1f stack_min_free=%u",
                 ++sequence, tag_profile_name(active_profile),
                 tag_input_source_name(input_source), count,
                 (esp_timer_get_time() - started) / 1000.0,
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        tag_candidate_t fast_candidate;
        const bool fast_found =
            tag_select_candidate(detections, TAG_MIN_MARGIN, 0, 0, 319, 239,
                                 "L1", &fast_candidate);
        apriltag_detections_destroy(detections);

        tag_candidate_t candidate = {0};
        bool candidate_from_l2 = false;
        if (fast_found) {
            candidate = fast_candidate;
            l1_miss_streak = 0;
            l2_miss_streak = 0;
            l2_active = false;
            l2_retry_after_us = 0;
            memset(l2_history, 0, sizeof(l2_history));
            l2_history_index = 0;
        } else if ((input_source == TAG_INPUT_LIVE ||
                    input_source == TAG_INPUT_L2_TEST) && tag_l2_enabled) {
            ++l1_miss_streak;
            const int64_t now_us = esp_timer_get_time();
            if (!l2_active && l1_miss_streak >= TAG_L2_START_MISSES &&
                now_us >= l2_retry_after_us) {
                l2_active = true;
                l2_miss_streak = 0;
                memset(l2_history, 0, sizeof(l2_history));
                l2_history_index = 0;
                ESP_LOGI("TAG_ENHANCE",
                         "state=START misses=%d retry_ms=%d",
                         l1_miss_streak, TAG_L2_BACKOFF_MS);
            }
        } else {
            l1_miss_streak = 0;
        }

        if (!fast_found && tag_l2_enabled && l2_active && full_frame_ready) {
            const int64_t l2_started = esp_timer_get_time();
            tag_prepare_l2_image();

            tag_candidate_t l2_fast;
            tag_profile_apply(detector, TAG_PROFILE_D2_FAST);
            zarray_t *l2_fast_detections = apriltag_detector_detect(detector, &l2_image);
            const int l2_fast_count = zarray_size(l2_fast_detections);
            const bool l2_fast_found =
                tag_select_candidate(
                    l2_fast_detections, TAG_L2_PREFILTER_MARGIN,
                    TAG_L2_PAD_PIXELS, TAG_L2_PAD_PIXELS,
                    TAG_L2_PAD_PIXELS + 639, TAG_L2_PAD_PIXELS + 479,
                    "L2_FAST", &l2_fast);
            apriltag_detections_destroy(l2_fast_detections);

            bool l2_found = false;
            float l2_margin = 0;
            if (l2_fast_found) {
                tag_candidate_t l2_refined;
                tag_profile_apply(detector, TAG_PROFILE_D2_REFINE);
                zarray_t *l2_refined_detections =
                    apriltag_detector_detect(detector, &l2_image);
                const int l2_refined_count = zarray_size(l2_refined_detections);
                const bool l2_refined_found =
                    tag_select_candidate(
                        l2_refined_detections, TAG_MIN_MARGIN,
                        TAG_L2_PAD_PIXELS, TAG_L2_PAD_PIXELS,
                        TAG_L2_PAD_PIXELS + 639, TAG_L2_PAD_PIXELS + 479,
                        "L2_REFINE", &l2_refined);
                apriltag_detections_destroy(l2_refined_detections);
                if (l2_refined_found &&
                    tag_candidates_agree(&l2_fast, &l2_refined)) {
                    candidate = l2_refined;
                    candidate.margin = fminf(l2_fast.margin, l2_refined.margin);
                    candidate_from_l2 = true;
                    l2_found = true;
                    l2_margin = candidate.margin;
                }
                ESP_LOGI("TAG_ENHANCE",
                         "profiles fast=%d refine=%d count=%d/%d agree=%d "
                         "id=%d margin=%.2f total_ms=%.1f",
                         l2_fast_found, l2_refined_found,
                         l2_fast_count, l2_refined_count,
                         l2_found, l2_found ? candidate.id : 0,
                         l2_margin,
                         (esp_timer_get_time() - l2_started) / 1000.0);
            } else {
                ESP_LOGI("TAG_ENHANCE",
                         "profiles fast=0 count=%d total_ms=%.1f",
                         l2_fast_count,
                         (esp_timer_get_time() - l2_started) / 1000.0);
            }
            tag_profile_apply(detector, active_profile);

            if (l2_found) {
                l2_miss_streak = 0;
            } else {
                ++l2_miss_streak;
                if (l2_miss_streak >= TAG_L2_MAX_MISSES) {
                    l2_active = false;
                    l2_retry_after_us =
                        esp_timer_get_time() + TAG_L2_BACKOFF_MS * 1000LL;
                    memset(l2_history, 0, sizeof(l2_history));
                    l2_history_index = 0;
                    ESP_LOGW("TAG_ENHANCE",
                             "state=BACKOFF misses=%d delay_ms=%d",
                             l2_miss_streak, TAG_L2_BACKOFF_MS);
                }
            }
        }

        const int candidate_id = candidate.id;
        const float candidate_margin = candidate.margin;
        fast_history[fast_history_index] = fast_found ? fast_candidate.id : 0;
        fast_history_index = (fast_history_index + 1) % TAG_CONFIRM_WINDOW;
        if (candidate_from_l2) {
            l2_history[l2_history_index] = candidate.id;
            l2_history_index = (l2_history_index + 1) % TAG_CONFIRM_WINDOW;
        } else if (l2_active) {
            l2_history[l2_history_index] = 0;
            l2_history_index = (l2_history_index + 1) % TAG_CONFIRM_WINDOW;
        }

        if (rearm_wait_empty) {
            if (candidate_id == 0) {
                rearm_wait_empty = false;
                rearm_blocked_logged = false;
                memset(fast_history, 0, sizeof(fast_history));
                fast_history_index = 0;
                memset(l2_history, 0, sizeof(l2_history));
                l2_history_index = 0;
                ESP_LOGI("TAG_EVENT", "state=REARM_READY epoch=%" PRIu32,
                         local_clear_epoch);
            } else {
                memset(fast_history, 0, sizeof(fast_history));
                fast_history_index = 0;
                memset(l2_history, 0, sizeof(l2_history));
                l2_history_index = 0;
                if (!rearm_blocked_logged) {
                    ESP_LOGI("TAG_EVENT",
                             "state=REARM_BLOCKED epoch=%" PRIu32
                             " reason=tag_still_visible group=%s id=%d",
                             local_clear_epoch, tag_group_name(tag_group_for_id(candidate_id)),
                             candidate_id);
                    rearm_blocked_logged = true;
                }
            }
        } else {
            if (locked_id != 0) {
                if (candidate_id == locked_id) {
                    lost_since_us = 0;
                } else {
                    const int64_t now_us = esp_timer_get_time();
                    if (lost_since_us == 0) {
                        lost_since_us = now_us;
                    } else if (now_us - lost_since_us >= TAG_LOST_TIMEOUT_US) {
                        ESP_LOGW("TAG_EVENT", "state=LOST group=%s id=%d timeout_ms=%lld seq=%u",
                                 tag_group_name(tag_group_for_id(locked_id)), locked_id,
                                 TAG_LOST_TIMEOUT_US / 1000, sequence);
                        locked_id = 0;
                        lost_since_us = 0;
                        memset(fast_history, 0, sizeof(fast_history));
                        fast_history_index = 0;
                        memset(l2_history, 0, sizeof(l2_history));
                        l2_history_index = 0;
                    }
                }
            } else if (candidate_id != 0) {
                int hits = 0;
                if (candidate_from_l2) {
                    for (int i = 0; i < TAG_CONFIRM_WINDOW; ++i) {
                        if (l2_history[i] == candidate_id) {
                            hits++;
                        }
                    }
                } else {
                    for (int i = 0; i < TAG_CONFIRM_WINDOW; ++i) {
                        if (fast_history[i] == candidate_id) {
                            hits++;
                        }
                    }
                }
                const int required_hits =
                    candidate_from_l2 ? TAG_L2_CONFIRM_HITS : TAG_CONFIRM_HITS;
                if (hits >= required_hits) {
                    const tag_group_t locked_group = tag_group_for_id(candidate_id);
                    if (tag_commit_lock(locked_group, candidate_id, detection_clear_epoch)) {
                        locked_id = candidate_id;
                        lost_since_us = 0;
                        ESP_LOGI("TAG_EVENT",
                                 "state=LOCK group=%s id=%d hamming=0 margin=%.2f "
                                 "source=%s hits=%d/5 seq=%u",
                                 tag_group_name(locked_group), locked_id,
                                 candidate_margin,
                                 candidate_from_l2 ? "L2" : "L1",
                                 hits, sequence);
                    } else {
                        locked_id = 0;
                        lost_since_us = 0;
                        memset(fast_history, 0, sizeof(fast_history));
                        fast_history_index = 0;
                        memset(l2_history, 0, sizeof(l2_history));
                        l2_history_index = 0;
                    }
                }
            }
        }
        tag_full_frame_requested =
            tag_l2_enabled && input_source == TAG_INPUT_LIVE && l2_active;
        xSemaphoreGive(tag_buffer_free);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void tag_uart_task(void *arg)
{
    uint8_t command;
    while (true) {
        if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &command, 1,
                            pdMS_TO_TICKS(100)) != 1) {
            continue;
        }
        if (command == 's') {
            tag_preview_requested = true;
            ESP_LOGI("TAG36H11", "Preview requested; waiting for the next camera frame");
        } else if (command == 'g') {
            if (!tag_detection_enabled) {
                ESP_LOGW("TAG36H11", "Golden test skipped: recognition is paused");
                continue;
            }
            if (xSemaphoreTake(tag_buffer_free, pdMS_TO_TICKS(1000)) != pdTRUE) {
                ESP_LOGW("TAG36H11", "Golden test skipped: detector buffer busy");
                continue;
            }
            memcpy(tag_gray, golden_tag_530_gray, sizeof(golden_tag_530_gray));
            tag_input_source = TAG_INPUT_GOLDEN;
            ESP_LOGI("TAG36H11", "Queued golden test image: bytes=%u crc=%08" PRIx32,
                     (unsigned)sizeof(golden_tag_530_gray),
                     esp_rom_crc32_le(0, golden_tag_530_gray, sizeof(golden_tag_530_gray)));
            xTaskNotifyGive(tag_task_handle);
        } else if (command == 'e') {
            if (!tag_detection_enabled) {
                ESP_LOGW("TAG36H11", "Empty test skipped: recognition is paused");
                continue;
            }
            if (xSemaphoreTake(tag_buffer_free, pdMS_TO_TICKS(1000)) != pdTRUE) {
                ESP_LOGW("TAG36H11", "Empty test skipped: detector buffer busy");
                continue;
            }
            memset(tag_gray, 0, 320 * 240);
            tag_input_source = TAG_INPUT_EMPTY;
            ESP_LOGI("TAG36H11", "Queued empty test image: bytes=%u", 320 * 240);
            xTaskNotifyGive(tag_task_handle);
        } else if (command == 't') {
            if (!tag_detection_enabled || !tag_l2_enabled) {
                ESP_LOGW("TAG_ENHANCE",
                         "L2 synthetic test skipped recognition=%d l2=%d",
                         tag_detection_enabled, tag_l2_enabled);
                continue;
            }
            if (xSemaphoreTake(tag_buffer_free, pdMS_TO_TICKS(3000)) != pdTRUE) {
                ESP_LOGW("TAG_ENHANCE", "L2 synthetic test skipped: detector busy");
                continue;
            }
            memset(tag_gray, 0, 320 * 240);
            memset(tag_gray_full, 238, 640 * 480);
            for (int y = 0; y < 240; ++y) {
                for (int x = 0; x < 320; ++x) {
                    tag_gray_full[(y + 120) * 640 + x + 160] =
                        golden_tag_530_gray[y * 320 + x];
                }
            }
            tag_full_frame_ready = true;
            tag_full_frame_requested = false;
            tag_input_source = TAG_INPUT_L2_TEST;
            ESP_LOGI("TAG_ENHANCE",
                     "Queued synthetic L2 frame bytes=%u",
                     (unsigned)(640 * 480));
            xTaskNotifyGive(tag_task_handle);
        } else if (command >= '1' && command <= '4') {
            requested_tag_profile = (tag_profile_t)(command - '0');
            ESP_LOGI("TAG36H11", "Profile requested: %s",
                     tag_profile_name(requested_tag_profile));
        } else if (command == 'l') {
            tag_l2_enabled = !tag_l2_enabled;
            tag_full_frame_requested = false;
            tag_full_frame_ready = false;
            ESP_LOGI("TAG_ENHANCE", "state=%s source=uart_l",
                     tag_l2_enabled ? "ENABLED" : "DISABLED");
        } else if (command == 'r' || command == 'c') {
            tag_request_clear(command == 'r' ? "uart_r" : "uart_c");
        } else if (command == 'p') {
            tag_set_detection_enabled(false, "uart_pause");
        } else if (command == 'u') {
            tag_set_detection_enabled(true, "uart_resume");
        } else if (command == 'q') {
            tag_report_detection_status();
#if RM_UART_EVENT_INJECTION
        } else if (command == 'E' || command == 'D') {
            tag_relay_inject_event(command == 'E' ? RM_ENTER : RM_BATCH_DONE);
#endif
        }
    }
}

static QueueHandle_t rx_frames_queue[EXAMPLE_NUMBER_OF_STREAMS];
static int device_count = 0;
static volatile bool dev_connected = false;
static bool frame_handling_tasks_started = false;
static uvc_host_frame_info_t *frame_info_list[EXAMPLE_NUMBER_OF_STREAMS] = {NULL};
static size_t frame_info_list_size[EXAMPLE_NUMBER_OF_STREAMS] = {0};

static const char *FORMAT_STR[] = {
    "FORMAT_UNDEFINED",
    "FORMAT_MJPEG",
    "FORMAT_YUY2",
    "FORMAT_H264",
    "FORMAT_H265",
};

bool frame_callback(const uvc_host_frame_t *frame, void *user_ctx)
{
    assert(frame);
    assert(user_ctx);
    QueueHandle_t frame_q = *((QueueHandle_t *)user_ctx);
    ESP_LOGD(TAG, "Frame callback! data len: %d", frame->data_len);
    BaseType_t result = xQueueSendToBack(frame_q, &frame, 0);
    if (pdPASS != result) {
        ESP_LOGW(TAG, "Queue full, losing frame"); // This should never happen. We allocated queue with the same size as EXAMPLE_FRAME_COUNT
        return true; // We will not process this frame, return it immediately
    }
    return false; // We only passed the frame to Queue, so we must return false and call uvc_host_frame_return() later
}

static void stream_callback(const uvc_host_stream_event_data_t *event, void *user_ctx)
{
    switch (event->type) {
    case UVC_HOST_TRANSFER_ERROR:
        ESP_LOGE(TAG, "USB error has occurred, err_no = %i", event->transfer_error.error);
        break;
    case UVC_HOST_DEVICE_DISCONNECTED:
        ESP_LOGI(TAG, "Device suddenly disconnected");
        dev_connected = false;
        device_count--;
        // The frame task returns queued buffers before closing its stream.
        break;
    case UVC_HOST_FRAME_BUFFER_OVERFLOW:
        ESP_LOGW(TAG, "Frame buffer overflow");
        break;
    case UVC_HOST_FRAME_BUFFER_UNDERFLOW:
        ESP_LOGW(TAG, "Frame buffer underflow");
        break;
#ifdef UVC_HOST_SUSPEND_RESUME_API_SUPPORTED
    case UVC_HOST_DEVICE_SUSPENDED:
        ESP_LOGI(TAG, "Device suspended");
        break;
    case UVC_HOST_DEVICE_RESUMED:
        ESP_LOGI(TAG, "Device resumed");
        break;
#endif // UVC_HOST_SUSPEND_RESUME_API_SUPPORTED
    default:
        abort();
        break;
    }
}

static void usb_lib_task(void *arg)
{
    while (1) {
        // Start handling system events
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "USB: All devices freed");
            // Continue handling USB events to allow device reconnection
        }
    }
}

/* Diagnostic preview only: take Y at every second row/column, without scaling
 * or JPEG decoding. The camera stream remains 640x480 YUY2. */
static void export_gray_preview(uvc_host_stream_hdl_t stream, QueueHandle_t queue,
                                uvc_host_frame_t *frame)
{
    const size_t size = 320 * 240;
    uint8_t *gray = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gray || frame->data_len != 640 * 480 * 2) {
        free(gray);
        uvc_host_frame_return(stream, frame);
        ESP_LOGE(TAG, "Snapshot allocation or frame length invalid");
        return;
    }
    for (size_t y = 0; y < 240; ++y) {
        for (size_t x = 0; x < 320; ++x) {
            gray[y * 320 + x] = frame->data[(y * 2 * 640 + x * 2) * 2];
        }
    }
    uvc_host_frame_return(stream, frame);
    ESP_ERROR_CHECK(uvc_host_stream_stop(stream));
    while (xQueueReceive(queue, &frame, 0) == pdPASS) {
        uvc_host_frame_return(stream, frame);
    }
    const uint32_t crc = esp_rom_crc32_le(0, gray, size);
    printf("\nSNAP_BEGIN 320 240 %u %08" PRIx32 "\n", (unsigned)size, crc);
    static const char hex[] = "0123456789abcdef";
    for (size_t y = 0; y < 240; ++y) {
        char line[641];
        for (size_t x = 0; x < 320; ++x) {
            const uint8_t value = gray[y * 320 + x];
            line[x * 2] = hex[value >> 4];
            line[x * 2 + 1] = hex[value & 15];
        }
        line[640] = '\0';
        printf("SNAP_ROW %u %s\n", (unsigned)y, line);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    printf("SNAP_END %08" PRIx32 "\n", crc);
    fflush(stdout);
    free(gray);
    ESP_ERROR_CHECK(uvc_host_stream_start(stream));
}

static void frame_handling_task(void *arg)
{
    TickType_t last_tag_tick = 0;
    const uvc_host_stream_config_t *stream_config = (uvc_host_stream_config_t *)arg;
    QueueHandle_t frame_q = *((QueueHandle_t *)(stream_config->user_ctx));
    const int uvc_index = stream_config->usb.uvc_stream_index;

    while (true) {
        uvc_host_stream_hdl_t uvc_stream = NULL;

        ESP_LOGI(TAG, "Opening UVC device 0x%04X:0x%04X-%d\n\t%dx%d@%2.1fFPS...",
                 stream_config->usb.vid, stream_config->usb.pid, uvc_index, stream_config->vs_format.h_res, stream_config->vs_format.v_res, stream_config->vs_format.fps);
        esp_err_t err = uvc_host_stream_open(stream_config, pdMS_TO_TICKS(5000), &uvc_stream);
        if (ESP_OK != err) {
            ESP_LOGI(TAG, "Failed to open device");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        //uvc_host_desc_print(uvc_stream);
        dev_connected = true;
        ESP_LOGI(TAG, "Device 0x%04X:0x%04X-%d OPENED!", stream_config->usb.vid, stream_config->usb.pid, uvc_index);
        vTaskDelay(pdMS_TO_TICKS(100));
        unsigned count = 0;

        // Keep a healthy stream running; only reopen after an actual failure.
        err = uvc_host_stream_start(uvc_stream);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Stream %d start failed: %s", uvc_index, esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Stream %d start. mode=continuous", uvc_index);
            const int64_t stream_started_us = esp_timer_get_time();
            int64_t last_status_us = stream_started_us;
            int64_t last_frame_us = 0;
            int64_t max_frame_gap_us = 0;
            while (dev_connected) {
                uvc_host_frame_t *frame;
                if (xQueueReceive(frame_q, &frame, pdMS_TO_TICKS(EXAMPLE_FRAME_TIMEOUT_MS)) == pdPASS) {
                    const int64_t now_us = esp_timer_get_time();
                    if (last_frame_us != 0 && now_us - last_frame_us > max_frame_gap_us) {
                        max_frame_gap_us = now_us - last_frame_us;
                    }
                    last_frame_us = now_us;
                    count++;
                    if (now_us - last_status_us >= EXAMPLE_STREAM_STATUS_US) {
                        ESP_LOGI(TAG,
                                 "STREAM_STATUS stream=%d uptime_ms=%lld frames=%u max_gap_ms=%.1f "
                                 "heap_free=%u internal_free=%u psram_free=%u",
                                 uvc_index, (now_us - stream_started_us) / 1000, count,
                                 max_frame_gap_us / 1000.0,
                                 (unsigned)esp_get_free_heap_size(),
                                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
                        last_status_us = now_us;
                    }
                    if (tag_preview_requested) {
                        tag_preview_requested = false;
                        if (xSemaphoreTake(tag_buffer_free, 0) != pdTRUE) {
                            uvc_host_frame_return(uvc_stream, frame);
                            ESP_LOGW("TAG36H11", "Preview skipped: detector buffer busy");
                            continue;
                        }
                        export_gray_preview(uvc_stream, frame_q, frame);
                        xSemaphoreGive(tag_buffer_free);
                        continue;
                    }
                    if (tag_detection_enabled &&
                        frame->data_len == 640 * 480 * 2 &&
                        xTaskGetTickCount() - last_tag_tick >= pdMS_TO_TICKS(TAG_SAMPLE_INTERVAL_MS) &&
                        xSemaphoreTake(tag_buffer_free, 0) == pdTRUE) {
                        const bool capture_full =
                            tag_full_frame_requested && tag_l2_enabled;
                        tag_full_frame_requested = false;
                        tag_full_frame_ready = false;
                        if (capture_full) {
                            for (size_t y = 0; y < 480; ++y) {
                                const uint8_t *src = frame->data + y * 640 * 2;
                                uint8_t *dst_full = tag_gray_full + y * 640;
                                for (size_t x = 0; x < 640; ++x) {
                                    const uint8_t value = src[x * 2];
                                    dst_full[x] = value;
                                    if ((x & 1U) == 0 && (y & 1U) == 0) {
                                        tag_gray[(y / 2) * 320 + x / 2] = value;
                                    }
                                }
                            }
                            tag_full_frame_ready = true;
                        } else {
                            for (size_t y = 0; y < 240; ++y) {
                                for (size_t x = 0; x < 320; ++x) {
                                    tag_gray[y * 320 + x] =
                                        frame->data[(y * 2 * 640 + x * 2) * 2];
                                }
                            }
                        }
                        last_tag_tick = xTaskGetTickCount();
                        xTaskNotifyGive(tag_task_handle);
                    }
                    ESP_LOGI(TAG, "Stream %d: New frame %dx%d! Len: %d DATA: %02X %02X %02X %02X ... %02X %02X %02X %02X", uvc_index, frame->vs_format.h_res, frame->vs_format.v_res, frame->data_len,
                             frame->data[0], frame->data[1], frame->data[2], frame->data[3],
                             frame->data[frame->data_len - 4], frame->data[frame->data_len - 3], frame->data[frame->data_len - 2], frame->data[frame->data_len - 1]);

                    // The optional preview uses its own nonblocking side buffer.
                    wifi_preview_submit_yuy2(frame->data, frame->data_len, now_us);

                    uvc_host_frame_return(uvc_stream, frame);
                } else {
                    ESP_LOGW(TAG, "Stream %d: Frame not received on time", uvc_index);
                    break;
                }
            }
        }

        if (dev_connected) {
            err = uvc_host_stream_stop(uvc_stream);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Stream %d stop failed: %s", uvc_index, esp_err_to_name(err));
            }
        }
        // A single task owns the handle, including disconnect cleanup.
        do {
            uvc_host_frame_t *queued_frame;
            while (xQueueReceive(frame_q, &queued_frame, 0) == pdPASS) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(uvc_host_frame_return(uvc_stream, queued_frame));
            }
            err = uvc_host_stream_close(uvc_stream);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Stream %d close deferred: %s", uvc_index, esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(EXAMPLE_STREAM_RETRY_MS));
            }
        } while (err != ESP_OK);
        dev_connected = false;
        ESP_LOGW(TAG, "Stream %d recovery: reopening after %d ms", uvc_index, EXAMPLE_STREAM_RETRY_MS);
        vTaskDelay(pdMS_TO_TICKS(EXAMPLE_STREAM_RETRY_MS));
    }
}

#if EXAMPLE_USE_SDCARD
#define MOUNT_POINT "/sdcard"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
void app_init_sdcard(void)
{
    esp_err_t ret;
    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 64 * 1024
    };
    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.

    ESP_LOGI(TAG, "Using SDMMC peripheral");

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
    // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 40MHz for SDMMC)
    // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // For SoCs where the SD power can be supplied both via an internal or external (e.g. on-board LDO) power supply.
    // When using specific IO pins (which can be used for ultra high-speed SDMMC) to connect to the SD card
    // and the internal LDO power supply, we need to initialize the power supply first.
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

    ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create a new on-chip LDO power control driver");
        return;
    }
    host.pwr_ctrl_handle = pwr_ctrl_handle;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    slot_config.width = 4;
    slot_config.clk = 43;
    slot_config.cmd = 44;
    slot_config.d0 = 39;
    slot_config.d1 = 40;
    slot_config.d2 = 41;
    slot_config.d3 = 42;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return;
    }
}
#endif // EXAMPLE_USE_SDCARD

static void start_uvc_frame_handling_tasks(void)
{
    if (frame_handling_tasks_started) {
        ESP_LOGI(TAG, "Existing frame task will reopen the reconnected camera");
        return;
    }
    bool supported = false;
    for (size_t i = 0; i < frame_info_list_size[0]; ++i) {
        const uvc_host_frame_info_t *info = &frame_info_list[0][i];
        if (info->format == UVC_VS_FORMAT_YUY2 && info->h_res == 640 && info->v_res == 480) {
            supported = true;
            break;
        }
    }
    if (!supported) {
        ESP_LOGE(TAG, "Requested YUY2 640x480 not advertised; no stream started");
        return;
    }
    uvc_host_stream_config_t _stream_config = {
        .event_cb = stream_callback,
        .frame_cb = frame_callback,
        .user_ctx = &rx_frames_queue[0],
        .usb = {
            .vid = EXAMPLE_USB_DEVICE_VID,
            .pid = EXAMPLE_USB_DEVICE_PID,
            .uvc_stream_index = 0,
        },
        .vs_format = {
            .h_res = 640,
            .v_res = 480,
            .fps = 10,
            .format = UVC_VS_FORMAT_YUY2,
        },
        .advanced = {
            .number_of_frame_buffers = EXAMPLE_FRAME_COUNT,
            .frame_size = 640 * 480 * 2,
            .number_of_urbs = 4,
            .urb_size = 10 * 1024,
        },
    };
    static uvc_host_stream_config_t stream_mjpeg_config;
    memcpy(&stream_mjpeg_config, &_stream_config, sizeof(uvc_host_stream_config_t));
    BaseType_t task_created = xTaskCreatePinnedToCore(frame_handling_task, "yuy2_handling", 4096, (void *)&stream_mjpeg_config, EXAMPLE_USB_HOST_PRIORITY - 2, NULL, tskNO_AFFINITY);
    assert(task_created == pdTRUE);
    frame_handling_tasks_started = true;

#if EXAMPLE_NUMBER_OF_STREAMS > 1
    vTaskDelay(pdMS_TO_TICKS(1000));
    _stream_config.usb.uvc_stream_index = 1;
    _stream_config.vs_format.h_res = frame_info_list[1][0].h_res;
    _stream_config.vs_format.v_res = frame_info_list[1][0].v_res;
    _stream_config.vs_format.fps = UVC_DESC_DWFRAMEINTERVAL_TO_FPS(frame_info_list[1][0].default_interval);
    _stream_config.vs_format.format = UVC_VS_FORMAT_H265;
    _stream_config.user_ctx = &rx_frames_queue[1];
    static uvc_host_stream_config_t stream_h265_config;
    memcpy(&stream_h265_config, &_stream_config, sizeof(uvc_host_stream_config_t));
    task_created = xTaskCreatePinnedToCore(frame_handling_task, "h265_handling", 4096, (void *)&stream_h265_config, EXAMPLE_USB_HOST_PRIORITY - 3, NULL, tskNO_AFFINITY);
    assert(task_created == pdTRUE);
#endif // EXAMPLE_NUMBER_OF_STREAMS > 1
}

static void uvc_event_cb(const uvc_host_driver_event_data_t *event, void *user_ctx)
{
    switch (event->type) {
    case UVC_HOST_DRIVER_EVENT_DEVICE_CONNECTED: {
        ESP_LOGI(TAG, "Device connected, addr: %d, stream index: %d", event->device_connected.dev_addr, event->device_connected.uvc_stream_index);
        int stream_index = event->device_connected.uvc_stream_index;
        if (stream_index >= EXAMPLE_NUMBER_OF_STREAMS) {
            ESP_LOGW(TAG, "Stream index %d is out of range. Max supported is %d", stream_index, EXAMPLE_NUMBER_OF_STREAMS - 1);
            break;
        }
        device_count++;
        if (device_count > 1) {
            ESP_LOGW(TAG, "Multiple devices connected. ignoring additional devices.");
            break;
        }

        frame_info_list_size[stream_index] = event->device_connected.frame_info_num;
        free(frame_info_list[stream_index]);
        frame_info_list[stream_index] = calloc(frame_info_list_size[stream_index], sizeof(uvc_host_frame_info_t));
        assert(frame_info_list[stream_index]);
        uvc_host_get_frame_list(event->device_connected.dev_addr, stream_index, (uvc_host_frame_info_t (*)[])frame_info_list[stream_index], &frame_info_list_size[stream_index]);
        for (int i = 0; i < frame_info_list_size[stream_index]; i++) {
            ESP_LOGI(TAG, "Camera format: %s %d*%d@%.1ffps",
                     FORMAT_STR[frame_info_list[stream_index][i].format],
                     frame_info_list[stream_index][i].h_res,
                     frame_info_list[stream_index][i].v_res,
                     UVC_DESC_DWFRAMEINTERVAL_TO_FPS(frame_info_list[stream_index][i].default_interval));
        }
        start_uvc_frame_handling_tasks();
        break;
    }
    default:
        break;
    }
}

/**
 * @brief Main application
 */
void app_main(void)
{
    tag_signal_init();
    tag_close_input_init();
    tag_action_mutex = xSemaphoreCreateMutex();
    tag_audio_queue = xQueueCreate(1, sizeof(tag_audio_request_t));
    assert(tag_action_mutex && tag_audio_queue);
    const esp_err_t audio_init_result = tag_audio_init();
    if (audio_init_result != ESP_OK) {
        ESP_LOGE("TAG_AUDIO",
                 "Initialization failed (%s); detection and GPIO outputs remain active",
                 esp_err_to_name(audio_init_result));
    }
    BaseType_t task_created =
        xTaskCreatePinnedToCore(tag_audio_task, "tag_audio", 6144, NULL, 4, NULL, tskNO_AFFINITY);
    assert(task_created == pdPASS);
    task_created =
        xTaskCreatePinnedToCore(tag_close_input_task, "tag_close", 3072, NULL, 6, NULL, tskNO_AFFINITY);
    assert(task_created == pdPASS);

    tag_gray = heap_caps_malloc(320 * 240, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    tag_gray_full = heap_caps_malloc(640 * 480, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    tag_gray_l2 = heap_caps_malloc(TAG_L2_IMAGE_WIDTH * TAG_L2_IMAGE_HEIGHT,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    tag_l2_integral = heap_caps_malloc((640 + 1) * (480 + 1) * sizeof(uint32_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    tag_buffer_free = xSemaphoreCreateBinary();
    assert(tag_gray && tag_gray_full && tag_gray_l2 && tag_l2_integral &&
           tag_buffer_free);
    ESP_LOGI("TAG_ENHANCE",
             "buffers full=%u enhanced=%u integral=%u psram_free=%u",
             (unsigned)(640 * 480),
             (unsigned)(TAG_L2_IMAGE_WIDTH * TAG_L2_IMAGE_HEIGHT),
             (unsigned)((640 + 1) * (480 + 1) * sizeof(uint32_t)),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    xSemaphoreGive(tag_buffer_free);
    BaseType_t tag_created = xTaskCreatePinnedToCore(tag_detection_task, "tag36h11", 98304, NULL, 5, &tag_task_handle, 1);
    assert(tag_created == pdPASS);
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0));
    task_created =
        xTaskCreatePinnedToCore(tag_uart_task, "tag_uart", 3072, NULL, 7, NULL, tskNO_AFFINITY);
    assert(task_created == pdPASS);
    ESP_LOGI(TAG,
             "UART commands: s=preview, g=golden image, e=empty image, "
             "t=synthetic L2 test, "
             "1..4=detector profiles, l=toggle L2 enhancement, "
             "p=pause, u=resume, q=status, "
             "r/c=stop audio and clear signals, GPIO22 high=external close; relay inputs=GPIO4/5; four-color=fixed_config");
    for (int i = 0; i < EXAMPLE_NUMBER_OF_STREAMS; i++) {
        rx_frames_queue[i] = xQueueCreate(EXAMPLE_FRAME_COUNT, sizeof(uvc_host_frame_t *));
        assert(rx_frames_queue[i]);
    }

#if EXAMPLE_USE_SDCARD
    app_init_sdcard();
#endif // EXAMPLE_USE_SDCARD

    // Install USB Host driver. Should only be called once in entire application
    ESP_LOGI(TAG, "Installing USB Host");
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    // Create a task that will handle USB library events
    task_created = xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4096, NULL, EXAMPLE_USB_HOST_PRIORITY, NULL, tskNO_AFFINITY);
    assert(task_created == pdTRUE);

    ESP_LOGI(TAG, "Installing UVC driver");
    const uvc_host_driver_config_t uvc_driver_config = {
        .driver_task_stack_size = 4 * 1024,
        .driver_task_priority = EXAMPLE_USB_HOST_PRIORITY + 1,
        .xCoreID = tskNO_AFFINITY,
        .create_background_task = true,
        .event_cb = uvc_event_cb,
    };
    ESP_ERROR_CHECK(uvc_host_install(&uvc_driver_config));
    wifi_preview_start();
}
