/* SPDX-License-Identifier: Apache-2.0 */
#include "rm_capture_mode.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) {fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);} } while(0)
int main(void)
{
    uint32_t interval; bool above;
    uint32_t rates[]={1000000,666666,500000,333333,166666};
    rm_capture_intervals_t caps={.default_interval=1000000,.intervals=rates,.count=5};
    CHECK(rm_capture_choose_interval(&caps,30,&interval,&above)); CHECK(interval==333333&&!above);
    CHECK(rm_capture_choose_interval(&caps,20,&interval,&above)); CHECK(interval==500000&&!above);
    CHECK(rm_capture_choose_interval(&caps,15,&interval,&above)); CHECK(interval==666666&&!above);
    caps.count=1; CHECK(rm_capture_choose_interval(&caps,30,&interval,&above)); CHECK(interval==1000000&&!above);
    caps=(rm_capture_intervals_t){.default_interval=166666,.intervals=rates+4,.count=1};
    CHECK(rm_capture_choose_interval(&caps,30,&interval,&above)); CHECK(interval==166666&&above);
    caps=(rm_capture_intervals_t){.continuous=true,.minimum=166666,.maximum=1000000,.step=100000,.default_interval=966666};
    CHECK(rm_capture_choose_interval(&caps,30,&interval,&above)); CHECK(interval==366666&&!above);
    caps.minimum=500000;caps.maximum=1000000;caps.step=0;
    CHECK(rm_capture_choose_interval(&caps,30,&interval,&above)); CHECK(interval==500000&&!above);
    caps.minimum=100;caps.maximum=50; CHECK(!rm_capture_choose_interval(&caps,30,&interval,&above));
    CHECK(!rm_capture_choose_interval(NULL,30,&interval,&above));
    CHECK(!rm_capture_choose_interval(&caps,0,&interval,&above));
    caps=(rm_capture_intervals_t){0}; CHECK(!rm_capture_choose_interval(&caps,30,&interval,&above));
    /* Every discrete entry is available, including ones beyond the old array of 3. */
    uint32_t many[255];for(unsigned i=0;i<255;++i)many[i]=2000000-i*6000;
    many[254]=333333;caps=(rm_capture_intervals_t){.intervals=many,.count=255,.default_interval=2000000};
    CHECK(rm_capture_choose_interval(&caps,30,&interval,&above)); CHECK(interval==333333);
    for(unsigned input_fps=30;input_fps<=60;input_fps+=30){
        int64_t next=0;unsigned accepted=0;
        for(unsigned i=0;i<input_fps*10;i++)
            accepted+=rm_capture_take_preview_slot(1000000+(int64_t)i*1000000/input_fps,&next,30);
        CHECK(accepted>=298&&accepted<=301);
    }
    int64_t next=0;CHECK(rm_capture_take_preview_slot(1000000,&next,30));
    CHECK(!rm_capture_take_preview_slot(1000001,&next,30));
    CHECK(rm_capture_take_preview_slot(5000000,&next,30));CHECK(next==5033333);
    CHECK(!rm_capture_take_preview_slot(0,&next,0));
    printf("PASS capture policy assertions=%u (advertised intervals and paced 30/60 FPS input)\n",checks);
    return 0;
}
