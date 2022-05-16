#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define CHAR_SIZE 8
#define WORD_SIZE 2 * CHAR_SIZE
#define READ_WORD(fp) getc(fp) << CHAR_SIZE | getc(fp)
#define GET_WORD(data, index) (data[index] << CHAR_SIZE) + data[index + 1]

typedef enum SegmentMarker {
    SOI = 0xFFD8,
    APP0 = 0xFFE0,
    SOF = 0xFFC0,
    DHT = 0xFFC4,
    DQT = 0xFFDB,
    SOS = 0xFFDA,
} SegmentMarker;

typedef struct image {
    uint16_t width;
    uint16_t height;
    uint8_t (**pixels)[3]; // RGB
} image;

typedef struct component_data {
    uint8_t id;
    uint8_t vSamplingFactor:4;
    uint8_t hSamplingFactor:4;
    uint8_t qTableId;
} component_data;

typedef struct jpeg_data {
    uint8_t versionMajor;
    uint8_t versionMinor;
    uint8_t pixelDensityUnit;
    uint8_t hPixelDensity;
    uint8_t vPixelDensity;
    uint8_t thumbWidth;
    uint8_t thumbHeight;
    uint8_t (**thumbPixels)[3]; // RGB
    uint16_t width;
    uint16_t height;
    uint8_t (**pixels)[3]; // YCbCr
    uint8_t precision;
    uint8_t componentCount;
    component_data **componentData;
} jpeg_data;

static int parse_APP0(jpeg_data *imageData, uint8_t *data, size_t length) {
    if ((imageData->versionMajor = data[5]) != 1) {
        puts("Invalid major version number.");
        return -1;
    }

    imageData->versionMinor = data[6];
    imageData->pixelDensityUnit = data[7];
    imageData->hPixelDensity = GET_WORD(data, 8);
    imageData->vPixelDensity = GET_WORD(data, 10);
    imageData->thumbWidth = data[12];
    imageData->thumbHeight = data[13];

    // No thumbnail
    if (!(imageData->thumbWidth && imageData->thumbHeight)) {
        return 0;
    }

    imageData->thumbPixels = malloc(imageData->thumbWidth * sizeof(*imageData->thumbPixels));
    for (size_t x=0; x < imageData->thumbWidth; ++x) {
        imageData->thumbPixels[x] = malloc(imageData->thumbHeight * sizeof(**imageData->thumbPixels));
        for (size_t y=0; y < imageData->thumbHeight; ++y) {
            for (size_t j=0; j < 3; ++j) {
                imageData->thumbPixels[x][y][j] = data[14 + (x*imageData->thumbWidth + y) * 3 + j];
            }
        }
    }

    return -1;
}

static int parse_SOF(jpeg_data *imageData, uint8_t *data, size_t length) {
    imageData->precision = data[0];
    imageData->height = GET_WORD(data, 1);
    imageData->width = GET_WORD(data, 3);
    imageData->componentCount = data[5];

    imageData->componentData = malloc(imageData->componentCount * sizeof *imageData->componentData);
    for (int i=0; i < imageData->componentCount; ++i) {
        component_data *componentData = malloc(sizeof *componentData);

        componentData->id = data[6 + i*3];
        componentData->vSamplingFactor = data[7 + i*3] >> 4;
        componentData->hSamplingFactor = data[7 + i*3] & 0xF;
        componentData->qTableId = data[8 + i*3] & 0xF;

        imageData->componentData[i] = componentData;
    }

    return 0;
}

static int parse_DHT(jpeg_data *imageData, uint8_t *data, size_t length) {
    // not implemented

    return -1;
}

static int parse_DQT(jpeg_data *imageData, uint8_t *data, size_t length) {
    // not implemented

    return -1;
}

static int parse_SOS(jpeg_data *imageData, FILE *fp) {
    // not implemented

    return -1;
}

static int parse_segment(jpeg_data *imageData, FILE *fp) {
    uint16_t marker = READ_WORD(fp);

    // segments that don't indicate their length
    switch (marker) {
        case SOI: return 0;
        case SOS: return parse_SOS(imageData, fp);
    }

    size_t length = READ_WORD(fp) - WORD_SIZE;
    uint8_t *data = malloc(length * sizeof *data);
    fread(data, length, sizeof(uint8_t), fp);

    switch (marker) {
        case APP0: return parse_APP0(imageData, data, length);
        case SOF: return parse_SOF(imageData, data, length);
        case DHT: return parse_DHT(imageData, data, length);
        case DQT: return parse_DQT(imageData, data, length);
    }

    printf("Could not identify marker: %04X", marker);
    return -1;
}

static image *jpeg_to_image(jpeg_data *data) {
    // not implemented

    return NULL;
}

static void free_jpeg(jpeg_data *data) {
    for (int i=0; i < data->componentCount; ++i) {
        free(data->componentData[i]);
    }
    free(data->componentData);

    for (size_t x=0; x < data->width; ++x) {
        free(data->pixels[x]);
    }
    free(data->pixels);

    for (size_t x=0; x < data->thumbWidth; ++x) {
        free(data->thumbPixels[x]);
    }
    free(data->thumbPixels);

    if (!(imageData->thumbWidth && imageData->thumbHeight)) {
        return 0;
    }

    free(data);

}

image *jpg_fparse(char *path) {
    FILE *fp = fopen(path, "r");

    if (fp == NULL) {
        puts("Could not open file.");
        return NULL;
    }

    jpeg_data *imageData = calloc(1, sizeof *imageData);

    int exit_code;
    while ((exit_code = parse_segment(imageData, fp)) == 0);

    fclose(fp);

    if (exit_code != 1) {
        puts("Could not parse jpeg.");
        return NULL;
    }

    image *im = jpeg_to_image(imageData);
    
    free_jpeg(imageData);

    return im;
}

void free_image(image *im) {
    for (size_t i=0; i < im->width; ++i) {
        free(im->pixels[i]);
    }
    free(im->pixels);
    free(im);
}