/* SPDX-License-Identifier: Apache-2.0: exact SDK adapter with network/RTOS stubs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cp_frame_pool.h"
#include "cp_stream_policy.h"
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define ESP_OK 0
#define ESP_FAIL -1
#define HTTP_GET 1
#define HTTPD_WS_TYPE_TEXT 1
#define HTTPD_WS_TYPE_BINARY 2
#define RM_PREVIEW_JPEG_CAPACITY 262144U
typedef int esp_err_t;typedef void *httpd_handle_t;
typedef struct{int method,fd;const char *body;}httpd_req_t;
typedef struct{int type;uint8_t *payload;size_t len;}httpd_ws_frame_t;
static int stream_mux,interest_mux,critical,sender_task_handle=1;
#define portENTER_CRITICAL(p) do{(void)(p);CHECK(!critical);critical=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);CHECK(critical);critical=0;}while(0)
static int64_t clock_us=1000000;
static int64_t esp_timer_get_time(void){return clock_us;}
static int httpd_req_to_sockfd(httpd_req_t *r){return r->fd;}
static int httpd_ws_recv_frame(httpd_req_t *r,httpd_ws_frame_t *p,size_t cap){
    p->type=HTTPD_WS_TYPE_TEXT;p->len=strlen(r->body);if(cap){CHECK(p->len<=cap);memcpy(p->payload,r->body,p->len);}return ESP_OK;
}
static void xTaskNotifyGive(int handle){CHECK(handle==1);}
static int closed_fd=-1,close_requests;static void host_close(int fd){closed_fd=fd;}
#define close host_close
static cp_flow_t flow;static bool preview_ready=true,network_ready=true,transport_failed,job_busy;
static httpd_handle_t stream_server=(void*)1;
static uint32_t boot_id=0x1234abcd,sent_frames,send_failures,last_send_us,max_send_us;
typedef struct{cp_frame_ref_t frame;int fd;uint32_t generation,token;}cp_send_job_t;
static cp_send_job_t send_job;
static void(*queued_work)(void*);static void *queued_arg;
static int httpd_queue_work(httpd_handle_t server,void(*work)(void*),void *arg){CHECK(server==stream_server&&!queued_work);queued_work=work;queued_arg=arg;return ESP_OK;}
static int httpd_sess_trigger_close(httpd_handle_t server,int fd){CHECK(server==stream_server);closed_fd=fd;++close_requests;return ESP_OK;}
static cp_pool_t pool;static unsigned char stores[3][96];static int send_fail;static unsigned sends;
static bool rm_mjpeg_acquire_latest(cp_frame_ref_t *v){return cp_pool_acquire_latest(&pool,v);}
static void rm_mjpeg_release_view(cp_frame_ref_t *v){CHECK(cp_pool_release_view(&pool,v));}
static int httpd_ws_send_frame_async(httpd_handle_t server,int fd,httpd_ws_frame_t *p){
    CHECK(server==stream_server&&fd==4&&p->type==HTTPD_WS_TYPE_BINARY);CHECK(job_busy&&send_job.frame.active);
    CHECK(p->payload==send_job.frame.storage&&p->len==44);CHECK(!memcmp(p->payload,"RMCP",4));CHECK(p->payload[32]==42);
    ++sends;clock_us+=3000;return send_fail?ESP_FAIL:ESP_OK;
}
#include "cplus_adapter.inc"
static void publish(int64_t received,unsigned char value){
    int i=cp_pool_reserve(&pool);CHECK(i>=0);stores[i][32]=value;uint32_t generation=pool.slots[i].generation;
    CHECK(cp_pool_commit(&pool,i,12,received));CHECK(cp_pool_publish(&pool,i));CHECK(cp_pool_release(&pool,i,generation));
}
static void run_work(void){void(*work)(void*)=queued_work;void *arg=queued_arg;queued_work=NULL;queued_arg=NULL;CHECK(work);work(arg);}
int main(void){
    uint8_t *buffers[]={stores[0],stores[1],stores[2]};cp_pool_init(&pool,buffers,64);cp_flow_init(&flow,60);
    httpd_req_t req={.method=HTTP_GET,.fd=4};CHECK(stream_handler(&req)==ESP_OK);
    req.method=2;req.body="N 1 0 1";CHECK(stream_handler(&req)==ESP_OK);
    publish(clock_us-1000,9);sender_tick(clock_us);CHECK(!queued_work); /* Pre-credit images cannot be sent. */
    clock_us+=1000;publish(clock_us,42);sender_tick(clock_us);CHECK(queued_work&&job_busy);
    CHECK(send_job.frame.active);publish(clock_us+1,91); /* Old network lease survives a newer publication. */
    run_work();CHECK(!job_busy&&flow.waiting_ack&&sends==1&&sent_frames==1&&last_send_us==3000);
    CHECK(cp_pool_free_slots(&pool)==2);
    req.body="N 2 1 1";clock_us+=5000;CHECK(stream_handler(&req)==ESP_OK);
    clock_us+=40000;publish(clock_us,42);sender_tick(clock_us);CHECK(queued_work);send_fail=1;run_work();
    CHECK(send_failures==1&&flow.closing&&close_requests==1&&closed_fd==4);CHECK(cp_pool_free_slots(&pool)==2);
    stream_close(stream_server,4);CHECK(!flow.connected&&closed_fd==4);
    printf("PASS CPLUS adapter assertions=%u (actual WebSocket handler/send work, immutable lease and error release)\n",checks);return 0;
}
