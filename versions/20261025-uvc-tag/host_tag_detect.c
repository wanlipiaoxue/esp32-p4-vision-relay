#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apriltag.h"
#include "tag36h11.h"
#include "common/image_u8.h"
#include "common/zarray.h"

static int read_pgm(const char *path, image_u8_t **out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    char magic[3] = {0};
    int width = 0;
    int height = 0;
    int max_value = 0;
    if (fscanf(f, "%2s %d %d %d", magic, &width, &height, &max_value) != 4 ||
        strcmp(magic, "P5") != 0 || max_value != 255 || width <= 0 || height <= 0) {
        fclose(f);
        return -2;
    }
    int separator = fgetc(f);
    if (separator != ' ' && separator != '\n' && separator != '\r' && separator != '\t') {
        fclose(f);
        return -3;
    }
    image_u8_t *image = image_u8_create(width, height);
    if (!image) {
        fclose(f);
        return -4;
    }
    for (int y = 0; y < height; ++y) {
        if (fread(&image->buf[y * image->stride], 1, width, f) != (size_t)width) {
            image_u8_destroy(image);
            fclose(f);
            return -5;
        }
    }
    fclose(f);
    *out = image;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s image.pgm\n", argv[0]);
        return 2;
    }
    image_u8_t *image = NULL;
    int result = read_pgm(argv[1], &image);
    if (result != 0) {
        fprintf(stderr, "PGM_READ_ERROR=%d\n", result);
        return 3;
    }
    apriltag_family_t *family = tag36h11_create();
    apriltag_detector_t *detector = apriltag_detector_create();
    apriltag_detector_add_family_bits(detector, family, 0);
    detector->nthreads = 1;
    detector->quad_decimate = 1.0;
    detector->quad_sigma = 0;
    detector->refine_edges = true;
    detector->debug = false;
    zarray_t *detections = apriltag_detector_detect(detector, image);
    printf("IMAGE %d %d DETECTIONS %d\n", image->width, image->height, zarray_size(detections));
    for (int index = 0; index < zarray_size(detections); ++index) {
        apriltag_detection_t *detection;
        zarray_get(detections, index, &detection);
        printf(
            "DETECTION id=%d hamming=%d margin=%.3f center=%.3f,%.3f "
            "corners=%.3f,%.3f;%.3f,%.3f;%.3f,%.3f;%.3f,%.3f\n",
            detection->id, detection->hamming, detection->decision_margin,
            detection->c[0], detection->c[1],
            detection->p[0][0], detection->p[0][1],
            detection->p[1][0], detection->p[1][1],
            detection->p[2][0], detection->p[2][1],
            detection->p[3][0], detection->p[3][1]);
    }
    apriltag_detections_destroy(detections);
    apriltag_detector_destroy(detector);
    tag36h11_destroy(family);
    image_u8_destroy(image);
    return 0;
}
