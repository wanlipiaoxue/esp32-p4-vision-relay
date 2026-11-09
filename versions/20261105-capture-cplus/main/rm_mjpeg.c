/* SPDX-License-Identifier: Apache-2.0 */
#include "rm_mjpeg.h"
#include "rm_jpeg.h"
#include "rm_capture_config.h"
#include <stdlib.h>
#include "driver/jpeg_decode.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
static const char *TAG="RM_MJPEG";
static jpeg_decoder_handle_t decoder;
static uint8_t *slot_storage[CP_POOL_SLOTS],*pixel_buffer,*input_buffer;
static size_t pixel_capacity,input_length;
static int prepared_slot=-1;
static uint32_t prepared_generation;
static cp_pool_t pool;
static portMUX_TYPE stats_mux=portMUX_INITIALIZER_UNLOCKED;
static rm_mjpeg_stats_t stats;

bool rm_mjpeg_init(void)
{
    if(stats.ready)return true;
    const jpeg_decode_memory_alloc_cfg_t in_cfg={.buffer_direction=JPEG_DEC_ALLOC_INPUT_BUFFER};
    const jpeg_decode_memory_alloc_cfg_t out_cfg={.buffer_direction=JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    const size_t wanted=RM_CAPTURE_FRAME_CAPACITY+1024U;
    bool allocated=true;
    for(unsigned i=0;i<CP_POOL_SLOTS;++i){
        size_t capacity;
        slot_storage[i]=jpeg_alloc_decoder_mem(wanted+CP_PACKET_HEADER,&in_cfg,&capacity);
        if(!slot_storage[i]||capacity<wanted+CP_PACKET_HEADER)allocated=false;
    }
    pixel_buffer=jpeg_alloc_decoder_mem(640U*480U*3U,&out_cfg,&pixel_capacity);
    const jpeg_decode_engine_cfg_t cfg={.intr_priority=1,.timeout_ms=40};
    if(!allocated||!pixel_buffer||jpeg_new_decoder_engine(&cfg,&decoder)!=ESP_OK){
        for(unsigned i=0;i<CP_POOL_SLOTS;++i){free(slot_storage[i]);slot_storage[i]=NULL;}
        free(pixel_buffer);pixel_buffer=NULL;
        ESP_LOGE(TAG,"JPEG pool/decoder initialization failed; capture will not start");return false;
    }
    portENTER_CRITICAL(&stats_mux);
    cp_pool_init(&pool,slot_storage,wanted);stats.ready=true;
    portEXIT_CRITICAL(&stats_mux);return true;
}

const uint8_t *rm_mjpeg_prepare(const uint8_t *source,size_t length,int64_t received_us,size_t *prepared_length)
{
    input_length=0;input_buffer=NULL;if(prepared_length)*prepared_length=0;
    if(!stats.ready||!prepared_length)return NULL;
    portENTER_CRITICAL(&stats_mux);
    if(prepared_slot>=0&&!cp_pool_release(&pool,prepared_slot,prepared_generation))++stats.pool_errors;
    prepared_slot=-1;
    const int slot=cp_pool_reserve(&pool);
    if(slot<0)++stats.pool_busy;
    const uint32_t generation=slot>=0?pool.slots[slot].generation:0;
    portEXIT_CRITICAL(&stats_mux);
    if(slot<0)return NULL;
    uint8_t *destination=slot_storage[slot]+CP_PACKET_HEADER;
    rm_jpeg_info_t info;
    bool valid=length<=RM_CAPTURE_FRAME_CAPACITY&&
        rm_jpeg_normalize(source,length,destination,RM_CAPTURE_FRAME_CAPACITY+1024U,640,480,&info);
    portENTER_CRITICAL(&stats_mux);
    if(valid)valid=cp_pool_commit(&pool,slot,info.length,received_us);
    if(valid){++stats.prepared;stats.dht_insertions+=info.inserted_standard_dht;}
    else {++stats.prepare_errors;if(!cp_pool_release(&pool,slot,generation))++stats.pool_errors;}
    portEXIT_CRITICAL(&stats_mux);
    if(!valid)return NULL;
    prepared_slot=slot;prepared_generation=generation;input_buffer=destination;input_length=info.length;
    *prepared_length=input_length;return input_buffer;
}

bool rm_mjpeg_publish_current(const uint8_t *data,size_t length)
{
    if(!stats.ready||prepared_slot<0||data!=input_buffer||length!=input_length)return false;
    portENTER_CRITICAL(&stats_mux);
    const bool ok=cp_pool_publish(&pool,prepared_slot);
    if(!ok)++stats.pool_errors;
    portEXIT_CRITICAL(&stats_mux);return ok;
}
bool rm_mjpeg_acquire_latest(cp_frame_ref_t *view)
{
    portENTER_CRITICAL(&stats_mux);
    const bool ok=stats.ready&&cp_pool_acquire_latest(&pool,view);
    portEXIT_CRITICAL(&stats_mux);return ok;
}
void rm_mjpeg_release_view(cp_frame_ref_t *view)
{
    portENTER_CRITICAL(&stats_mux);
    if(!cp_pool_release_view(&pool,view))++stats.pool_errors;
    portEXIT_CRITICAL(&stats_mux);
}
bool rm_mjpeg_copy_gray(uint8_t *half,uint8_t *full)
{
    if(!stats.ready||!input_length||!half)return false;
    const int64_t started=esp_timer_get_time();jpeg_decode_picture_info_t info;
    esp_err_t err=jpeg_decoder_get_info(input_buffer,(uint32_t)input_length,&info);
    bool gray=false;uint32_t output_length=0;
    if(err==ESP_OK&&info.width==640&&info.height==480){
        gray=info.sample_method==JPEG_DOWN_SAMPLING_GRAY;
        /* The pinned SDK has no color-JPEG->GRAY DMA entry: preserve RGB888. */
        const jpeg_decode_cfg_t cfg={.output_format=gray?JPEG_DECODE_OUT_FORMAT_GRAY:JPEG_DECODE_OUT_FORMAT_RGB888,
            .rgb_order=JPEG_DEC_RGB_ELEMENT_ORDER_RGB,.conv_std=JPEG_YUV_RGB_CONV_STD_BT601};
        err=jpeg_decoder_process(decoder,&cfg,input_buffer,(uint32_t)input_length,pixel_buffer,(uint32_t)pixel_capacity,&output_length);
    }else if(err==ESP_OK)err=ESP_ERR_INVALID_ARG;
    const bool ok=err==ESP_OK&&rm_jpeg_pixels_to_gray(pixel_buffer,output_length,!gray,half,full);
    portENTER_CRITICAL(&stats_mux);
    stats.last_decode_us=(uint32_t)(esp_timer_get_time()-started);
    if(ok)++stats.decoded;else ++stats.decode_errors;
    portEXIT_CRITICAL(&stats_mux);return ok;
}
void rm_mjpeg_get_stats(rm_mjpeg_stats_t *snapshot)
{
    if(!snapshot)return;
    portENTER_CRITICAL(&stats_mux);
    *snapshot=stats;snapshot->free_slots=stats.ready?cp_pool_free_slots(&pool):0;
    portEXIT_CRITICAL(&stats_mux);
}
