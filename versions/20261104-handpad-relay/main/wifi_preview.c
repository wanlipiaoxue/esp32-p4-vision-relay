/* SPDX-License-Identifier: Apache-2.0 */
#include "wifi_preview.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/jpeg_encode.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_hosted_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define PREVIEW_W 320
#define PREVIEW_H 240
#define PREVIEW_RAW_SIZE (PREVIEW_W * PREVIEW_H)
#define PREVIEW_JPEG_CAPACITY (64 * 1024)
#define PREVIEW_INTEREST_US (2000000LL)
#define PREVIEW_MAX_AGE_US (250000LL)
#define PREVIEW_SSID "RM-P4-Camera"
#define PREVIEW_PASSWORD "CHANGE_ME_12345"
static const char *TAG = "WIFI_PREVIEW";
static jpeg_encoder_handle_t encoder;
static uint8_t *gray_buffer, *encoded_buffer, *published_buffer, *http_buffer;
static size_t gray_capacity, encoded_capacity;
static SemaphoreHandle_t raw_free, image_mutex;
static TaskHandle_t encoder_task_handle;
static volatile bool preview_ready;
static volatile bool transport_failed;
static portMUX_TYPE interest_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t last_request_us, raw_received_us, published_received_us;
static uint32_t published_sequence, boot_id;
static volatile uint32_t dropped_frames, encoded_frames;
static size_t published_length;
static httpd_handle_t server;
extern const uint8_t page_start[] asm("_binary_index_html_start");
extern const uint8_t page_end[] asm("_binary_index_html_end");

static void record_interest(int64_t now)
{
    portENTER_CRITICAL(&interest_mux);
    last_request_us = now;
    portEXIT_CRITICAL(&interest_mux);
}
static bool viewer_interested(int64_t now)
{
    portENTER_CRITICAL(&interest_mux);
    const int64_t requested = last_request_us;
    portEXIT_CRITICAL(&interest_mux);
    return requested > 0 && now - requested < PREVIEW_INTEREST_US;
}
static void record_drop(void)
{
    portENTER_CRITICAL(&interest_mux);
    ++dropped_frames;
    portEXIT_CRITICAL(&interest_mux);
}
/* No detector buffer or detector semaphore is used by this side copy. */
void wifi_preview_submit_yuy2(const uint8_t *data, size_t length, int64_t received_us)
{
    if (!preview_ready || transport_failed || !data || length != 640U * 480U * 2U ||
        !viewer_interested(received_us)) return;
    if (xSemaphoreTake(raw_free, 0) != pdTRUE) {
        record_drop();
        return;
    }
    for (size_t y = 0; y < PREVIEW_H; ++y) {
        const uint8_t *row = data + y * 2U * 640U * 2U;
        uint8_t *out = gray_buffer + y * PREVIEW_W;
        for (size_t x = 0; x < PREVIEW_W; ++x) out[x] = row[x * 4U];
    }
    raw_received_us = received_us;
    xTaskNotifyGive(encoder_task_handle);
}
static void encode_task(void *unused)
{
    (void)unused;
    const jpeg_encode_cfg_t cfg = {
        .height = PREVIEW_H, .width = PREVIEW_W,
        .src_type = JPEG_ENCODE_IN_FORMAT_GRAY,
        .sub_sample = JPEG_DOWN_SAMPLING_GRAY, .image_quality = 55,
    };
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const int64_t timestamp = raw_received_us;
        uint32_t output_length = 0;
        esp_err_t result = ESP_ERR_TIMEOUT;
        if (!transport_failed && viewer_interested(esp_timer_get_time())) {
            result = jpeg_encoder_process(encoder, &cfg, gray_buffer,
                PREVIEW_RAW_SIZE, encoded_buffer, encoded_capacity, &output_length);
        }
        xSemaphoreGive(raw_free);
        if (result != ESP_OK || output_length == 0 ||
            output_length > PREVIEW_JPEG_CAPACITY ||
            esp_timer_get_time() - timestamp > PREVIEW_MAX_AGE_US) {
            record_drop();
            continue;
        }
        if (xSemaphoreTake(image_mutex, 0) != pdTRUE) {
            record_drop();
            continue;
        }
        memcpy(published_buffer, encoded_buffer, output_length);
        published_length = output_length;
        published_received_us = timestamp;
        ++published_sequence;
        ++encoded_frames;
        xSemaphoreGive(image_mutex);
    }
}
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)page_start, page_end - page_start - 1);
}
static esp_err_t frame_handler(httpd_req_t *req)
{
    record_interest(esp_timer_get_time());
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
    if (!preview_ready || transport_failed || xSemaphoreTake(image_mutex, 0) != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, NULL, 0);
    }
    size_t length = published_length;
    const uint32_t seq = published_sequence;
    const int64_t age_us = esp_timer_get_time() - published_received_us;
    char query[96], previous[24];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "after", previous, sizeof(previous)) == ESP_OK &&
        strtoul(previous, NULL, 10) == seq) length = 0;
    if (age_us > PREVIEW_MAX_AGE_US) length = 0;
    if (length) memcpy(http_buffer, published_buffer, length);
    xSemaphoreGive(image_mutex);
    char sequence[24], age[24], boot[16];
    snprintf(sequence, sizeof(sequence), "%" PRIu32, seq);
    snprintf(age, sizeof(age), "%lld", (long long)(age_us > 0 ? age_us / 1000 : 0));
    snprintf(boot, sizeof(boot), "%08" PRIx32, boot_id);
    httpd_resp_set_hdr(req, "X-Frame-Seq", sequence);
    httpd_resp_set_hdr(req, "X-Frame-Age-Ms", age);
    httpd_resp_set_hdr(req, "X-Boot-Id", boot);
    if (!length) {
        httpd_resp_set_status(req, "204 No Content");
        return httpd_resp_send(req, NULL, 0);
    }
    httpd_resp_set_type(req, "image/jpeg");
    /* The single HTTP task owns http_buffer; the USB frame is already returned. */
    return httpd_resp_send(req, (const char *)http_buffer, length);
}
static esp_err_t status_handler(httpd_req_t *req)
{
    char response[320];
    snprintf(response, sizeof(response),
        "{\"version\":\"wifi-preview-v0.1\",\"preview_ready\":%s,"
        "\"width\":320,\"height\":240,\"capture_fps_config\":10,"
        "\"encoded\":%" PRIu32 ",\"dropped\":%" PRIu32 ","
        "\"heap_free\":%u,\"internal_free\":%u}",
        preview_ready ? "true" : "false", encoded_frames, dropped_frames,
        (unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, response);
}
static bool setup_encoder(void)
{
    const jpeg_encode_memory_alloc_cfg_t in_cfg = {
        .buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER,
    };
    const jpeg_encode_memory_alloc_cfg_t out_cfg = {
        .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
    };
    gray_buffer = jpeg_alloc_encoder_mem(PREVIEW_RAW_SIZE, &in_cfg, &gray_capacity);
    encoded_buffer = jpeg_alloc_encoder_mem(PREVIEW_JPEG_CAPACITY, &out_cfg, &encoded_capacity);
    published_buffer = heap_caps_malloc(PREVIEW_JPEG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    http_buffer = heap_caps_malloc(PREVIEW_JPEG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    raw_free = xSemaphoreCreateBinary();
    image_mutex = xSemaphoreCreateMutex();
    if (!gray_buffer || !encoded_buffer || !published_buffer || !http_buffer ||
        !raw_free || !image_mutex) {
        ESP_LOGE(TAG, "Preview allocation failed; original recognition remains enabled");
        goto failed;
    }
    const jpeg_encode_engine_cfg_t engine_cfg = {.intr_priority = 1, .timeout_ms = 40};
    esp_err_t err = jpeg_new_encoder_engine(&engine_cfg, &encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "JPEG initialization failed: %s", esp_err_to_name(err));
        goto failed;
    }
    if (xTaskCreatePinnedToCore(encode_task, "preview_jpeg", 6144, NULL, 2,
        &encoder_task_handle, 0) != pdPASS) {
        ESP_LOGE(TAG, "Preview task allocation failed");
        goto failed;
    }
    xSemaphoreGive(raw_free);
    preview_ready = true;
    return true;
failed:
    if (encoder) {
        jpeg_del_encoder_engine(encoder);
        encoder = NULL;
    }
    if (raw_free) vSemaphoreDelete(raw_free);
    if (image_mutex) vSemaphoreDelete(image_mutex);
    free(gray_buffer);
    free(encoded_buffer);
    free(published_buffer);
    free(http_buffer);
    gray_buffer = encoded_buffer = published_buffer = http_buffer = NULL;
    raw_free = image_mutex = NULL;
    return false;
}
static void transport_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == ESP_HOSTED_EVENT_TRANSPORT_FAILURE) {
        if (!transport_failed)
            ESP_LOGE(TAG, "Transport failed; preview suspended, recognition and GPIO untouched");
        transport_failed = true;
    } else if (id == ESP_HOSTED_EVENT_TRANSPORT_UP) {
        transport_failed = false;
    }
}
static esp_err_t start_ap(void)
{
    esp_err_t err = nvs_flash_init();
    /* Existing NVS is never erased automatically by the optional preview. */
    if (err != ESP_OK) return err;
    err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_handler_register(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID,
                                    transport_event, NULL);
    if (err != ESP_OK) return err;
    err = esp_hosted_init();
    if (err != ESP_OK) return err;
    if (!esp_netif_create_default_wifi_ap()) return ESP_ERR_NO_MEM;
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    wifi_config_t wifi_cfg = {0};
    memcpy(wifi_cfg.ap.ssid, PREVIEW_SSID, sizeof(PREVIEW_SSID));
    wifi_cfg.ap.ssid_len = strlen(PREVIEW_SSID);
    memcpy(wifi_cfg.ap.password, PREVIEW_PASSWORD, sizeof(PREVIEW_PASSWORD));
    wifi_cfg.ap.channel = 1;
    wifi_cfg.ap.max_connection = 2;
    wifi_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.ap.pmf_cfg.required = false;
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg);
    if (err != ESP_OK) return err;
    return esp_wifi_start();
}
static void network_task(void *unused)
{
    (void)unused;
    boot_id = esp_random();
    setup_encoder();
    esp_err_t err = start_ap();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi unavailable: %s; no recognition or GPIO reset requested",
            esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.task_priority = 2;
    cfg.core_id = 0;
    cfg.stack_size = 6144;
    cfg.max_open_sockets = 3;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 1;
    cfg.send_wait_timeout = 1;
    err = httpd_start(&server, &cfg);
    if (err == ESP_OK) {
        const httpd_uri_t home = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
        const httpd_uri_t frame = {.uri = "/frame.jpg", .method = HTTP_GET, .handler = frame_handler};
        const httpd_uri_t status = {.uri = "/status", .method = HTTP_GET, .handler = status_handler};
        err = httpd_register_uri_handler(server, &home);
        if (err == ESP_OK) err = httpd_register_uri_handler(server, &frame);
        if (err == ESP_OK) err = httpd_register_uri_handler(server, &status);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "READY ssid=%s password=%s url=http://192.168.4.1/ capture=YUY2_640x480_10fps",
            PREVIEW_SSID, PREVIEW_PASSWORD);
    } else ESP_LOGE(TAG, "HTTP startup failed: %s", esp_err_to_name(err));
    vTaskDelete(NULL);
}
void wifi_preview_start(void)
{
    if (xTaskCreatePinnedToCore(network_task, "preview_init", 8192, NULL, 2, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "Network task unavailable; original recognition remains enabled");
}
