/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cp_frame_pool.h"
#define CP_FRAME_MAX_AGE_US 250000LL
#define CP_CREDIT_TIMEOUT_US 600000LL
typedef struct {
    int fd;
    uint32_t generation, token, flight_token, sent_sequence;
    unsigned target_fps, max_fps, good_acks;
    bool connected, credit, queued, waiting_ack, closing, have_sent;
    int64_t next_send_us,last_progress_us,last_send_us,credit_received_us;
    uint32_t acks,timeouts,send_errors,last_ack_us;
} cp_flow_t;
void cp_flow_init(cp_flow_t *flow,unsigned max_fps);
bool cp_flow_connect(cp_flow_t *flow,int fd,int64_t now);
void cp_flow_disconnect(cp_flow_t *flow,int fd);
bool cp_flow_credit(cp_flow_t *flow,uint32_t token,uint32_t ack,bool shown,int64_t now);
bool cp_flow_ready(const cp_flow_t *flow,int64_t now);
bool cp_flow_begin(cp_flow_t *flow,uint32_t sequence,int64_t now);
bool cp_flow_finish(cp_flow_t *flow,uint32_t generation,bool sent,int64_t now,uint32_t send_us);
bool cp_flow_expire(cp_flow_t *flow,int64_t now);
bool cp_parse_credit(const char *text,size_t length,uint32_t *token,uint32_t *ack,bool *shown);
void cp_packet_header(uint8_t out[CP_PACKET_HEADER],uint32_t boot,uint32_t sequence,
                       uint32_t token,uint32_t age_ms,uint32_t length);
typedef struct {unsigned attempts;bool pending,exhausted;int64_t next_us;} cp_recovery_t;
void cp_recovery_arm(cp_recovery_t *r,int64_t now);
bool cp_recovery_take(cp_recovery_t *r,int64_t now);
void cp_recovery_result(cp_recovery_t *r,bool ok,int64_t now);
