/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "rm_relay.h"

/* Pre-match role colors. Edit these three defaults, then rebuild.
 * All roles must use distinct colors; P3=4x, P2/P1=2x, rank P3>P2>P1. */
#ifndef RM_P1_COLOR
#define RM_P1_COLOR RM_RED
#endif
#ifndef RM_P2_COLOR
#define RM_P2_COLOR RM_BLUE
#endif
#ifndef RM_P3_COLOR
#define RM_P3_COLOR RM_GREEN
#endif

/* P4-NANO schematic: ENTER=P1.12, DONE=P1.11. No four-times pulse input. */
#define RM_ENTER_GPIO       4
#define RM_BATCH_DONE_GPIO  5

/* CODE DEFAULT ONLY. Actual relay module polarity has NOT been confirmed.
 * Change each channel here before any separately authorized hardware test.
 * Logical OFF is always converted through rm_relay_level(), never assumed 0. */
#ifndef RM_RED_ACTIVE_LEVEL
#define RM_RED_ACTIVE_LEVEL    1
#endif
#ifndef RM_GREEN_ACTIVE_LEVEL
#define RM_GREEN_ACTIVE_LEVEL  1
#endif
#ifndef RM_BLUE_ACTIVE_LEVEL
#define RM_BLUE_ACTIVE_LEVEL   1
#endif

/* Off in this candidate. Optional E/D commands inject ENTER/DONE,
 * never a TagID or LOCK, through rm_relay_event(). */
#ifndef RM_UART_EVENT_INJECTION
#define RM_UART_EVENT_INJECTION 0
#endif

#if (RM_RED_ACTIVE_LEVEL != 0 && RM_RED_ACTIVE_LEVEL != 1) || \
    (RM_GREEN_ACTIVE_LEVEL != 0 && RM_GREEN_ACTIVE_LEVEL != 1) || \
    (RM_BLUE_ACTIVE_LEVEL != 0 && RM_BLUE_ACTIVE_LEVEL != 1)
#error "Relay active levels must be 0 or 1"
#endif
