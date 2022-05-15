#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define CHAR_SIZE 8
#define WORD_SIZE 2 * CHAR_SIZE
#define READ_WORD(fp) getc(fp) << CHAR_SIZE | getc(fp)

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

typedef struct jpeg_data {
    uint16_t width;
    uint16_t height;
    uint8_t (**pixels)[3]; // YCbCr
} jpeg_data;

int parse_APP0(jpeg_data *imageData, uint8_t *data, size_t length) {
    
}

int parse_SOF(jpeg_data *imageData, uint8_t *data, size_t length) {
    
}

int parse_DHT(jpeg_data *imageData, uint8_t *data, size_t length) {
    
}

int parse_DQT(jpeg_data *imageData, uint8_t *data, size_t length) {
    
}

int parse_SOS(jpeg_data *imageData, fp) {

}

int parse_segment(jpeg_data imageData, FILE *fp) {
    uint16_t marker = READ_WORD(fp);

    // markers that don't indicate their length
    switch (marker) {
        case SOI: return 0;
        case SOS: return parse_SOF(imageData, fp);
    }

    size_t length = READ_WORD(fp) - WORD_SIZE;
    uint8_t *data = malloc(length * sizeof *data);
    fread(data, fp, sizeof(uint8_t), length, fp);

    switch (marker) {
        case APP0: return parse_APP0(imageData, data, length);
        case SOF: return parse_SOS(imageData, data, length);
        case DHT: return parse_DHT(imageData, data, length);
        case DQT: return parse_DQT(imageData, data, length);
    }

    printf("Could not identify marker: %04X", marker);
    return -1;
}

image *jpeg_to_image(jpeg *data) {
    
}

image *jpg_fparse(char *path) {
    FILE *fp = fopen(path, "r");

    if (fp == NULL) {
        puts("Could not open file.");
        return NULL;
    }

    while (jpg_parse_segment(fp) == 0);

    fclose(fp);
}