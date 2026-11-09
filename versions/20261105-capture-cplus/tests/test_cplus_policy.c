/* SPDX-License-Identifier: Apache-2.0 */
#include "cp_frame_pool.h"
#include "cp_stream_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(int argc,char **argv)
{
    CHECK(argc==2);uint8_t storage[3][96];uint8_t *pointers[]={storage[0],storage[1],storage[2]};cp_pool_t pool;
    cp_pool_init(&pool,pointers,64);CHECK(cp_pool_free_slots(&pool)==3);
    int a=cp_pool_reserve(&pool);CHECK(a>=0);storage[a][32]=42;uint32_t ga=pool.slots[a].generation;
    CHECK(cp_pool_commit(&pool,a,10,1000));CHECK(cp_pool_publish(&pool,a));CHECK(cp_pool_release(&pool,a,ga));
    cp_frame_ref_t view;CHECK(cp_pool_acquire_latest(&pool,&view));CHECK(view.storage==storage[a]&&view.storage[32]==42);
    int b=cp_pool_reserve(&pool);CHECK(b>=0&&b!=a);uint32_t gb=pool.slots[b].generation;
    storage[b][32]=91;CHECK(cp_pool_commit(&pool,b,12,2000));CHECK(cp_pool_publish(&pool,b));CHECK(cp_pool_release(&pool,b,gb));
    int c=cp_pool_reserve(&pool);CHECK(c>=0&&c!=a&&c!=b);CHECK(cp_pool_reserve(&pool)==-1);
    CHECK(view.storage[32]==42);CHECK(cp_pool_release_view(&pool,&view));CHECK(!cp_pool_release_view(&pool,&view));
    CHECK(cp_pool_release(&pool,c,pool.slots[c].generation));CHECK(cp_pool_free_slots(&pool)==2);
    CHECK(!cp_pool_release(&pool,b,gb)); /* The published pin cannot be released as a reader. */
    cp_flow_t f;cp_flow_init(&f,60);CHECK(cp_flow_connect(&f,4,1000000));CHECK(!cp_flow_connect(&f,5,1000000));
    CHECK(cp_flow_credit(&f,1,0,true,1000000));CHECK(cp_flow_ready(&f,1000000));CHECK(cp_flow_begin(&f,1,1000000));
    CHECK(!cp_flow_ready(&f,1000001));uint32_t generation=f.generation;
    CHECK(cp_flow_finish(&f,generation,true,1001000,1000));CHECK(!cp_flow_credit(&f,2,99,true,1010000));
    CHECK(cp_flow_credit(&f,2,1,true,1010000));CHECK(!cp_flow_ready(&f,1010000));
    int64_t now=1040000;uint32_t token=2,sequence=2;
    for(unsigned i=0;i<200;++i){
        if(now<f.next_send_us)now=f.next_send_us;
        CHECK(cp_flow_begin(&f,sequence++,now));CHECK(cp_flow_finish(&f,generation,true,now+1000,1000));
        const uint32_t previous=token;++token;
        CHECK(cp_flow_credit(&f,token,previous,true,now+10000));now+=20000;
    }
    CHECK(f.target_fps==60);CHECK(f.acks==201);
    if(now<f.next_send_us)now=f.next_send_us;
    CHECK(cp_flow_begin(&f,sequence++,now));CHECK(cp_flow_finish(&f,generation,true,now+1000,1000));
    CHECK(cp_flow_credit(&f,token+1,token,false,now+10000));CHECK(f.target_fps==30);
    CHECK(cp_flow_expire(&f,now+700001));CHECK(f.target_fps==15&&f.timeouts==1);
    CHECK(!cp_flow_expire(&f,now+800000));cp_flow_disconnect(&f,4);CHECK(!f.connected);
    CHECK(cp_flow_connect(&f,4,now+900000));CHECK(!cp_flow_finish(&f,generation,true,now+900000,1));
    uint32_t t,ack;bool shown;
    CHECK(cp_parse_credit("N 7 6 1",7,&t,&ack,&shown));CHECK(t==7&&ack==6&&shown);
    CHECK(!cp_parse_credit("N -1 0 1",8,&t,&ack,&shown));CHECK(!cp_parse_credit("N 4294967296 0 1",16,&t,&ack,&shown));
    CHECK(!cp_parse_credit("N 1 0 2",7,&t,&ack,&shown));CHECK(!cp_parse_credit("N 0 0 1",7,&t,&ack,&shown));
    uint8_t header[32];cp_packet_header(header,0x1234abcd,91,7,12,123);
    FILE *out=fopen(argv[1],"wb");CHECK(out);CHECK(fwrite(header,1,32,out)==32);fclose(out);
    cp_recovery_t r;cp_recovery_arm(&r,0);CHECK(cp_recovery_take(&r,0));cp_recovery_result(&r,false,0);
    CHECK(!cp_recovery_take(&r,999999));CHECK(cp_recovery_take(&r,1000000));cp_recovery_result(&r,false,1000000);
    CHECK(cp_recovery_take(&r,3000000));cp_recovery_result(&r,false,3000000);CHECK(r.exhausted&&!r.pending);
    CHECK(!cp_recovery_take(&r,100000000));cp_recovery_arm(&r,200000000);CHECK(cp_recovery_take(&r,200000000));cp_recovery_result(&r,true,200000001);CHECK(!r.exhausted&&!r.pending);
    printf("PASS CPLUS policy assertions=%u (production slot pins, credits, adaptive pacing and three-attempt recovery)\n",checks);return 0;
}
