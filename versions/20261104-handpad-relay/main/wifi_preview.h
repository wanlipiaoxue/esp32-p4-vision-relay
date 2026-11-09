#pragma once
#include <stddef.h>
#include <stdint.h>
void wifi_preview_start(void);
void wifi_preview_submit_yuy2(const uint8_t *data, size_t length, int64_t received_us);
