/* SPDX-License-Identifier: Apache-2.0: exact status serializer, SDK reads stubbed. */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wifi_preview.h"
#include "rm_mjpeg.h"
#include "rm_capture_config.h"
#include "cp_stream_policy.h"
#define PREVIEW_W RM_PREVIEW_WIDTH
#define PREVIEW_H RM_PREVIEW_HEIGHT
#define ESP_OK 0
#define MALLOC_CAP_INTERNAL 1
typedef int esp_err_t;typedef struct{int unused;}httpd_req_t;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int interest_mux,lights_mux,stream_mux,critical;
#define portENTER_CRITICAL(p) do{(void)(p);CHECK(!critical);critical=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);CHECK(critical);critical=0;}while(0)
static uint32_t boot_id=0x1234abcd,received_frames=60,encoded_frames=30,dropped_frames=2,sent_frames=29,empty_responses,send_failures;
static uint32_t encode_failures,last_encode_us,last_send_us=1000,max_send_us=2000,network_attempts=2,network_successes=1;
static int64_t fake_us=1000000,last_input_us=999000;
static float capture_fps_config=60;
static bool preview_ready=true,transport_failed,light_snapshot_seen;
static wifi_preview_lights_t light_snapshot;
static cp_flow_t flow;static cp_recovery_t recovery;
static char response[4096];
static int64_t esp_timer_get_time(void){return fake_us;}
static int httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;return ESP_OK;}
static int httpd_resp_set_hdr(httpd_req_t *r,const char *a,const char *b){(void)r;(void)a;(void)b;return ESP_OK;}
static int httpd_resp_sendstr(httpd_req_t *r,const char *body){(void)r;CHECK(!critical&&strlen(body)<sizeof(response));strcpy(response,body);return ESP_OK;}
static unsigned esp_get_free_heap_size(void){return 100000;}
static unsigned heap_caps_get_free_size(int cap){(void)cap;return 50000;}
void rm_mjpeg_get_stats(rm_mjpeg_stats_t *s){*s=(rm_mjpeg_stats_t){.ready=true,.free_slots=2};}
#include "cplus_status.inc"
int main(void){
    cp_flow_init(&flow,60);httpd_req_t request={0};CHECK(status_handler(&request)==ESP_OK);printf("STATUS_COLD %s\n",response);
    wifi_preview_lights_t lamps={.epoch=9,.enabled=true,.batch_active=true,.selected=0,.four_color=0,.sampled_us=fake_us,
        .channels={{.state=2,.multiplier=4,.on=true},{.state=1,.multiplier=2,.on=true},{.state=3,.multiplier=2,.on=false}}};
    wifi_preview_publish_lights(&lamps);CHECK(status_handler(&request)==ESP_OK);printf("STATUS_LIVE %s\n",response);
    fake_us+=1000001;CHECK(status_handler(&request)==ESP_OK);printf("STATUS_STALE %s\n",response);
    printf("PASS CPLUS status assertions=%u (production JSON, recovery and lamp snapshots)\n",checks);return 0;
}
