#include <assert.h>
#include <stdint.h>
#include <skry/skry.h>

#include "demosaic.h"

static
const unsigned CFA_GREEN_OFS[] =
{
    [SKRY_CFA_RGGB] = 1,
    [SKRY_CFA_BGGR] = 1,
    [SKRY_CFA_GRBG] = 0,
    [SKRY_CFA_GBRG] = 0

};

static
const unsigned CFA_RED_ROW_OFS[] =
{
    [SKRY_CFA_RGGB] = 0,
    [SKRY_CFA_BGGR] = 1,
    [SKRY_CFA_GRBG] = 0,
    [SKRY_CFA_GBRG] = 1

};

static
const unsigned CFA_RED_COL_OFS[] =
{
    [SKRY_CFA_RGGB] = 0,
    [SKRY_CFA_BGGR] = 1,
    [SKRY_CFA_GRBG] = 1,
    [SKRY_CFA_GBRG] = 0
};


/* Demosaicing is split into macros, because we want to be able to:
     - handle 8- and 16-bit data
     - save output as RGB (8- or 16-bit) or convert straight to 8-bit mono
   with minimal code duplication. */

#define RED   0
#define GREEN 1
#define BLUE  2


/// Demosaics a 2x2 pixel block
#define DEMOSAIC_BLOCK_SIMPLE()                            \
{                                                          \
    RGB_at_R[RED]   = src_blk_R[dxR];                      \
    RGB_at_R[GREEN] = (src_blk_R[dxR-1]                    \
                     + src_blk_R[dxR+1]                    \
                     + src_blk_Rm1[dxR]                    \
                     + src_blk_Rp1[dxR]) >> 2;             \
    RGB_at_R[BLUE]  = (src_blk_Rm1[dxR-1]                  \
                     + src_blk_Rp1[dxR+1]                  \
                     + src_blk_Rp1[dxR-1]                  \
                     + src_blk_Rm1[dxR+1]) >> 2;           \
                                                           \
    RGB_at_B[RED] = (src_blk_Bm1[dxB-1]                    \
                   + src_blk_Bp1[dxB+1]                    \
                   + src_blk_Bp1[dxB-1]                    \
                   + src_blk_Bm1[dxB+1]) >> 2;             \
    RGB_at_B[GREEN] = (src_blk_B[dxB-1]                    \
                     + src_blk_B[dxB+1]                    \
                     + src_blk_Bm1[dxB]                    \
                     + src_blk_Bp1[dxB]) >> 2;             \
    RGB_at_B[BLUE] = src_blk_B[dxB];                       \
                                                           \
    RGB_at_G_at_R_row[RED] = (src_blk_R[(dxR^1)-1]         \
                            + src_blk_R[(dxR^1)+1]) >> 1;  \
    RGB_at_G_at_R_row[GREEN] = src_blk_R[dxR^1];           \
    RGB_at_G_at_R_row[BLUE] = (src_blk_Rm1[dxR^1]          \
                             + src_blk_Rp1[dxR^1]) >> 1;   \
                                                           \
    RGB_at_G_at_B_row[RED] = (src_blk_Bm1[dxB^1]           \
                            + src_blk_Bp1[dxB^1]) >> 1;    \
    RGB_at_G_at_B_row[GREEN] = src_blk_B[dxB^1];           \
    RGB_at_G_at_B_row[BLUE] = (src_blk_B[(dxB^1)-1]        \
                             + src_blk_B[(dxB^1)+1]) >> 1; \
}

/*
  Based on:

  HIGH-QUALITY LINEAR INTERPOLATION FOR DEMOSAICING OF BAYER-PATTERNED COLOR IMAGES
  Henrique S. Malvar, Li-wei He, Ross Cutler

 */
#define DEMOSAIC_BLOCK_HQLINEAR()                          \
{                                                          \
  /* output red at red pixel in raw data */                \
    RGB_at_R[RED]   = src_blk_R[dxR];                      \
                                                           \
  /*
    output green at red pixel in raw data

          +--+
          |-1|
          +--+
          | 2|
    +--+--+--+--+--+
    |-1| 2| 4| 2|-1|    multiply everything by 1/8
    +--+--+--+--+--+
          | 2|
          +--+
          |-1|
          +--+
  */                                                       \
                                                           \
    RGB_at_R[GREEN] = (4*src_blk_R[dxR]                    \
                    + 2*(src_blk_R[dxR-1]                  \
                       + src_blk_R[dxR+1]                  \
                       + src_blk_Rm1[dxR]                  \
                       + src_blk_Rp1[dxR])                 \
                    - src_blk_R[dxR-2]                     \
                    - src_blk_R[dxR+2]                     \
                    - src_blk_Rm2[dxR]                     \
                    - src_blk_Rp2[dxR]) >> 3;              \
  /*
    output blue at red pixel in raw data

          +--+
          |-3|
       +--+--+--+
       | 4|  | 4|
    +--+--+--+--+--+
    |-3|  |12|  |-3|    multiply everything by 1/16
    +--+--+--+--+--+
       | 4|  | 4|
       +--+--+--+
          |-3|
          +--+
  */                                                       \
                                                           \
    RGB_at_R[BLUE]  = (12*src_blk_R[dxR]                   \
                     + 4*(src_blk_Rp1[dxR+1]               \
                        + src_blk_Rp1[dxR-1]               \
                        + src_blk_Rm1[dxR-1]               \
                        + src_blk_Rm1[dxR+1])              \
                     - 3*(src_blk_Rm2[dxR]                 \
                        + src_blk_Rp2[dxR]                 \
                        + src_blk_R[dxR-2]                 \
                        + src_blk_R[dxR+2])) >> 4;         \
                                                           \
  /*
    output red at blue pixel in raw data,
    (same coefficients as for blue at red)
  */                                                       \
                                                           \
    RGB_at_B[RED]  = (12*src_blk_B[dxB]                    \
                     + 4*(src_blk_Bp1[dxB+1]               \
                        + src_blk_Bp1[dxB-1]               \
                        + src_blk_Bm1[dxB-1]               \
                        + src_blk_Bm1[dxB+1])              \
                     - 3*(src_blk_Bm2[dxB]                 \
                        + src_blk_Bp2[dxB]                 \
                        + src_blk_B[dxB-2]                 \
                        + src_blk_B[dxB+2])) >> 4;         \
                                                           \
  /*
    output green at blue pixel in raw data
    (same coefficients as for green at red)
  */                                                       \
                                                           \
    RGB_at_B[GREEN] = (4*src_blk_B[dxB]                    \
                    + 2*(src_blk_B[dxB-1]                  \
                       + src_blk_B[dxB+1]                  \
                       + src_blk_Bm1[dxB]                  \
                       + src_blk_Bp1[dxB])                 \
                    - src_blk_B[dxB-2]                     \
                    - src_blk_B[dxB+2]                     \
                    - src_blk_Bm2[dxB]                     \
                    - src_blk_Bp2[dxB]) >> 3;              \
                                                           \
  /* output blue at blue pixel in raw data */              \
    RGB_at_B[BLUE] = src_blk_B[dxB];                       \
                                                           \
  /*
    output red at green pixel in red row in raw data

          +--+
          | 1|
       +--+--+--+
       |-2|  |-2|
    +--+--+--+--+--+
    |-2| 8|10| 8|-2|    multiply everything by 1/16
    +--+--+--+--+--+
       |-2|  |-2|
       +--+--+--+
          | 1|
          +--+
  */                                                       \
    RGB_at_G_at_R_row[RED] = (10*src_blk_R[dxB]            \
                           + 8*(src_blk_R[dxB-1]           \
                              + src_blk_R[dxB+1])          \
                           - 2*(src_blk_Rm1[dxB-1]         \
                              + src_blk_Rm1[dxB+1]         \
                              + src_blk_Rp1[dxB-1]         \
                              + src_blk_Rp1[dxB+1]         \
                              + src_blk_R[dxB-2]           \
                              + src_blk_R[dxB+2])          \
                           + src_blk_Rm2[dxB]              \
                           + src_blk_Rp2[dxB]) >> 4;       \
                                                           \
  /* output green at green pixel in red row in raw data */ \
    RGB_at_G_at_R_row[GREEN] = src_blk_R[dxB];             \
                                                           \
  /*
    output blue at green pixel in red row in raw data
          +--+
          |-2|
       +--+--+--+
       |-2| 8|-2|
    +--+--+--+--+--+
    | 1|  |10|  | 1|    multiply everything by 1/16
    +--+--+--+--+--+
       |-2| 8|-2|
       +--+--+--+
          |-2|
          +--+
  */                                                       \
                                                           \
    RGB_at_G_at_R_row[BLUE] = (10*src_blk_R[dxB]           \
                              + 8*(src_blk_Rm1[dxB]        \
                                 + src_blk_Rp1[dxB])       \
                              - 2*(src_blk_Rm1[dxB-1]      \
                                 + src_blk_Rm1[dxB+1]      \
                                 + src_blk_Rp1[dxB-1]      \
                                 + src_blk_Rp1[dxB+1]      \
                                 + src_blk_Rm2[dxB]        \
                                 + src_blk_Rp2[dxB])       \
                               + src_blk_R[dxB-2]          \
                               + src_blk_R[dxB+2]) >> 4;   \
                                                           \
  /*
    output red at green pixel in blue row in raw data
    (same coefficients as for blue at green pixel in red row)
  */                                                       \
    RGB_at_G_at_B_row[RED] = (10*src_blk_B[dxR]            \
                             + 8*(src_blk_Bm1[dxR]         \
                                + src_blk_Bp1[dxR])        \
                             - 2*(src_blk_Bm1[dxR-1]       \
                                + src_blk_Bm1[dxR+1]       \
                                + src_blk_Bp1[dxR-1]       \
                                + src_blk_Bp1[dxR+1]       \
                                + src_blk_Bm2[dxR]         \
                                + src_blk_Bp2[dxR])        \
                              + src_blk_B[dxR-2]           \
                              + src_blk_B[dxR+2]) >> 4;    \
                                                           \
  /* output green at green pixel in raw data */            \
    RGB_at_G_at_B_row[GREEN] = src_blk_B[dxR];             \
  /*
    output blue at green pixel in blue row in raw data
    (same coefficients as for red at green pixel in red row)
  */                                                       \
    RGB_at_G_at_B_row[BLUE] = (10*src_blk_B[dxR]           \
                           + 8*(src_blk_B[dxR-1]           \
                              + src_blk_B[dxR+1])          \
                           - 2*(src_blk_Bm1[dxR-1]         \
                              + src_blk_Bm1[dxR+1]         \
                              + src_blk_Bp1[dxR-1]         \
                              + src_blk_Bp1[dxR+1]         \
                              + src_blk_B[dxR-2]           \
                              + src_blk_B[dxR+2])          \
                           + src_blk_Bm2[dxR]              \
                           + src_blk_Bp2[dxR]) >> 4;       \
}


#define WRITE_OUTPUT_RGB()                                \
{                                                         \
    dest_blk_R[dxR*3 + RED]   = RGB_at_R[RED];            \
    dest_blk_R[dxR*3 + GREEN] = RGB_at_R[GREEN];          \
    dest_blk_R[dxR*3 + BLUE]  = RGB_at_R[BLUE];           \
                                                          \
    dest_blk_B[dxB*3 + RED]   = RGB_at_B[RED];            \
    dest_blk_B[dxB*3 + GREEN] = RGB_at_B[GREEN];          \
    dest_blk_B[dxB*3 + BLUE]  = RGB_at_B[BLUE];           \
                                                          \
    dest_blk_R[dxB*3 + RED]   = RGB_at_G_at_R_row[RED];   \
    dest_blk_R[dxB*3 + GREEN] = RGB_at_G_at_R_row[GREEN]; \
    dest_blk_R[dxB*3 + BLUE]  = RGB_at_G_at_R_row[BLUE];  \
                                                          \
    dest_blk_B[dxR*3 + RED]   = RGB_at_G_at_B_row[RED];   \
    dest_blk_B[dxR*3 + GREEN] = RGB_at_G_at_B_row[GREEN]; \
    dest_blk_B[dxR*3 + BLUE]  = RGB_at_G_at_B_row[BLUE];  \
}


#define WRITE_OUTPUT_MONO8_from_RGB8()               \
{                                                    \
    dest_blk_R[dxR] = (RGB_at_R[RED]                 \
                     + RGB_at_R[GREEN]               \
                     + RGB_at_R[BLUE]) / 3;          \
                                                     \
    dest_blk_B[dxB] = (RGB_at_B[RED]                 \
                     + RGB_at_B[GREEN]               \
                     + RGB_at_B[BLUE]) / 3;          \
                                                     \
    dest_blk_R[dxB] = (RGB_at_G_at_R_row[RED] +      \
                     + RGB_at_G_at_R_row[GREEN]      \
                     + RGB_at_G_at_R_row[BLUE]) / 3; \
                                                     \
    dest_blk_B[dxR] = (RGB_at_G_at_B_row[RED]        \
                     + RGB_at_G_at_B_row[GREEN]      \
                     + RGB_at_G_at_B_row[BLUE]) / 3; \
}


#define WRITE_OUTPUT_MONO8_from_RGB16()                   \
{                                                         \
    dest_blk_R[dxR] = (RGB_at_R[RED]                      \
                     + RGB_at_R[GREEN]                    \
                     + RGB_at_R[BLUE]) / 3 >> 8;          \
                                                          \
    dest_blk_B[dxB] = (RGB_at_B[RED]                      \
                     + RGB_at_B[GREEN]                    \
                     + RGB_at_B[BLUE]) / 3 >> 8;          \
                                                          \
    dest_blk_R[dxB] = (RGB_at_G_at_R_row[RED] +           \
                     + RGB_at_G_at_R_row[GREEN]           \
                     + RGB_at_G_at_R_row[BLUE]) / 3 >> 8; \
                                                          \
    dest_blk_B[dxR] = (RGB_at_G_at_B_row[RED]             \
                     + RGB_at_G_at_B_row[GREEN]           \
                     + RGB_at_G_at_B_row[BLUE]) / 3 >> 8; \
}

static
void clamp(int RGB[3], int max_value)
{
    for (int ch = 0; ch < 3; ch++)
    {
        if (RGB[ch] < 0)
            RGB[ch] = 0;
        else if (RGB[ch] > max_value)
            RGB[ch] = max_value;
    }
}

#define OUTPUT_AT(OutputT, nch, x, y) \
    ((OutputT *)((uint8_t *)output + (y)*output_stride))[(x)*nch + ch]


/*
    Type:            input/output data type (e.g. uint8_t)
    algorithm:       SIMPLE or HQLINEAR
    output_kind:     RGB, MONO8_from_RGB8, MONO8_from_RGB16
    n_out_ch:        number of output channels
    max_val:         max value of output (e.g. 0xFF)
*/
#define DEMOSAIC(InputT, OutputT, algorithm, output_kind, n_out_ch, max_val)            \
do {                                                                                    \
    if (width < 6 || height < 6)                                                        \
        return;                                                                         \
                                                                                        \
    /* Offset of the red input pixel in each 2x2 block */                               \
    int dxR = CFA_RED_COL_OFS[CFA_pattern],                                             \
        dyR = CFA_RED_ROW_OFS[CFA_pattern];                                             \
                                                                                        \
    /* Offset of the blue input pixel in each 2x2 block */                              \
    int dxB = dxR^1,                                                                    \
        dyB = dyR^1;                                                                    \
                                                                                        \
    int RGB_at_R[3], /* RGB values at red input pixel location */                       \
        RGB_at_B[3], /* RGB values at blue input pixel location */                      \
        RGB_at_G_at_R_row[3], /* RGB values at green input pixel at red row */          \
        RGB_at_G_at_B_row[3]; /* RGB values at green input pixel at blue row */         \
                                                                                        \
    /* skip 2 initial rows */                                                           \
    InputT *src_row  = (InputT *)((uint8_t *)input + 2*input_stride);                   \
    OutputT *dest_row = (OutputT *)((uint8_t *)output + 2*output_stride);               \
                                                                                        \
    /* Process pixels in 2x2 blocks; each block has to be
       at least 2 pixels from image border */                                           \
    for (unsigned y = 2; y <= height-4; y += 2)                                         \
    {                                                                                   \
        /* skip 2 initial columns */                                                    \
        InputT *src_blk = src_row + 2*1;                                                \
        OutputT *dest_blk = dest_row + 2*n_out_ch;                                      \
                                                                                        \
        for (unsigned x = 2; x <= width-4; x += 2)                                      \
        {                                                                               \
            /* Pointers to the current block's red row and its neighbors */             \
            InputT *src_blk_R   = (InputT *)((uint8_t *)src_blk + dyR*input_stride);    \
            InputT *src_blk_Rm1 = (InputT *)((uint8_t *)src_blk_R   - input_stride);    \
            InputT *src_blk_Rm2 = (InputT *)((uint8_t *)src_blk_Rm1 - input_stride);    \
            InputT *src_blk_Rp1 = (InputT *)((uint8_t *)src_blk_R   + input_stride);    \
            InputT *src_blk_Rp2 = (InputT *)((uint8_t *)src_blk_Rp1 + input_stride);    \
                                                                                        \
            /* Pointers to the current block's blue row and its neighbors */            \
            InputT *src_blk_B   = (InputT *)((uint8_t *)src_blk + dyB*input_stride);    \
            InputT *src_blk_Bm1 = (InputT *)((uint8_t *)src_blk_B   - input_stride);    \
            InputT *src_blk_Bm2 = (InputT *)((uint8_t *)src_blk_Bm1 - input_stride);    \
            InputT *src_blk_Bp1 = (InputT *)((uint8_t *)src_blk_B   + input_stride);    \
            InputT *src_blk_Bp2 = (InputT *)((uint8_t *)src_blk_Bp1 + input_stride);    \
                                                                                        \
            OutputT *dest_blk_R = (OutputT *)((uint8_t *)dest_blk + dyR*output_stride); \
            OutputT *dest_blk_B = (OutputT *)((uint8_t *)dest_blk + dyB*output_stride); \
                                                                                        \
            DEMOSAIC_BLOCK_##algorithm();                                               \
                                                                                        \
            clamp(RGB_at_R, max_val);                                                   \
            clamp(RGB_at_B, max_val);                                                   \
            clamp(RGB_at_G_at_R_row, max_val);                                          \
            clamp(RGB_at_G_at_B_row, max_val);                                          \
                                                                                        \
            WRITE_OUTPUT_##output_kind();                                               \
                                                                                        \
            src_blk += 2*1;                                                             \
            dest_blk += 2*n_out_ch;                                                     \
        }                                                                               \
                                                                                        \
        src_row = (InputT *)((uint8_t *)src_row + 2*input_stride);                      \
        dest_row = (OutputT *)((uint8_t *)dest_row + 2*output_stride);                  \
    }                                                                                   \
                                                                                        \
    /* Fill the borders */                                                              \
    for (size_t ch = 0; ch < n_out_ch; ch++)                                            \
    {                                                                                   \
        /* upper left 2x2 block */                                                      \
        for (unsigned x = 0; x <= 1; x++)                                               \
            for (unsigned y = 0; y <= 1; y++)                                           \
                OUTPUT_AT(OutputT, n_out_ch, x, y) =                                    \
                    OUTPUT_AT(OutputT, n_out_ch, 2, 2);                                 \
                                                                                        \
        /* lower left 2x3 block*/                                                       \
        for (unsigned x = 0; x <= 1; x++)                                               \
            for (unsigned y = height-3; y <= height-1; y++)                             \
                OUTPUT_AT(OutputT, n_out_ch, x, y) =                                    \
                    OUTPUT_AT(OutputT, n_out_ch, 2, height-4);                          \
                                                                                        \
        /* lower right 3x3 block */                                                     \
        for (unsigned x = width-3; x <= width-1; x++)                                   \
            for (unsigned y = height-3; y <= height-1; y++)                             \
                OUTPUT_AT(OutputT, n_out_ch, x, y) =                                    \
                    OUTPUT_AT(OutputT, n_out_ch, width-4, height-4);                    \
                                                                                        \
        /* upper right 3x2 block */                                                     \
        for (unsigned x = width-3; x <= width-1; x++)                                   \
            for (unsigned y = 0; y <= 1; y++)                                           \
                OUTPUT_AT(OutputT, n_out_ch, x, y) =                                    \
                    OUTPUT_AT(OutputT, n_out_ch, width-4, 2);                           \
                                                                                        \
        for (unsigned x = 2; x <= width-4; x++)                                         \
        {                                                                               \
            /* top */                                                                   \
            OUTPUT_AT(OutputT, n_out_ch, x, 0) =                                        \
            OUTPUT_AT(OutputT, n_out_ch, x, 1) =                                        \
                OUTPUT_AT(OutputT, n_out_ch, x, 2);                                     \
                                                                                        \
            /* bottom */                                                                \
            OUTPUT_AT(OutputT, n_out_ch, x, height-1) =                                 \
            OUTPUT_AT(OutputT, n_out_ch, x, height-2) =                                 \
            OUTPUT_AT(OutputT, n_out_ch, x, height-3) =                                 \
                OUTPUT_AT(OutputT, n_out_ch, x, height-4);                              \
        }                                                                               \
                                                                                        \
        for (unsigned y = 2; y <= height-4; y++)                                        \
        {                                                                               \
            /* left */                                                                  \
            OUTPUT_AT(OutputT, n_out_ch, 0, y) =                                        \
            OUTPUT_AT(OutputT, n_out_ch, 1, y) =                                        \
                OUTPUT_AT(OutputT, n_out_ch, 2, y);                                     \
                                                                                        \
            /* right */                                                                 \
            OUTPUT_AT(OutputT, n_out_ch, width-1, y) =                                  \
            OUTPUT_AT(OutputT, n_out_ch, width-2, y) =                                  \
            OUTPUT_AT(OutputT, n_out_ch, width-3, y) =                                  \
                OUTPUT_AT(OutputT, n_out_ch, width-4, y);                               \
        }                                                                               \
    }                                                                                   \
} while (0)


void demosaic_8_as_RGB(uint8_t *input, unsigned width, unsigned height, ptrdiff_t input_stride,
                       uint8_t *output, ptrdiff_t output_stride,
                       enum SKRY_CFA_pattern CFA_pattern, enum SKRY_demosaic_method method)
{
    if (method == SKRY_DEMOSAIC_SIMPLE)
        DEMOSAIC(uint8_t, uint8_t, SIMPLE, RGB, 3, 0xFF);
    else
        DEMOSAIC(uint8_t, uint8_t, HQLINEAR, RGB, 3, 0xFF);
}

void demosaic_8_as_mono8(uint8_t *input, unsigned width, unsigned height, ptrdiff_t input_stride,
                         uint8_t *output, ptrdiff_t output_stride,
                         enum SKRY_CFA_pattern CFA_pattern, enum SKRY_demosaic_method method)
{
    if (method == SKRY_DEMOSAIC_SIMPLE)
        DEMOSAIC(uint8_t, uint8_t, SIMPLE, MONO8_from_RGB8, 1, 0xFF);
    else
        DEMOSAIC(uint8_t, uint8_t, HQLINEAR, MONO8_from_RGB8, 1, 0xFF);
}

void demosaic_16_as_RGB(uint16_t *input, unsigned width, unsigned height, ptrdiff_t input_stride,
                        uint16_t *output, ptrdiff_t output_stride,
                        enum SKRY_CFA_pattern CFA_pattern, enum SKRY_demosaic_method method)
{
    if (method == SKRY_DEMOSAIC_SIMPLE)
        DEMOSAIC(uint16_t, uint16_t, SIMPLE, RGB, 3, 0xFFFF);
    else
        DEMOSAIC(uint16_t, uint16_t, HQLINEAR, RGB, 3, 0xFFFF);
}

void demosaic_16_as_mono8(uint16_t *input, unsigned width, unsigned height, ptrdiff_t input_stride,
                          uint8_t *output, ptrdiff_t output_stride,
                          enum SKRY_CFA_pattern CFA_pattern, enum SKRY_demosaic_method method)
{
    if (method == SKRY_DEMOSAIC_SIMPLE)
        DEMOSAIC(uint16_t, uint8_t, SIMPLE, MONO8_from_RGB16, 1, 0xFFFF);
    else
        DEMOSAIC(uint16_t, uint8_t, HQLINEAR, MONO8_from_RGB16, 1, 0xFFFF);
}

enum SKRY_CFA_pattern translate_CFA_pattern(enum SKRY_CFA_pattern pattern,
                                            unsigned dx, unsigned dy)
{
    static
    const enum SKRY_CFA_pattern pattern_translation_LUT[SKRY_CFA_MAX][2][2] =
    {
        [SKRY_CFA_BGGR][0][0] = SKRY_CFA_BGGR,
        [SKRY_CFA_BGGR][1][0] = SKRY_CFA_GBRG,
        [SKRY_CFA_BGGR][0][1] = SKRY_CFA_GRBG,
        [SKRY_CFA_BGGR][1][1] = SKRY_CFA_RGGB,

        [SKRY_CFA_GBRG][0][0] = SKRY_CFA_GBRG,
        [SKRY_CFA_GBRG][1][0] = SKRY_CFA_BGGR,
        [SKRY_CFA_GBRG][0][1] = SKRY_CFA_RGGB,
        [SKRY_CFA_GBRG][1][1] = SKRY_CFA_GRBG,

        [SKRY_CFA_GRBG][0][0] = SKRY_CFA_GRBG,
        [SKRY_CFA_GRBG][1][0] = SKRY_CFA_RGGB,
        [SKRY_CFA_GRBG][0][1] = SKRY_CFA_BGGR,
        [SKRY_CFA_GRBG][1][1] = SKRY_CFA_GBRG,

        [SKRY_CFA_RGGB][0][0] = SKRY_CFA_RGGB,
        [SKRY_CFA_RGGB][1][0] = SKRY_CFA_GRBG,
        [SKRY_CFA_RGGB][0][1] = SKRY_CFA_GBRG,
        [SKRY_CFA_RGGB][1][1] = SKRY_CFA_BGGR
    };

    assert(pattern >= 0 && pattern < SKRY_CFA_MAX);

    return pattern_translation_LUT[pattern][dx][dy];
}
