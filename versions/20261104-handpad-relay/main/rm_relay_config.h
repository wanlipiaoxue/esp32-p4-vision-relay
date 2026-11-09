/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* P4-NANO schematic: ENTER=P1.12, DONE=P1.11, NEXT_FOUR=P2.12.
 * These are P4 GPIOs, not the neighboring C6 pins. See candidate delivery notes. */
#define RM_ENTER_GPIO       4
#define RM_BATCH_DONE_GPIO  5
#define RM_NEXT_FOUR_GPIO   6

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

/* Off in this candidate. Optional E/D/F commands inject ENTER/DONE/NEXT_FOUR,
 * never a TagID or LOCK, through rm_relay_event(). */
#ifndef RM_UART_EVENT_INJECTION
#define RM_UART_EVENT_INJECTION 0
#endif

#if (RM_RED_ACTIVE_LEVEL != 0 && RM_RED_ACTIVE_LEVEL != 1) || \
    (RM_GREEN_ACTIVE_LEVEL != 0 && RM_GREEN_ACTIVE_LEVEL != 1) || \
    (RM_BLUE_ACTIVE_LEVEL != 0 && RM_BLUE_ACTIVE_LEVEL != 1)
#error "Relay active levels must be 0 or 1"
#endif
