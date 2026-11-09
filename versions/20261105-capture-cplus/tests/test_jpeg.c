/* SPDX-License-Identifier: Apache-2.0 */
#include "rm_jpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) {fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);} } while(0)
int main(int argc,char **argv)
{
    CHECK(argc==4);
    FILE *input=fopen(argv[1],"rb");CHECK(input);
    CHECK(fseek(input,0,SEEK_END)==0);long size=ftell(input);CHECK(size>0);rewind(input);
    unsigned char *source=malloc((size_t)size+8),*output=malloc((size_t)size+1024);CHECK(source&&output);
    CHECK(fread(source,1,(size_t)size,input)==(size_t)size);fclose(input);
    rm_jpeg_info_t info;bool missing=atoi(argv[3])!=0;
    CHECK(rm_jpeg_normalize(source,(size_t)size,output,(size_t)size+1024,640,480,&info));
    CHECK(info.width==640&&info.height==480&&info.inserted_standard_dht==missing);
    CHECK(info.length==(size_t)size+(missing?432:0));
    FILE *target=fopen(argv[2],"wb");CHECK(target);CHECK(fwrite(output,1,info.length,target)==info.length);fclose(target);
    CHECK(!rm_jpeg_normalize(source,(size_t)size-2,output,(size_t)size+1024,640,480,&info));
    CHECK(!rm_jpeg_normalize(source,(size_t)size,output,10,640,480,&info));
    CHECK(!rm_jpeg_normalize(source,(size_t)size,output,(size_t)size+1024,320,240,&info));
    memset(source+size,0,8);
    CHECK(rm_jpeg_normalize(source,(size_t)size+8,output,(size_t)size+1024,640,480,&info));
    CHECK(info.length==(size_t)size+(missing?432:0));
    unsigned char *pixels=malloc(640U*480U*3U),*half=malloc(320U*240U),*full=malloc(640U*480U);CHECK(pixels&&half&&full);
    for(size_t i=0;i<640U*480U;i++){pixels[i*3]=(unsigned char)(i%256);pixels[i*3+1]=pixels[i*3];pixels[i*3+2]=pixels[i*3];}
    CHECK(rm_jpeg_pixels_to_gray(pixels,640U*480U*3U,true,half,full));
    CHECK(full[639]==127&&half[319]==126&&half[320]==0);
    memset(half,0,320U*240U);CHECK(rm_jpeg_pixels_to_gray(pixels,640U*480U*3U,true,half,NULL));CHECK(half[319]==126);
    CHECK(!rm_jpeg_pixels_to_gray(pixels,1,true,half,NULL));
    free(pixels);free(half);free(full);free(source);free(output);
    printf("PASS JPEG assertions=%u missing_DHT=%d (exact production parser/pixel conversion)\n",checks,missing);
    return 0;
}
