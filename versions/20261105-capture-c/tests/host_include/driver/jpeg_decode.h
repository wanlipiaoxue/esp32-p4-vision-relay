#pragma once
#include <stddef.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define JPEG_DEC_ALLOC_INPUT_BUFFER 0
#define JPEG_DEC_ALLOC_OUTPUT_BUFFER 1
#define JPEG_DECODE_OUT_FORMAT_GRAY 0
#define JPEG_DECODE_OUT_FORMAT_RGB888 1
#define JPEG_DEC_RGB_ELEMENT_ORDER_RGB 1
#define JPEG_YUV_RGB_CONV_STD_BT601 0
#define JPEG_DOWN_SAMPLING_GRAY 1
typedef int esp_err_t;
typedef void *jpeg_decoder_handle_t;
typedef struct {int buffer_direction;} jpeg_decode_memory_alloc_cfg_t;
typedef struct {int intr_priority,timeout_ms;} jpeg_decode_engine_cfg_t;
typedef struct {unsigned width,height;int sample_method;} jpeg_decode_picture_info_t;
typedef struct {int output_format,rgb_order,conv_std;} jpeg_decode_cfg_t;
void *jpeg_alloc_decoder_mem(size_t,const jpeg_decode_memory_alloc_cfg_t*,size_t*);
int jpeg_new_decoder_engine(const jpeg_decode_engine_cfg_t*,jpeg_decoder_handle_t*);
int jpeg_decoder_get_info(const unsigned char*,unsigned,jpeg_decode_picture_info_t*);
int jpeg_decoder_process(jpeg_decoder_handle_t,const jpeg_decode_cfg_t*,const unsigned char*,unsigned,unsigned char*,unsigned,unsigned*);
