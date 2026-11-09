/* SPDX-License-Identifier: Apache-2.0 */
#include "cp_stream_policy.h"
#include "rm_capture_mode.h"
#include <string.h>
static void slow_down(cp_flow_t *f){f->target_fps=f->target_fps>10?f->target_fps/2:5;f->good_acks=0;}
void cp_flow_init(cp_flow_t *f,unsigned max_fps)
{
    memset(f,0,sizeof(*f));f->fd=-1;
    f->max_fps=max_fps<5?5:(max_fps>60?60:max_fps);
    f->target_fps=f->max_fps<30?f->max_fps:30;
}
bool cp_flow_connect(cp_flow_t *f,int fd,int64_t now)
{
    if(f->connected||fd<0)return false;
    ++f->generation;f->fd=fd;f->connected=true;f->credit=f->queued=f->waiting_ack=f->closing=f->have_sent=false;
    f->token=f->flight_token=0;f->next_send_us=0;f->last_progress_us=now;return true;
}
void cp_flow_disconnect(cp_flow_t *f,int fd)
{
    if(!f->connected||f->fd!=fd)return;
    ++f->generation;f->fd=-1;f->connected=f->credit=f->queued=f->waiting_ack=f->closing=false;
}
bool cp_flow_credit(cp_flow_t *f,uint32_t token,uint32_t ack,bool shown,int64_t now)
{
    if(!f->connected||f->closing||f->queued||f->credit||!token||token==f->token)return false;
    if(f->waiting_ack){
        if(ack!=f->flight_token)return false;
        const int64_t elapsed=now-f->last_send_us;if(elapsed<0)return false;
        f->last_ack_us=(uint32_t)elapsed;++f->acks;
        if(!shown||elapsed>80000)slow_down(f);
        else if(elapsed<35000){if(++f->good_acks>=30){
            if(f->target_fps<f->max_fps)f->target_fps=f->target_fps+5>f->max_fps?f->max_fps:f->target_fps+5;
            f->good_acks=0;}}
        else f->good_acks=0;
    }else if(ack||f->have_sent)return false;
    f->waiting_ack=false;f->credit=true;f->token=token;f->last_progress_us=f->credit_received_us=now;return true;
}
bool cp_flow_ready(const cp_flow_t *f,int64_t now)
{return f->connected&&!f->closing&&f->credit&&!f->queued&&!f->waiting_ack&&now>=f->next_send_us;}
bool cp_flow_begin(cp_flow_t *f,uint32_t sequence,int64_t now)
{
    if(!cp_flow_ready(f,now)||(f->have_sent&&sequence==f->sent_sequence))return false;
    if(!rm_capture_take_preview_slot(now,&f->next_send_us,f->target_fps))return false;
    f->queued=true;f->credit=false;f->flight_token=f->token;f->sent_sequence=sequence;return true;
}
bool cp_flow_finish(cp_flow_t *f,uint32_t generation,bool sent,int64_t now,uint32_t send_us)
{
    if(!f->connected||f->generation!=generation||!f->queued)return false;
    f->queued=false;f->last_progress_us=now;
    if(!sent){++f->send_errors;slow_down(f);f->closing=true;return false;}
    if(send_us>50000)slow_down(f);
    f->waiting_ack=true;f->have_sent=true;f->last_send_us=now;return true;
}
bool cp_flow_expire(cp_flow_t *f,int64_t now)
{
    if(!f->connected||f->closing||now-f->last_progress_us<=CP_CREDIT_TIMEOUT_US)return false;
    f->closing=true;++f->timeouts;slow_down(f);return true;
}
static bool number(const char *s,size_t length,size_t *at,uint32_t *value)
{
    uint64_t n=0;unsigned digits=0;
    while(*at<length&&s[*at]>='0'&&s[*at]<='9'){
        n=n*10+(unsigned)(s[(*at)++]-'0');if(++digits>10||n>UINT32_MAX)return false;
    }
    if(!digits)return false;
    *value=(uint32_t)n;return true;
}
bool cp_parse_credit(const char *s,size_t length,uint32_t *token,uint32_t *ack,bool *shown)
{
    if(!s||!token||!ack||!shown||length<7||length>63||s[0]!='N'||s[1]!=' ')return false;
    size_t at=2;uint32_t flag;
    if(!number(s,length,&at,token)||at>=length||s[at++]!=' ')return false;
    if(!number(s,length,&at,ack)||at>=length||s[at++]!=' ')return false;
    if(!number(s,length,&at,&flag)||flag>1||!*token)return false;
    while(at<length&&(s[at]=='\n'||s[at]=='\r'||s[at]==' '))++at;
    if(at!=length)return false;
    *shown=flag!=0;return true;
}
static void u32(uint8_t *p,uint32_t n){p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;}
void cp_packet_header(uint8_t out[CP_PACKET_HEADER],uint32_t boot,uint32_t sequence,uint32_t token,uint32_t age_ms,uint32_t length)
{
    memcpy(out,"RMCP",4);out[4]=1;out[5]=0;out[6]=0;out[7]=CP_PACKET_HEADER;
    u32(out+8,boot);u32(out+12,sequence);u32(out+16,token);u32(out+20,age_ms);u32(out+24,length);
    out[28]=2;out[29]=128;out[30]=1;out[31]=224; /* 640 x 480 */
}
void cp_recovery_arm(cp_recovery_t *r,int64_t now){*r=(cp_recovery_t){.pending=true,.next_us=now};}
bool cp_recovery_take(cp_recovery_t *r,int64_t now)
{
    if(!r->pending||r->exhausted||now<r->next_us)return false;
    r->pending=false;++r->attempts;return true;
}
void cp_recovery_result(cp_recovery_t *r,bool ok,int64_t now)
{
    if(ok){r->pending=false;r->exhausted=false;return;}
    if(r->attempts>=3){r->exhausted=true;r->pending=false;return;}
    r->pending=true;r->next_us=now+((int64_t)1<<(r->attempts-1))*1000000LL;
}
