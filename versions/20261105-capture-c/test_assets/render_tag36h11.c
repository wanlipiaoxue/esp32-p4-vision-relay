#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "apriltag.h"
#include "common/image_u8.h"
#include "tag36h11.h"

#define OUTPUT_WIDTH 320
#define OUTPUT_HEIGHT 240

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <id> <output.pgm>\n", argv[0]);
        return 2;
    }

    char *end = NULL;
    errno = 0;
    const long requested_id = strtol(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0') {
        fprintf(stderr, "invalid tag id: %s\n", argv[1]);
        return 3;
    }

    apriltag_family_t *family = tag36h11_create();
    if (!family || requested_id < 0 || requested_id >= (long)family->ncodes) {
        fprintf(stderr, "tag id out of range: %ld\n", requested_id);
        tag36h11_destroy(family);
        return 4;
    }

    image_u8_t *tag = apriltag_to_image(family, (uint32_t)requested_id);
    if (!tag) {
        tag36h11_destroy(family);
        return 5;
    }

    FILE *output = fopen(argv[2], "wb");
    if (!output) {
        perror(argv[2]);
        image_u8_destroy(tag);
        tag36h11_destroy(family);
        return 6;
    }

    fprintf(output, "P5\n%d %d\n255\n", OUTPUT_WIDTH, OUTPUT_HEIGHT);
    const int scale = 20;
    const int origin_x = (OUTPUT_WIDTH - tag->width * scale) / 2;
    const int origin_y = (OUTPUT_HEIGHT - tag->height * scale) / 2;
    for (int y = 0; y < OUTPUT_HEIGHT; ++y) {
        for (int x = 0; x < OUTPUT_WIDTH; ++x) {
            const int tag_x = (x - origin_x) / scale;
            const int tag_y = (y - origin_y) / scale;
            const uint8_t value =
                tag_x >= 0 && tag_x < tag->width &&
                tag_y >= 0 && tag_y < tag->height
                    ? tag->buf[tag_y * tag->stride + tag_x]
                    : 255;
            fputc(value, output);
        }
    }

    const int close_result = fclose(output);
    image_u8_destroy(tag);
    tag36h11_destroy(family);
    if (close_result != 0) {
        perror(argv[2]);
        return 7;
    }
    return 0;
}
