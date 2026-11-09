/* SPDX-License-Identifier: Apache-2.0 */
#include "rm_jpeg.h"
#include <string.h>
#include "rm_standard_dht.h"

static unsigned be16(const uint8_t *p) { return ((unsigned)p[0] << 8) | p[1]; }

bool rm_jpeg_normalize(const uint8_t *source, size_t length, uint8_t *output,
                       size_t capacity, unsigned width, unsigned height,
                       rm_jpeg_info_t *info)
{
    if (!source || !output || !info || length < 4 || source[0] != 0xff || source[1] != 0xd8)
        return false;
    size_t cursor = 2, scan_at = 0, end_at = 0;
    unsigned tables = 0, required = 0, components = 0, found_width = 0, found_height = 0;
    bool quant = false, sof = false;
    while (cursor + 1 < length) {
        const size_t marker_at = cursor;
        if (source[cursor++] != 0xff) return false;
        while (cursor < length && source[cursor] == 0xff) ++cursor;
        if (cursor >= length) return false;
        const unsigned marker = source[cursor++];
        if (!marker || marker == 0xd8 || marker == 0xd9 || (marker >= 0xd0 && marker <= 0xd7))
            return false;
        if (cursor + 2 > length) return false;
        const size_t segment = be16(source + cursor);
        if (segment < 2 || segment > length - cursor) return false;
        const uint8_t *payload = source + cursor + 2;
        const size_t payload_size = segment - 2;
        if (marker == 0xc0) {
            if (sof || payload_size < 6 || payload[0] != 8) return false;
            found_height = be16(payload + 1); found_width = be16(payload + 3);
            components = payload[5];
            if ((components != 1 && components != 3) || payload_size != 6 + 3 * components ||
                found_width != width || found_height != height) return false;
            sof = true;
        } else if (marker >= 0xc1 && marker <= 0xcf && marker != 0xc4) {
            return false; /* P4 baseline codec, no progressive/arithmetic JPEG. */
        } else if (marker == 0xdb) {
            quant = true;
        } else if (marker == 0xc4) {
            size_t offset = 0;
            while (offset < payload_size) {
                if (payload_size - offset < 17) return false;
                const unsigned selector = payload[offset];
                if ((selector & 0xeeU) != 0) return false;
                unsigned symbols = 0;
                for (unsigned i = 1; i <= 16; ++i) symbols += payload[offset + i];
                if (symbols > 256 || symbols > payload_size - offset - 17) return false;
                tables |= 1U << ((selector >> 4) * 2 + (selector & 1));
                offset += 17 + symbols;
            }
        } else if (marker == 0xda) {
            if (!sof || !quant || payload_size != 4 + 2 * components || payload[0] != components ||
                payload[payload_size - 3] != 0 || payload[payload_size - 2] != 63 ||
                payload[payload_size - 1] != 0) return false;
            for (unsigned i = 0; i < components; ++i) {
                const unsigned selector = payload[2 + i * 2];
                if ((selector >> 4) > 1 || (selector & 15) > 1) return false;
                required |= (1U << (selector >> 4)) | (1U << (2 + (selector & 15)));
            }
            if (tables && (tables & required) != required) return false;
            scan_at = marker_at;
            cursor += segment;
            while (cursor + 1 < length) {
                if (source[cursor++] != 0xff) continue;
                while (cursor < length && source[cursor] == 0xff) ++cursor;
                if (cursor >= length) return false;
                const unsigned code = source[cursor++];
                if (code == 0xd9) { end_at = cursor; break; }
                if (code != 0 && (code < 0xd0 || code > 0xd7)) return false;
            }
            break;
        }
        cursor += segment;
    }
    if (!scan_at || !end_at) return false;
    const size_t extra = tables ? 0 : sizeof(rm_standard_dht);
    if (end_at > capacity || extra > capacity - end_at) return false;
    /* Tail first also supports the in-place case. */
    memmove(output + scan_at + extra, source + scan_at, end_at - scan_at);
    memmove(output, source, scan_at);
    if (extra) memcpy(output + scan_at, rm_standard_dht, extra);
    *info = (rm_jpeg_info_t){.length=end_at + extra, .width=(uint16_t)found_width,
        .height=(uint16_t)found_height, .inserted_standard_dht=extra != 0};
    return true;
}

bool rm_jpeg_pixels_to_gray(const uint8_t *pixels, size_t length, bool rgb888,
                            uint8_t *half, uint8_t *full)
{
    const size_t stride = rgb888 ? 3 : 1;
    if (!pixels || !half || length < 640U * 480U * stride) return false;
    for (size_t y = 0; y < 480; ++y) {
        if (!full && (y & 1U)) continue;
        for (size_t x = 0; x < 640; ++x) {
            if (!full && (x & 1U)) continue;
            const uint8_t *p = pixels + (y * 640 + x) * stride;
            const uint8_t value = rgb888 ? (uint8_t)((77U*p[0] + 150U*p[1] + 29U*p[2] + 128U) >> 8) : p[0];
            if (full) full[y * 640 + x] = value;
            if (!(x & 1U) && !(y & 1U)) half[(y / 2) * 320 + x / 2] = value;
        }
    }
    return true;
}
