/* SPDX-License-Identifier: Apache-2.0
 * Compiles exact production publisher/TCP/frame/status functions with IDF stubs. */
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wifi_preview.h"
#include "rm_capture_config.h"
#include "rm_capture_mode.h"
#define PREVIEW_JPEG_CAPACITY RM_PREVIEW_JPEG_CAPACITY
#define PREVIEW_INTEREST_US 2000000LL
static int64_t next_preview_submit_us;
#include "rm_mjpeg.h"
#define PREVIEW_W RM_PREVIEW_WIDTH
#define PREVIEW_H RM_PREVIEW_HEIGHT
static float capture_fps_config = 30.0f;
#if RM_CAPTURE_USE_MJPEG
void rm_mjpeg_get_stats(rm_mjpeg_stats_t *s){*s=(rm_mjpeg_stats_t){.ready=true};}
#endif

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x); exit(1); } } while(0)
typedef int esp_err_t;
typedef void *httpd_handle_t;
typedef struct { const char *query; int status; char state[20]; } httpd_req_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define pdTRUE 1
#define PREVIEW_MAX_AGE_US 250000LL
#define MALLOC_CAP_INTERNAL 1
#define IPPROTO_TCP 6
#define TCP_NODELAY 1
static int interest_mux, lights_mux, image_mutex, critical, held, socket_result;
#define portENTER_CRITICAL(p) do { (void)(p); assert(!critical); critical=1; } while(0)
#define portEXIT_CRITICAL(p) do { (void)(p); assert(critical); critical=0; } while(0)
static wifi_preview_lights_t light_snapshot;
static bool light_snapshot_seen, preview_ready = true, transport_failed;
static int64_t fake_us=1000000, published_received_us=999000, last_input_us=999000, last_request_us;
static uint32_t boot_id=0x1234abcd, published_sequence=7, received_frames, encoded_frames, dropped_frames;
static uint32_t sent_frames, empty_responses, send_failures, encode_failures, last_encode_us, last_send_us, max_send_us;
static unsigned char published_data[32]="JPEG-fixture", http_data[32];
static unsigned char *published_buffer=published_data, *http_buffer=http_data;
static size_t published_length=12, sent_length;
static char response_body[2048];
static int send_result;
static const char *TAG="host";
static void host_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
#define ESP_LOGW(...) host_log(__VA_ARGS__)
static int64_t esp_timer_get_time(void){return fake_us;}
static void record_interest(int64_t now){last_request_us=now;}
static int xSemaphoreTake(int semaphore,int timeout){(void)semaphore;(void)timeout;if(held)return 0;held=1;return 1;}
static void xSemaphoreGive(int semaphore){(void)semaphore;CHECK(held);held=0;}
static int setsockopt(int fd,int level,int option,const void *value,size_t len){CHECK(fd==4&&level==IPPROTO_TCP&&option==TCP_NODELAY&&len==sizeof(int)&&*(const int*)value==1);return socket_result;}
static int httpd_req_get_url_query_str(httpd_req_t *r,char *out,size_t cap){if(!r->query||strlen(r->query)>=cap)return ESP_FAIL;strcpy(out,r->query);return ESP_OK;}
static int httpd_query_key_value(const char *q,const char *key,char *out,size_t cap){
    size_t k=strlen(key);const char *p=q;
    while(*p){const char *end=strchr(p,'&');if(!end)end=p+strlen(p);
        if((size_t)(end-p)>k&&memcmp(p,key,k)==0&&p[k]=='='){
            size_t n=(size_t)(end-p)-k-1;if(n>=cap)return ESP_FAIL;memcpy(out,p+k+1,n);out[n]=0;return ESP_OK;}
        p=*end?end+1:end;}return ESP_FAIL;
}
static int httpd_resp_set_hdr(httpd_req_t *r,const char *key,const char *v){if(!strcmp(key,"X-Frame-State"))snprintf(r->state,sizeof(r->state),"%s",v);return ESP_OK;}
static int httpd_resp_set_status(httpd_req_t *r,const char *v){r->status=atoi(v);return ESP_OK;}
static int httpd_resp_set_type(httpd_req_t *r,const char *v){(void)r;(void)v;return ESP_OK;}
static int httpd_resp_send(httpd_req_t *r,const char *body,size_t len){(void)r;(void)body;CHECK(!critical&&!held);sent_length=len;fake_us+=3000;return send_result;}
static int httpd_resp_sendstr(httpd_req_t *r,const char *body){(void)r;CHECK(!critical&&!held);CHECK(strlen(body)<sizeof(response_body));strcpy(response_body,body);return ESP_OK;}
static unsigned esp_get_free_heap_size(void){return 100000;}
static unsigned heap_caps_get_free_size(int kind){(void)kind;return 50000;}
#include "preview_backend.inc"

static httpd_req_t frame(const char *query,int64_t age){
    httpd_req_t r={.query=query,.status=200};published_received_us=fake_us-age;
    CHECK(frame_handler(&r)==ESP_OK);return r;
}
int main(void){
    CHECK(preview_connection_open(NULL,4)==ESP_OK);socket_result=-1;CHECK(preview_connection_open(NULL,4)==ESP_FAIL);socket_result=0;
    httpd_req_t r=frame("after=7&boot=1234abcd",1000);CHECK(r.status==204&&!strcmp(r.state,"duplicate")&&sent_length==0);
    r=frame("after=7&boot=aaaaaaaa",1000);CHECK(r.status==200&&sent_length==12&&!strcmp(r.state,"fresh"));
    r=frame("after=6&boot=1234abcd",250000);CHECK(r.status==200&&sent_length==12);
    r=frame("after=6&boot=1234abcd",250001);CHECK(r.status==204&&!strcmp(r.state,"expired"));
    r=frame("after=6&boot=1234abcd",-1);CHECK(r.status==204&&!strcmp(r.state,"expired"));
    const char *invalid[]={"after=-1&boot=1234abcd","after=7x&boot=1234abcd","after=4294967296&boot=1234abcd","after=7"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){r=frame(invalid[i],1000);CHECK(r.status==200&&sent_length==12);}
    preview_ready=false;r=frame(NULL,1000);CHECK(r.status==503);preview_ready=true;
    CHECK(last_send_us==3000&&max_send_us==3000&&sent_frames==6&&empty_responses==3);
    send_result=ESP_FAIL;r=(httpd_req_t){0};CHECK(send_preview_response(&r,"x",1)==ESP_FAIL&&send_failures==1);send_result=ESP_OK;
    CHECK(status_handler(&r)==ESP_OK);printf("STATUS_COLD %s\n",response_body);
    wifi_preview_lights_t lamps={.epoch=9,.enabled=true,.batch_active=true,.selected=0,.four_color=0,.sampled_us=fake_us,
      .channels={{.state=2,.multiplier=4,.on=true},{.state=1,.multiplier=2,.on=true},{.state=3,.multiplier=2,.on=false}}};
    wifi_preview_publish_lights(&lamps);lamps.channels[0].state=0; /* Snapshot must be a copy. */
    CHECK(status_handler(&r)==ESP_OK);printf("STATUS_LIVE %s\n",response_body);
    fake_us+=1000001;CHECK(status_handler(&r)==ESP_OK);printf("STATUS_STALE %s\n",response_body);
    /* Exact production MJPEG publication: owned copy, pacing and bounded drops. */
    fake_us=10000000;last_request_us=fake_us;published_length=0;published_sequence=7;
    unsigned char packet[12]="JPEG-frame";
    wifi_preview_submit_mjpeg(packet,sizeof(packet),fake_us);
    CHECK(published_sequence==8&&published_length==12&&!memcmp(published_buffer,packet,12));
    packet[0]='X';CHECK(published_buffer[0]=='J');
    wifi_preview_submit_mjpeg(packet,sizeof(packet),fake_us+1000);CHECK(published_sequence==8);
    wifi_preview_submit_mjpeg(packet,sizeof(packet),fake_us+40000);CHECK(published_sequence==9);
    held=1;wifi_preview_submit_mjpeg(packet,sizeof(packet),fake_us+80000);held=0;CHECK(published_sequence==9);
    wifi_preview_submit_mjpeg(packet,PREVIEW_JPEG_CAPACITY+1,fake_us+120000);CHECK(published_sequence==9);
    transport_failed=true;wifi_preview_submit_mjpeg(packet,sizeof(packet),fake_us+160000);CHECK(published_sequence==9);
    printf("PASS backend assertions=%u (exact production C handlers)\n",checks);return 0;
}
