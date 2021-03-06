#include "quanttable.h"
#include "hufftree.h"
#include "macros.h"
#include "stream.h"
#include "image.h"
#include "jpeg.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define CHAR_WIDTH 8
#define WORD_SIZE 2
#define READ_WORD(fp) getc(fp) << CHAR_WIDTH | getc(fp)
#define GET_WORD(data, index) (data[index] << CHAR_WIDTH) + data[index + 1]

static size_t ZIGZAG[8][8] = {
    {0, 1, 5, 6, 14, 15, 27, 28},
    {2, 4, 7, 13, 16, 26, 29, 42},
    {3, 8, 12, 17, 25, 30, 41, 43},
    {9, 11, 18, 24, 31, 40, 44, 53},
    {10, 19, 23, 32, 39, 45, 52, 54},
    {20, 22, 33, 38, 46, 51, 55, 60},
    {21, 34, 37, 47, 50, 56, 59, 61},
    {35, 36, 48, 49, 57, 58, 62, 63}
};

static double IDCT_TABLE[8][8] = {
    {0.707107, 0.707107, 0.707107, 0.707107, 0.707107, 0.707107, 0.707107, 0.707107},
    {0.980785, 0.831470, 0.555570, 0.195090, -0.195090, -0.555570, -0.831470, -0.980785},
    {0.923880, 0.382683, -0.382683, -0.923880, -0.923880, -0.382683, 0.382683, 0.923880},
    {0.831470, -0.195090, -0.980785, -0.555570, 0.555570, 0.980785, 0.195090, -0.831470},
    {0.707107, -0.707107, -0.707107, 0.707107, 0.707107, -0.707107, -0.707107, 0.707107},
    {0.555570, -0.980785, 0.195090, 0.831470, -0.831470, -0.195090, 0.980785, -0.555570},
    {0.382683, -0.923880, 0.923880, -0.382683, -0.382683, 0.923880, -0.923880, 0.382683},
    {0.195090, -0.555570, 0.831470, -0.980785, 0.980785, -0.831470, 0.555570, -0.195090}
};

typedef enum SegmentMarker {
    SOI = 0xFFD8,
    COM = 0xFFFE,
    APP0 = 0xFFE0,
    SOF = 0xFFC0,
    DHT = 0xFFC4,
    DQT = 0xFFDB,
    SOS = 0xFFDA,
} SegmentMarker;

typedef enum ComponentId {
    ID_Y = 1,
    ID_CB = 2,
    ID_CR = 3
} ComponentId;

typedef struct component_data {
    uint8_t id;
    uint8_t vSamplingFactor:4;
    uint8_t hSamplingFactor:4;
    uint8_t qTableId;
    uint8_t hTreeId;
} component_data;

typedef struct jpeg_data {
    uint8_t versionMajor;
    uint8_t versionMinor;
    uint8_t pixelDensityUnit;
    uint8_t hPixelDensity;
    uint8_t vPixelDensity;
    uint16_t width;
    uint16_t height;
    uint8_t precision;
    uint8_t componentCount;
    component_data **componentData;
    huff_tree *huffTrees[2][2];
    quant_table *quantTables[2];
} jpeg_data;

static void parse_APP0(jpeg_data *imageData, uint8_t *data, size_t length) {
    char identifier[5];
    strncpy(identifier, (char *)data, 5);

    if (strcmp(identifier, "JFIF") == 0) {
        CHECK_FAIL((imageData->versionMajor = data[5]) != 1, "Invalid major version number: %d", imageData->versionMajor);

        imageData->versionMinor = data[6];
        imageData->pixelDensityUnit = data[7];
        imageData->hPixelDensity = GET_WORD(data, 8);
        imageData->vPixelDensity = GET_WORD(data, 10);
    } else if (strcmp(identifier, "JFXX") == 0) {
        // Thumbnails are ignored
    } else {
        FAIL("Invalid APP0 identifier: %s", identifier);
    }
}

static void parse_SOF(jpeg_data *imageData, uint8_t *data, size_t length) {
    imageData->precision = data[0];
    imageData->height = GET_WORD(data, 1);
    imageData->width = GET_WORD(data, 3);
    imageData->componentCount = data[5];

    imageData->componentData = malloc(imageData->componentCount * sizeof *imageData->componentData);
    CHECK_ALLOC(imageData->componentData, "image component data");

    for (size_t i=0; i < imageData->componentCount; ++i) {
        component_data *currComponentData = malloc(sizeof *currComponentData);
        CHECK_ALLOC(currComponentData, "component data");

        currComponentData->id = data[6 + i*3];
        currComponentData->vSamplingFactor = data[7 + i*3] >> 4;
        currComponentData->hSamplingFactor = data[7 + i*3] & 0xF;
        currComponentData->qTableId = data[8 + i*3] & 0xF;

        imageData->componentData[i] = currComponentData;
    }
}

static void parse_DHT(jpeg_data *imageData, uint8_t *data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        huff_tree *tree = huf_parse_huff_tree(data, &offset);
        imageData->huffTrees[tree->id][tree->class] = tree;
    }
}

static void parse_DQT(jpeg_data *imageData, uint8_t *data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        quant_table *table = qnt_parse_quant_table(data, &offset);
        imageData->quantTables[table->number] = table;
    }
}

static void parse_SOS(jpeg_data *imageData, uint8_t *data, size_t length) {
    uint8_t componentCount = data[0];
    for (size_t i=0; i < componentCount; ++i) {
        imageData->componentData[i]->hTreeId = data[2 + i * 2] >> 4;
    }
}

static int decode_MCU_value(uint8_t size, int16_t bits) {
    if (!(bits >> (size - 1))) {
        bits += 1 - (1 << size);
    }

    return bits;
}

static int decode_dc_diff(huff_tree *tree, stream *str, int prevCoeff) {
    uint8_t size = huf_decode_next_symbol(tree, str);
    int32_t bits = str_get_bits(str, size);

    return decode_MCU_value(size, bits) + prevCoeff;
}

static int parse_coeff_matrix(int coeffMatrix[8][8], jpeg_data *imageData, component_data *componentData, stream *str, int prevDcCoeff) {
    int coeffVector[64] = {0};

    huff_tree **huffTrees = imageData->huffTrees[componentData->hTreeId];
    quant_table *quantTable = imageData->quantTables[componentData->qTableId];

    int dcCoeff = decode_dc_diff(huffTrees[CLASS_DC], str, prevDcCoeff);
    coeffVector[0] = dcCoeff * quantTable->values[0];

    for (size_t i=1; i < 64; ++i) {
        uint8_t value = huf_decode_next_symbol(huffTrees[CLASS_AC], str);

        if (!value)
            break;

        i += value >> 4;  // skip zeroes
        uint8_t size = value & 0xF;
        
        CHECK_FAIL(64 <= i, "Coefficient value index went past 64.");

        coeffVector[i] = decode_MCU_value(size, str_get_bits(str, size)) * quantTable->values[i];
    }

    for (size_t x=0; x < 8; ++x) {
        for (size_t y=0; y < 8; ++y) {
            coeffMatrix[x][y] = coeffVector[ZIGZAG[x][y]];
        }
    }

    return dcCoeff;
}

static float idct(int coeffMatrix[8][8], size_t x, size_t y) {
    float sum = 0;
    for (size_t u=0; u < 8; ++u) {
        for (size_t v=0; v < 8; ++v) { 
            sum += coeffMatrix[v][u] * IDCT_TABLE[u][x] * IDCT_TABLE[v][y];
        }
    }

    return sum / 4;
}

static inline int clamp(int n, int min, int max) {
    return n < min ? min : n > max ? max : n;
}

static int decode_MCU(jpeg_data *imageData, image *im, stream *str, component_data *componentData, int prevDcCoeff, size_t McuX, size_t McuY, size_t hSamplingFactor, size_t vSamplingFactor) {
    int coeffMatrix[8][8];
    int dcCoeff = parse_coeff_matrix(coeffMatrix, imageData, componentData, str, prevDcCoeff);

    for (size_t y=0; y < 8 && McuY + y * vSamplingFactor < imageData->height; ++y) {
        for (size_t x=0; x < 8 && McuX + x * hSamplingFactor < imageData->width; ++x) {
            uint8_t value = clamp(lround(idct(coeffMatrix, x, y)) + 128, 0, 255);

            uint16_t globalX = McuX + x * hSamplingFactor;
            uint16_t globalY = McuY + y * vSamplingFactor;

            for (size_t v=0; v < vSamplingFactor && globalY + v < imageData->height; ++v) {
                for (size_t h=0; h < hSamplingFactor && globalX + h < imageData->width; ++h) {
                    // component id is one above index in a YUV pixel
                    im->pixels[globalX + h][globalY + v][componentData->id - 1] = value;
                }
            }
        }
    }

    return dcCoeff;
}

static void parse_image_data(jpeg_data *imageData, image *im, uint8_t *data, size_t length) {
    // get rid of byte stuffing
    for (size_t i=0; i < length; ++i) {
        if (data[i] == 0xFF) {
            memcpy(data + i + 1, data + i + 2, length-- - i - 1);
        }
    }

    stream *str = str_create_stream(data, length);

    int dcCoeffs[5] = {0};

    // note: this assumes both Cb and Cr have the same sampling factors
    size_t hSamplingFactor = 1;
    size_t vSamplingFactor = 1;
    if (imageData->componentCount == 3) {
        hSamplingFactor = imageData->componentData[0]->hSamplingFactor / imageData->componentData[1]->hSamplingFactor;
        vSamplingFactor = imageData->componentData[0]->vSamplingFactor / imageData->componentData[1]->vSamplingFactor;
    }
    
    for (int y=0; y < imageData->height; y += 8 * vSamplingFactor) {
        for (int x=0; x < imageData->width; x += 8 * hSamplingFactor) {
            for (size_t v=0; v < vSamplingFactor; ++v) {
                for (size_t h=0; h < hSamplingFactor; ++h) {
                    dcCoeffs[0] = decode_MCU(imageData, im, str, imageData->componentData[0], dcCoeffs[0], x + 8*h, y + 8*v, 1, 1);
                }
            }

            if (imageData->componentCount == 3) {
                dcCoeffs[1] = decode_MCU(imageData, im, str, imageData->componentData[1], dcCoeffs[1], x, y, hSamplingFactor, vSamplingFactor);
                dcCoeffs[2] = decode_MCU(imageData, im, str, imageData->componentData[2], dcCoeffs[2], x, y, hSamplingFactor, vSamplingFactor);
            }
        }
    }
}

static void parse_segments(jpeg_data *imageData, FILE *fp) {
    SegmentMarker marker = 0;
    do {
        marker = READ_WORD(fp);

        size_t length = READ_WORD(fp) - WORD_SIZE;
        uint8_t *data = malloc(length * sizeof *data);
        CHECK_ALLOC(data, "data");

        fread(data, sizeof(*data), length, fp);

        switch (marker) {
            case COM: printf("Comment: \"%s\"\n", data); break;
            case APP0: parse_APP0(imageData, data, length); break;
            case SOF: parse_SOF(imageData, data, length); break;
            case DHT: parse_DHT(imageData, data, length); break;
            case DQT: parse_DQT(imageData, data, length); break;
            case SOS: parse_SOS(imageData, data, length); break;
            default: WARN("Could not recognize marker: %04X", marker);
        }

        free(data);
    } while (marker != SOS);
}

static void free_jpeg(jpeg_data *data) {
    FREE_2D_ARRAY(data->componentData, data->componentCount);

    for (size_t i=0; i < 2; ++i) {
        for (size_t j=0; j < 2; ++j) {
            huf_free_huff_tree(data->huffTrees[i][j]);
        }
    }

    for (size_t i=0; i < 2; ++i) {
        qnt_free_quant_table(data->quantTables[i]);
    }

    free(data);
}

image *jpg_fparse(char *path) {
    FILE *fp = fopen(path, "r");
    CHECK_FAIL(fp == NULL, "Could not open file.");

    for (uint16_t word = READ_WORD(fp); word != SOI; word = (word << CHAR_WIDTH) + fgetc(fp)) {}

    jpeg_data *imageData = calloc(1, sizeof *imageData);
    CHECK_ALLOC(imageData, "image data");

    parse_segments(imageData, fp);

    image *im = img_create_image(imageData->width, imageData->height);

    {
        // get remaining file length
        size_t index = ftell(fp);
        fseek(fp, 0, SEEK_END);
        size_t fileSize = ftell(fp);
        fseek(fp, index, SEEK_SET);
        size_t length = fileSize - index - WORD_SIZE;

        uint8_t *data = malloc(length * sizeof *data);
        CHECK_ALLOC(data, "data");

        fread(data, sizeof *data, length, fp);

        parse_image_data(imageData, im, data, length);

        free(data);
    }

    fclose(fp);
    free_jpeg(imageData);

    return im;
}