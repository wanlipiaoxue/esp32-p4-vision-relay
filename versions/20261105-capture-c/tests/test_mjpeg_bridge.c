/* SPDX-License-Identifier: Apache-2.0
 * Production bridge with hardware JPEG/RTOS API stubs, not real decoding. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rm_mjpeg.h"
#include "driver/jpeg_decode.h"
static unsigned checks,decode_calls;static int fail_decode,gray_input;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static long long clock_us;
long long esp_timer_get_time(void){return clock_us+=1000;}
void *jpeg_alloc_decoder_mem(size_t size,const jpeg_decode_memory_alloc_cfg_t *cfg,size_t *actual){(void)cfg;*actual=size;return malloc(size);}
int jpeg_new_decoder_engine(const jpeg_decode_engine_cfg_t *cfg,jpeg_decoder_handle_t *out){CHECK(cfg->timeout_ms==40);*out=(void*)1;return ESP_OK;}
int jpeg_decoder_get_info(const unsigned char *in,unsigned length,jpeg_decode_picture_info_t *out){CHECK(in&&length>2);*out=(jpeg_decode_picture_info_t){.width=640,.height=480,.sample_method=gray_input?JPEG_DOWN_SAMPLING_GRAY:2};return ESP_OK;}
int jpeg_decoder_process(jpeg_decoder_handle_t h,const jpeg_decode_cfg_t *cfg,const unsigned char *in,unsigned length,unsigned char *out,unsigned cap,unsigned *used){
    CHECK(h&&in&&length>2&&cap>=640U*480U*3U);++decode_calls;if(fail_decode)return ESP_ERR_INVALID_ARG;
    CHECK(cfg->output_format==(gray_input?JPEG_DECODE_OUT_FORMAT_GRAY:JPEG_DECODE_OUT_FORMAT_RGB888));
    CHECK(cfg->rgb_order==JPEG_DEC_RGB_ELEMENT_ORDER_RGB);
    for(unsigned i=0;i<640U*480U;i++){
        unsigned char value=(unsigned char)(i%256);
        if(gray_input)out[i]=value;else out[i*3]=out[i*3+1]=out[i*3+2]=value;
    }
    *used=640U*480U*(gray_input?1:3);return ESP_OK;
}
static unsigned char *load(const char *path,size_t *length){FILE *f=fopen(path,"rb");CHECK(f);fseek(f,0,SEEK_END);*length=(size_t)ftell(f);rewind(f);unsigned char *data=malloc(*length);CHECK(data);CHECK(fread(data,1,*length,f)==*length);fclose(f);return data;}
int main(int argc,char **argv){
    CHECK(argc==3);CHECK(rm_mjpeg_init());CHECK(rm_mjpeg_init());
    unsigned char *half=malloc(320U*240U),*full=malloc(640U*480U);CHECK(half&&full);
    size_t length,prepared_length;unsigned char *data=load(argv[1],&length);
    CHECK(rm_mjpeg_prepare(data,length,&prepared_length));CHECK(prepared_length==length+432);
    CHECK(rm_mjpeg_copy_gray(half,full));CHECK(full[639]==127&&half[319]==126);
    rm_mjpeg_stats_t stats;rm_mjpeg_get_stats(&stats);CHECK(stats.ready&&stats.prepared==1&&stats.dht_insertions==1&&stats.decoded==1);
    memset(half,42,320U*240U);fail_decode=1;CHECK(!rm_mjpeg_copy_gray(half,NULL));CHECK(half[0]==42);fail_decode=0;
    CHECK(!rm_mjpeg_prepare(data,4,&prepared_length));CHECK(prepared_length==0);unsigned calls=decode_calls;
    CHECK(!rm_mjpeg_copy_gray(half,NULL));CHECK(decode_calls==calls);free(data);
    data=load(argv[2],&length);gray_input=1;CHECK(rm_mjpeg_prepare(data,length,&prepared_length));CHECK(rm_mjpeg_copy_gray(half,full));CHECK(full[639]==127&&half[319]==126);
    rm_mjpeg_get_stats(&stats);CHECK(stats.decoded==2&&stats.decode_errors==1&&stats.prepare_errors==1);
    free(data);free(half);free(full);printf("PASS MJPEG bridge assertions=%u (production C; JPEG hardware stubbed)\n",checks);return 0;
}
