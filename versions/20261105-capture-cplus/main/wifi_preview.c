/* SPDX-License-Identifier: Apache-2.0 */
#include "wifi_preview.h"
#include "rm_capture_config.h"
#include "rm_mjpeg.h"
#include "cp_stream_policy.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#define PREVIEW_W RM_PREVIEW_WIDTH
#define PREVIEW_H RM_PREVIEW_HEIGHT
#define PREVIEW_SSID "RM-P4-Camera"
#define PREVIEW_PASSWORD "CHANGE_ME_12345"
static const char *TAG="WIFI_PREVIEW";
static volatile bool preview_ready,transport_failed,transport_up,network_ready,network_changing;
static volatile bool network_requested;
static portMUX_TYPE interest_mux=portMUX_INITIALIZER_UNLOCKED;
static uint32_t boot_id,received_frames,encoded_frames,dropped_frames,sent_frames,empty_responses,send_failures;
static uint32_t encode_failures,last_encode_us,last_send_us,max_send_us;
static int64_t last_input_us;
static float capture_fps_config;
static httpd_handle_t server,stream_server;
static TaskHandle_t sender_task_handle;
static portMUX_TYPE lights_mux=portMUX_INITIALIZER_UNLOCKED;
static wifi_preview_lights_t light_snapshot;
static bool light_snapshot_seen;
static portMUX_TYPE stream_mux=portMUX_INITIALIZER_UNLOCKED;
static cp_flow_t flow;
static cp_recovery_t recovery;
static uint32_t network_attempts,network_successes;
static bool job_busy;
typedef struct {cp_frame_ref_t frame;int fd;uint32_t generation,token;} cp_send_job_t;
static cp_send_job_t send_job;
extern const uint8_t page_start[] asm("_binary_index_html_start");
extern const uint8_t page_end[] asm("_binary_index_html_end");
extern const uint8_t script_start[] asm("_binary_preview_js_start");
extern const uint8_t script_end[] asm("_binary_preview_js_end");

void wifi_preview_set_capture_fps(float fps)
{
    portENTER_CRITICAL(&interest_mux);capture_fps_config=fps;portEXIT_CRITICAL(&interest_mux);
}
void wifi_preview_note_input(int64_t received_us)
{
    portENTER_CRITICAL(&interest_mux);++received_frames;last_input_us=received_us;portEXIT_CRITICAL(&interest_mux);
}
bool wifi_preview_wants_frame(int64_t now)
{
    portENTER_CRITICAL(&stream_mux);
    const bool wanted=preview_ready&&network_ready&&!transport_failed&&!job_busy&&cp_flow_ready(&flow,now);
    portEXIT_CRITICAL(&stream_mux);return wanted;
}
void wifi_preview_submit_mjpeg(const uint8_t *data,size_t length,int64_t received_us)
{
    if(!wifi_preview_wants_frame(received_us))return;
    const bool ok=length<=RM_PREVIEW_JPEG_CAPACITY&&rm_mjpeg_publish_current(data,length);
    portENTER_CRITICAL(&interest_mux);
    if(ok)++encoded_frames;else ++dropped_frames;
    portEXIT_CRITICAL(&interest_mux);
    if(ok&&sender_task_handle)xTaskNotifyGive(sender_task_handle);
}


/* Called inside the existing action mutex; this short copy never waits on HTTP.
 * HTTP copies it out and releases this spinlock before formatting or sending. */
void wifi_preview_publish_lights(const wifi_preview_lights_t *snapshot)
{
    if (!snapshot) return;
    portENTER_CRITICAL(&lights_mux);
    light_snapshot = *snapshot;
    light_snapshot_seen = true;
    portEXIT_CRITICAL(&lights_mux);
}


static esp_err_t script_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/javascript; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)script_start, script_end - script_start - 1);
}


static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)page_start, page_end - page_start - 1);
}

static esp_err_t preview_connection_open(httpd_handle_t handle, int socket_fd)
{
    (void)handle;
    /* HTTPD sends headers in several small writes. Avoid Nagle/delayed-ACK waits. */
    const int enabled = 1;
    if (setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) != 0) {
        ESP_LOGW(TAG, "TCP_NODELAY setup failed; closing preview connection");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    uint32_t received, encoded, dropped, sent, empty, send_errors, encode_errors;
    uint32_t encode_us, send_us, send_max_us;
    float configured_fps;
    int64_t input_us;
    portENTER_CRITICAL(&interest_mux);
    received = received_frames;
    encoded = encoded_frames;
    dropped = dropped_frames;
    sent = sent_frames;
    empty = empty_responses;
    send_errors = send_failures;
    encode_errors = encode_failures;
    encode_us = last_encode_us;
    send_us = last_send_us;
    send_max_us = max_send_us;
    input_us = last_input_us;
    configured_fps = capture_fps_config;
    portEXIT_CRITICAL(&interest_mux);
    const int64_t input_age_ms = input_us ? (esp_timer_get_time() - input_us) / 1000 : -1;
    wifi_preview_lights_t lamps;
    bool lamps_seen;
    portENTER_CRITICAL(&lights_mux);
    lamps = light_snapshot;
    lamps_seen = light_snapshot_seen;
    portEXIT_CRITICAL(&lights_mux);
    const int64_t lamp_age_us = esp_timer_get_time() - lamps.sampled_us;
    const bool lamps_valid = lamps_seen && lamp_age_us >= 0 && lamp_age_us <= 1000000LL;
    const int64_t lamp_age_ms = lamps_seen && lamp_age_us >= 0 ? (lamp_age_us + 999) / 1000 : -1;
    rm_mjpeg_stats_t mjpeg = {0};
#if RM_CAPTURE_USE_MJPEG
    rm_mjpeg_get_stats(&mjpeg);
#endif
    portENTER_CRITICAL(&stream_mux);
    const cp_flow_t stream=flow;const cp_recovery_t net=recovery;
    const uint32_t attempts=network_attempts,successes=network_successes;
    portEXIT_CRITICAL(&stream_mux);
    char response[2304];
    snprintf(response, sizeof(response),
        "{\"version\":\"capture-cplus-pending-v0.1\",\"preview_ready\":%s,"
        "\"boot_id\":\"%08" PRIx32 "\","
        "\"width\":%u,\"height\":%u,\"capture_fps_config\":%.3f,"
        "\"capture_format\":\"%s\",\"target_fps\":%u,\"preview_limit_fps\":%u,"
        "\"mjpeg_ready\":%s,\"mjpeg_prepare_errors\":%" PRIu32 ",\"mjpeg_dht_insertions\":%" PRIu32 ","
        "\"mjpeg_decoded\":%" PRIu32 ",\"mjpeg_decode_errors\":%" PRIu32 ",\"last_decode_ms\":%.2f,"
        "\"received\":%" PRIu32 ",\"encoded\":%" PRIu32 ",\"dropped\":%" PRIu32 ","
        "\"sent\":%" PRIu32 ",\"empty\":%" PRIu32 ",\"send_errors\":%" PRIu32 ","
        "\"encode_errors\":%" PRIu32 ",\"last_input_age_ms\":%lld,"
        "\"last_encode_ms\":%.2f,\"last_send_ms\":%.2f,\"max_send_ms\":%.2f,"
        "\"transport_failed\":%s,\"tcp_nodelay\":true,"
        "\"stream_port\":81,\"stream_target_fps\":%u,\"stream_connected\":%s,"
        "\"stream_acks\":%" PRIu32 ",\"stream_timeouts\":%" PRIu32 ",\"last_ack_ms\":%.2f,"
        "\"pool_free\":%" PRIu32 ",\"pool_busy\":%" PRIu32 ",\"pool_errors\":%" PRIu32 ","
        "\"net_attempts\":%" PRIu32 ",\"net_successes\":%" PRIu32 ",\"net_exhausted\":%s,"
        "\"heap_free\":%u,\"internal_free\":%u,"
        "\"lights\":{\"valid\":%s,\"age_ms\":%lld,\"epoch\":%" PRIu32 ","
        "\"enabled\":%s,\"batch_active\":%s,\"selected\":%d,\"four_color\":%d,"
        "\"channels\":[{\"color\":\"red\",\"state\":%u,\"multiplier\":%u,\"on\":%s},"
        "{\"color\":\"green\",\"state\":%u,\"multiplier\":%u,\"on\":%s},"
        "{\"color\":\"blue\",\"state\":%u,\"multiplier\":%u,\"on\":%s}]}}",
        preview_ready ? "true" : "false", boot_id, (unsigned)PREVIEW_W, (unsigned)PREVIEW_H, configured_fps,
        RM_CAPTURE_FORMAT_NAME, (unsigned)RM_CAPTURE_TARGET_FPS, (unsigned)RM_PREVIEW_MAX_FPS,
        mjpeg.ready ? "true" : "false", mjpeg.prepare_errors, mjpeg.dht_insertions,
        mjpeg.decoded, mjpeg.decode_errors, mjpeg.last_decode_us / 1000.0,
        received, encoded, dropped, sent, empty,
        send_errors, encode_errors, input_age_ms, encode_us / 1000.0, send_us / 1000.0,
        send_max_us / 1000.0, transport_failed ? "true" : "false",
        stream.target_fps,stream.connected?"true":"false",stream.acks,stream.timeouts,stream.last_ack_us/1000.0,
        mjpeg.free_slots,mjpeg.pool_busy,mjpeg.pool_errors,attempts,successes,net.exhausted?"true":"false",
        (unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        lamps_valid ? "true" : "false", lamp_age_ms, lamps.epoch,
        lamps.enabled ? "true" : "false", lamps.batch_active ? "true" : "false",
        lamps.selected, lamps.four_color,
        (unsigned)lamps.channels[0].state, (unsigned)lamps.channels[0].multiplier,
        lamps.channels[0].on ? "true" : "false",
        (unsigned)lamps.channels[1].state, (unsigned)lamps.channels[1].multiplier,
        lamps.channels[1].on ? "true" : "false",
        (unsigned)lamps.channels[2].state, (unsigned)lamps.channels[2].multiplier,
        lamps.channels[2].on ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, response);
}

/* CP_PROTOCOL_HANDLERS_BEGIN: shared production adapter, host-verifiable. */
static esp_err_t stream_handler(httpd_req_t *req)
{
    const int fd=httpd_req_to_sockfd(req);
    if(req->method==HTTP_GET){
        portENTER_CRITICAL(&stream_mux);
        const bool accepted=cp_flow_connect(&flow,fd,esp_timer_get_time());
        portEXIT_CRITICAL(&stream_mux);
        return accepted?ESP_OK:ESP_FAIL;
    }
    httpd_ws_frame_t packet={0};
    esp_err_t err=httpd_ws_recv_frame(req,&packet,0);
    if(err!=ESP_OK||packet.type!=HTTPD_WS_TYPE_TEXT||packet.len<7||packet.len>63)return ESP_FAIL;
    uint8_t command[64];packet.payload=command;
    err=httpd_ws_recv_frame(req,&packet,sizeof(command)-1);
    uint32_t token,ack;bool shown;
    if(err!=ESP_OK||!cp_parse_credit((const char*)command,packet.len,&token,&ack,&shown))return ESP_FAIL;
    portENTER_CRITICAL(&stream_mux);
    const bool accepted=flow.fd==fd&&cp_flow_credit(&flow,token,ack,shown,esp_timer_get_time());
    portEXIT_CRITICAL(&stream_mux);
    if(!accepted)return ESP_FAIL;
    if(sender_task_handle)xTaskNotifyGive(sender_task_handle);
    return ESP_OK;
}
static void stream_close(httpd_handle_t handle,int fd)
{
    (void)handle;
    portENTER_CRITICAL(&stream_mux);cp_flow_disconnect(&flow,fd);portEXIT_CRITICAL(&stream_mux);
    close(fd); /* A custom HTTPD close callback owns the descriptor close. */
}
static void stream_send_work(void *argument)
{
    cp_send_job_t *job=argument;
    portENTER_CRITICAL(&stream_mux);
    bool valid=preview_ready&&network_ready&&flow.connected&&!flow.closing&&flow.fd==job->fd&&flow.generation==job->generation&&!transport_failed;
    portEXIT_CRITICAL(&stream_mux);
    const int64_t started=esp_timer_get_time();
    const int64_t age=started-job->frame.received_us;
    valid=valid&&age>=0&&age<=CP_FRAME_MAX_AGE_US;
    esp_err_t result=ESP_FAIL;
    if(valid){
        cp_packet_header(job->frame.storage,boot_id,job->frame.sequence,job->token,
            (uint32_t)((age+999)/1000),(uint32_t)job->frame.length);
        httpd_ws_frame_t packet={.type=HTTPD_WS_TYPE_BINARY,.payload=job->frame.storage,
            .len=CP_PACKET_HEADER+job->frame.length};
        result=httpd_ws_send_frame_async(stream_server,job->fd,&packet);
    }
    const int64_t ended=esp_timer_get_time();
    const uint32_t duration=(uint32_t)(ended-started);
    /* send() has finished consuming the payload. No JPEG copy and no early reuse. */
    rm_mjpeg_release_view(&job->frame);
    portENTER_CRITICAL(&stream_mux);
    const bool waiting=cp_flow_finish(&flow,job->generation,result==ESP_OK,ended,duration);
    const bool must_close=!waiting&&flow.connected&&flow.generation==job->generation;
    job_busy=false;
    portEXIT_CRITICAL(&stream_mux);
    portENTER_CRITICAL(&interest_mux);
    last_send_us=duration;if(duration>max_send_us)max_send_us=duration;
    if(result==ESP_OK)++sent_frames;else ++send_failures;
    portEXIT_CRITICAL(&interest_mux);
    if(must_close)httpd_sess_trigger_close(stream_server,job->fd);
}
static void sender_tick(int64_t now)
{
    int close_fd=-1;
    portENTER_CRITICAL(&stream_mux);
    if(cp_flow_expire(&flow,now))close_fd=flow.fd;
    const bool wanted=stream_server&&preview_ready&&network_ready&&!transport_failed&&!job_busy&&cp_flow_ready(&flow,now);
    portEXIT_CRITICAL(&stream_mux);
    if(close_fd>=0)httpd_sess_trigger_close(stream_server,close_fd);
    if(!wanted)return;
    cp_frame_ref_t view={0};
    if(!rm_mjpeg_acquire_latest(&view))return;
    portENTER_CRITICAL(&stream_mux);
    const bool fresh=view.length<=RM_PREVIEW_JPEG_CAPACITY&&view.received_us>=flow.credit_received_us&&
        now>=view.received_us&&now-view.received_us<=CP_FRAME_MAX_AGE_US;
    const bool selected=!job_busy&&fresh&&cp_flow_begin(&flow,view.sequence,now);
    if(selected){
        job_busy=true;
        send_job=(cp_send_job_t){.frame=view,.fd=flow.fd,.generation=flow.generation,.token=flow.flight_token};
    }
    portEXIT_CRITICAL(&stream_mux);
    if(!selected){rm_mjpeg_release_view(&view);return;}
    if(httpd_queue_work(stream_server,stream_send_work,&send_job)!=ESP_OK){
        rm_mjpeg_release_view(&send_job.frame);
        portENTER_CRITICAL(&stream_mux);
        const int fd=send_job.fd;
        cp_flow_finish(&flow,send_job.generation,false,esp_timer_get_time(),0);job_busy=false;
        portEXIT_CRITICAL(&stream_mux);
        httpd_sess_trigger_close(stream_server,fd);
    }
}
/* CP_PROTOCOL_HANDLERS_END */

static void sender_task(void *unused)
{
    (void)unused;
    while(true){ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(2));sender_tick(esp_timer_get_time());}
}


static bool nvs_ready,netif_ready,loop_ready,handlers_ready,hosted_ready,wifi_initialized,ap_started;
static esp_netif_t *ap_netif;
static void transport_event(void *arg,esp_event_base_t base,int32_t id,void *data)
{
    (void)arg;(void)base;(void)data;
    portENTER_CRITICAL(&stream_mux);
    if(id==ESP_HOSTED_EVENT_TRANSPORT_FAILURE||id==ESP_HOSTED_EVENT_TRANSPORT_DOWN){
        if(!transport_failed)network_requested=true;
        transport_failed=true;transport_up=false;network_ready=preview_ready=false;ap_started=false;
    }else if(id==ESP_HOSTED_EVENT_TRANSPORT_UP){
        if(transport_failed)network_requested=true;
        transport_failed=false;transport_up=true;
    }
    portEXIT_CRITICAL(&stream_mux);
}
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data)
{
    (void)arg;(void)base;(void)data;
    portENTER_CRITICAL(&stream_mux);
    if(id==WIFI_EVENT_AP_STOP){
        ap_started=false;
        if(!network_changing&&network_ready){network_ready=false;network_requested=true;}
    }
    portEXIT_CRITICAL(&stream_mux);
}
static esp_err_t ensure_ap(void)
{
    esp_err_t err;
    if(!nvs_ready){err=nvs_flash_init();if(err!=ESP_OK)return err;nvs_ready=true;}
    if(!netif_ready){err=esp_netif_init();if(err!=ESP_OK)return err;netif_ready=true;}
    if(!loop_ready){err=esp_event_loop_create_default();if(err!=ESP_OK&&err!=ESP_ERR_INVALID_STATE)return err;loop_ready=true;}
    if(!handlers_ready){
        err=esp_event_handler_register(ESP_HOSTED_EVENT,ESP_EVENT_ANY_ID,transport_event,NULL);if(err!=ESP_OK)return err;
        err=esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_AP_STOP,wifi_event,NULL);if(err!=ESP_OK)return err;
        handlers_ready=true;
    }
    if(!hosted_ready){err=esp_hosted_init();if(err!=ESP_OK)return err;hosted_ready=true;}
    const bool reconnect=transport_failed||!transport_up;
    if(reconnect){
        err=esp_hosted_connect_to_slave();if(err!=ESP_OK)return err;
        /* The pinned wrapper may log an error but return OK. Require its UP event. */
        for(unsigned i=0;i<100&&!transport_up;++i)vTaskDelay(pdMS_TO_TICKS(10));
        if(!transport_up||transport_failed)return ESP_ERR_TIMEOUT;
    }
    if(!ap_netif){ap_netif=esp_netif_create_default_wifi_ap();if(!ap_netif)return ESP_ERR_NO_MEM;}
    if(wifi_initialized&&(reconnect||!ap_started)){
        err=esp_wifi_stop();if(err!=ESP_OK&&err!=ESP_ERR_WIFI_NOT_INIT)return err;
        err=esp_wifi_deinit();if(err!=ESP_OK&&err!=ESP_ERR_WIFI_NOT_INIT)return err;
        wifi_initialized=false;ap_started=false;
    }
    if(!wifi_initialized){
        wifi_init_config_t init_cfg=WIFI_INIT_CONFIG_DEFAULT();err=esp_wifi_init(&init_cfg);if(err!=ESP_OK)return err;
        wifi_initialized=true;
    }
    if(!ap_started){
        err=esp_wifi_set_storage(WIFI_STORAGE_RAM);if(err!=ESP_OK)return err;
        wifi_config_t wifi_cfg={0};
        memcpy(wifi_cfg.ap.ssid,PREVIEW_SSID,sizeof(PREVIEW_SSID));wifi_cfg.ap.ssid_len=strlen(PREVIEW_SSID);
        memcpy(wifi_cfg.ap.password,PREVIEW_PASSWORD,sizeof(PREVIEW_PASSWORD));
        wifi_cfg.ap.channel=1;wifi_cfg.ap.max_connection=2;wifi_cfg.ap.authmode=WIFI_AUTH_WPA2_PSK;
        err=esp_wifi_set_mode(WIFI_MODE_AP);if(err!=ESP_OK)return err;
        err=esp_wifi_set_config(WIFI_IF_AP,&wifi_cfg);if(err!=ESP_OK)return err;
        err=esp_wifi_start();if(err!=ESP_OK)return err;ap_started=true;
    }
    return ESP_OK;
}
static esp_err_t start_servers(void)
{
    esp_err_t err;
    if(!server){
        httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.core_id=0;cfg.task_priority=2;cfg.stack_size=8192;
        cfg.max_open_sockets=4;cfg.lru_purge_enable=true;cfg.recv_wait_timeout=1;cfg.send_wait_timeout=1;
        cfg.open_fn=preview_connection_open;cfg.keep_alive_enable=true;
        err=httpd_start(&server,&cfg);if(err!=ESP_OK)return err;
        const httpd_uri_t home={.uri="/",.method=HTTP_GET,.handler=root_handler};
        const httpd_uri_t script={.uri="/preview.js",.method=HTTP_GET,.handler=script_handler};
        const httpd_uri_t status={.uri="/status",.method=HTTP_GET,.handler=status_handler};
        err=httpd_register_uri_handler(server,&home);
        if(err==ESP_OK)err=httpd_register_uri_handler(server,&script);
        if(err==ESP_OK)err=httpd_register_uri_handler(server,&status);
        if(err!=ESP_OK){httpd_stop(server);server=NULL;return err;}
    }
    if(!stream_server){
        httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.server_port=81;cfg.ctrl_port=32769;
        cfg.core_id=0;cfg.task_priority=3;cfg.stack_size=8192;cfg.max_open_sockets=2;cfg.lru_purge_enable=true;
        cfg.recv_wait_timeout=1;cfg.send_wait_timeout=1;cfg.open_fn=preview_connection_open;cfg.close_fn=stream_close;
        cfg.keep_alive_enable=true;cfg.keep_alive_idle=2;cfg.keep_alive_interval=1;cfg.keep_alive_count=3;
        err=httpd_start(&stream_server,&cfg);if(err!=ESP_OK)return err;
        const httpd_uri_t stream={.uri="/stream",.method=HTTP_GET,.handler=stream_handler,.is_websocket=true};
        err=httpd_register_uri_handler(stream_server,&stream);
        if(err!=ESP_OK){httpd_stop(stream_server);stream_server=NULL;return err;}
    }
    return ESP_OK;
}
static void network_task(void *unused)
{
    (void)unused;boot_id=esp_random();cp_flow_init(&flow,RM_PREVIEW_MAX_FPS);
    cp_recovery_arm(&recovery,esp_timer_get_time());
    if(xTaskCreatePinnedToCore(sender_task,"preview_sender",6144,NULL,3,&sender_task_handle,0)!=pdPASS){
        ESP_LOGE(TAG,"Sender allocation failed; recognition/GPIO remain active");vTaskDelete(NULL);return;
    }
    while(true){
        const int64_t now=esp_timer_get_time();
        portENTER_CRITICAL(&stream_mux);
        if(network_requested){
            network_requested=false;
            if(!recovery.pending)cp_recovery_arm(&recovery,now);
        }
        const bool attempt=cp_recovery_take(&recovery,now);
        if(attempt){network_changing=true;++network_attempts;}
        portEXIT_CRITICAL(&stream_mux);
        if(attempt){
            esp_err_t err=ensure_ap();if(err==ESP_OK)err=start_servers();
            const bool ok=err==ESP_OK&&transport_up&&!transport_failed&&server&&stream_server;
            portENTER_CRITICAL(&stream_mux);
            network_changing=false;network_ready=preview_ready=ok;
            cp_recovery_result(&recovery,ok,esp_timer_get_time());
            if(ok){++network_successes;network_requested=false;}
            const bool exhausted=recovery.exhausted;
            portEXIT_CRITICAL(&stream_mux);
            if(ok)ESP_LOGI(TAG,"READY C Plus http=80 ws=81 capture_target=60 preview_adaptive=5..60; no measured FPS");
            else ESP_LOGW(TAG,"Network attempt failed (%s) exhausted=%d; no P4 restart or recognition reset",esp_err_to_name(err),exhausted);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void wifi_preview_start(void)
{
    if(xTaskCreatePinnedToCore(network_task,"preview_init",8192,NULL,2,NULL,0)!=pdPASS)
        ESP_LOGE(TAG,"Network task unavailable; recognition/GPIO remain active");
}
