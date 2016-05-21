/*
libskry - astronomical image stacking
Copyright (C) 2016 Filip Szczerek <ga.software@yahoo.com>

This file is part of libskry.

Libskry is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Libskry is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with libskry.  If not, see <http://www.gnu.org/licenses/>.

File description:
    SER-related functions implementation.
*/

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <skry/imgseq.h>
#include <skry/image.h>

#include "imgseq_internal.h"
#include "../utils/logging.h"
#include "../utils/misc.h"
#include "video.h"

/* We need a 64-bit offset-capable file seek function;
   'fseek' is fine only if sizeof(long)==8, e.g. on 64-bit
   Linux, but not on Windows (32- or 64-bit) */

#if defined (_MSC_VER) || defined(__MINGW32__)
  #define FSEEK64 _fseeki64

  // TODO: add more cases (OS X)
#else
  #define FSEEK64 fseek
#endif

enum SER_color_format
{
    SER_MONO       = 0,
    SER_BAYER_RGGB = 8,
    SER_BAYER_GRBG = 9,
    SER_BAYER_GBRG = 10,
    SER_BAYER_BGGR = 11,
    SER_BAYER_CYYM = 16,
    SER_BAYER_YCMY = 17,
    SER_BAYER_YMCY = 18,
    SER_BAYER_MYYC = 19,
    SER_RGB        = 100,
    SER_BGR        = 101
};

const char *SER_color_format_str[] =
{
    [SER_MONO]       = "mono",
    [SER_BAYER_RGGB] = "Bayer RGGB",
    [SER_BAYER_GRBG] = "Bayer GRBG",
    [SER_BAYER_GBRG] = "Bayer GBRG",
    [SER_BAYER_BGGR] = "Bayer BGGR",
    [SER_BAYER_CYYM] = "Bayer CYYM",
    [SER_BAYER_YCMY] = "Bayer YCMY",
    [SER_BAYER_YMCY] = "Bayer YMCY",
    [SER_BAYER_MYYC] = "Bayer MYYC",
    [SER_RGB]        = "RGB",
    [SER_BGR]        = "BGR",
};

// See comment for SER_header::little_endian
#define SER_LITTLE_ENDIAN 0
#define SER_BIG_ENDIAN    1


#pragma pack(push)

#pragma pack(1)
struct SER_header
{
    char     signature[14];
    uint32_t camera_series_id;
    uint32_t color_id;
    // Online documentation claims this is 0 when 16-bit pixel data
    // is big-endian, but the meaning is actually reversed.
    uint32_t little_endian;
    uint32_t img_width;
    uint32_t img_height;
    uint32_t bits_per_channel;
    uint32_t frame_count;
    char     observer[40];
    char     instrument[40];
    char     telescope[40];
    int64_t  date_time;
    int64_t  date_time_UTC;
};

#pragma pack(pop)

struct SER_data
{
    char *file_name;
    FILE *file;
    int little_endian_data; ///< Concerns 16-bit pixel data
    enum SER_color_format SER_color_fmt;
    enum SKRY_pixel_format pix_fmt;
    unsigned width, height;
};

static
void SER_free(struct SKRY_img_sequence *img_seq)
{
    if (img_seq)
    {
        if (img_seq->data)
        {
            struct SER_data *data = (struct SER_data *)img_seq->data;
            if (data)
            {
                free(data->file_name);
                if (data->file)
                {
                    fclose(data->file);
                }
                free(img_seq->data);
            }
        }
        free(img_seq);
    }
}

static
enum SKRY_result SER_get_curr_img_metadata(const struct SKRY_img_sequence *img_seq,
                                           unsigned *width, unsigned *height,
                                           enum SKRY_pixel_format *pix_fmt)
{
    struct SER_data *data = (struct SER_data *)img_seq->data;
    if (width) *width = data->width;
    if (height) *height = data->height;
    if (pix_fmt) *pix_fmt = data->pix_fmt;
    return SKRY_SUCCESS;
}

/// Reverses RGB<->BGR
#define REVERSE_RGB(Type, line, width)   \
    do {                                 \
    for (unsigned x = 0; x < width; x++) \
    {                                    \
        Type ch0 = line[3*x + 0];        \
        line[3*x + 0] = line[3*x + 2];   \
        line[3*x + 2] = ch0;             \
    } } while (0)                        \


#define FRAME_FAIL(error_code) \
    {                          \
        SKRY_free_image(img);  \
        if (result) *result = error_code; \
        return 0;              \
    }

static
struct SKRY_image *SER_get_img_by_index(const struct SKRY_img_sequence *img_seq,
                                        size_t index, enum SKRY_result *result)
{
    assert(index < img_seq->num_images);

    struct SER_data *data = (struct SER_data *)img_seq->data;
    if (!data->file)
        data->file = fopen(data->file_name, "rb");

    if (!data->file)
    {
        LOG_MSG(SKRY_LOG_SER, "Cannot open %s.", data->file_name);
        if (result) *result = SKRY_CANNOT_OPEN_FILE;
    }

    SKRY_Image *img = SKRY_new_image(data->width, data->height, data->pix_fmt,
                                     0, 0);

    if (!img)
    {
        if (result) *result = SKRY_OUT_OF_MEMORY;
        return 0;
    }

    size_t frame_size = data->width * data->height * BYTES_PER_PIXEL[data->pix_fmt];
    if (FSEEK64(data->file, sizeof(struct SER_header) + (int64_t)index * frame_size, SEEK_SET))
    {
        LOG_MSG(SKRY_LOG_AVI, "Cannot seek to frame %zu.", index);
        FRAME_FAIL(SKRY_FILE_IO_ERROR);
    }

    size_t line_byte_count = data->width * BYTES_PER_PIXEL[data->pix_fmt];
    for (unsigned y = 0; y < data->height; y++)
    {
        void *line = SKRY_get_line(img, y);
        if (1 != fread(line, line_byte_count, 1, data->file))
            FRAME_FAIL(SKRY_FILE_IO_ERROR);

        if (SER_BGR == data->SER_color_fmt)
        {
            if (SKRY_PIX_RGB8 == data->pix_fmt)
                REVERSE_RGB(uint8_t, ((uint8_t *)line), data->width);
            else
                REVERSE_RGB(uint16_t, ((uint16_t *)line), data->width);
        }
    }

    if (BITS_PER_CHANNEL[data->pix_fmt] > 8 && is_machine_big_endian() == data->little_endian_data)
        swap_words16(img);

    if (result) *result = SKRY_SUCCESS;

    return img;
}

static
struct SKRY_image *SER_get_current_img(const struct SKRY_img_sequence *img_seq,
                                       enum SKRY_result *result)
{
    return SER_get_img_by_index(img_seq, SKRY_get_curr_img_idx(img_seq), result);
}

static
void SER_deactivate_img_seq(struct SKRY_img_sequence *img_seq)
{
    struct SER_data *data = (struct SER_data *)img_seq->data;
    if (data->file)
    {
        fclose(data->file);
        data->file = 0;
    }
}

#define FAIL_ON_NULL(ptr)                         \
    if (!(ptr))                                   \
    {                                             \
        SKRY_free_img_sequence(img_seq);          \
        if (result) *result = SKRY_OUT_OF_MEMORY; \
        return 0;                                 \
    }

#define FAIL(error_code)                          \
    {                                             \
        SKRY_free_img_sequence(img_seq);          \
        if (result) *result = error_code;         \
        return 0;                                 \
    }

struct SKRY_img_sequence *init_SER(const char *file_name,
                                   SKRY_ImagePool *img_pool,
                                   enum SKRY_result *result)
{
    struct SKRY_img_sequence *img_seq = malloc(sizeof(*img_seq));
    FAIL_ON_NULL(img_seq);

    *img_seq = (struct SKRY_img_sequence) { 0 };

    img_seq->data = malloc(sizeof(struct SER_data));
    FAIL_ON_NULL(img_seq->data);

    img_seq->type = SKRY_IMG_SEQ_SER;
    img_seq->free = SER_free;
    img_seq->get_curr_img = SER_get_current_img;
    img_seq->get_curr_img_metadata = SER_get_curr_img_metadata;
    img_seq->get_img_by_index = SER_get_img_by_index;
    img_seq->deactivate_img_seq = SER_deactivate_img_seq;

    struct SER_data *data = (struct SER_data *)img_seq->data;
    *data = (struct SER_data) { 0 };
    data->file_name = malloc(strlen(file_name) + 1);
    FAIL_ON_NULL(data->file_name);
    strcpy(data->file_name, file_name);
    data->file = fopen(file_name, "rb");
    FAIL_ON_NULL(data->file);

    struct SER_header fheader;
    if (1 != fread(&fheader, sizeof(fheader), 1, data->file))
    {
        LOG_MSG(SKRY_LOG_AVI, "Could not read file header.");
        FAIL(SKRY_SER_MALFORMED_FILE);
    }

    int is_machine_b_e = is_machine_big_endian();

    uint32_t color_id = cnd_swap_32(fheader.color_id, is_machine_b_e);
    if (color_id != SER_MONO &&
        color_id != SER_RGB &&
        color_id != SER_BGR &&
        color_id != SER_BAYER_BGGR &&
        color_id != SER_BAYER_GBRG &&
        color_id != SER_BAYER_GRBG &&
        color_id != SER_BAYER_RGGB)
    {
        LOG_MSG(SKRY_LOG_SER, "Unsupported color format: %s", SER_color_format_str[color_id]);
        FAIL(SKRY_SER_UNSUPPORTED_FORMAT);
    }
    data->SER_color_fmt = color_id;

    uint32_t bits_per_channel = cnd_swap_32(fheader.bits_per_channel, is_machine_b_e);
    if (bits_per_channel > 16)
    {
        LOG_MSG(SKRY_LOG_SER, "Invalid bit depth: %u", bits_per_channel);
        FAIL(SKRY_SER_MALFORMED_FILE);
    }
    switch (color_id)
    {
        case SER_MONO:
            data->pix_fmt = (bits_per_channel <= 8 ?
                                SKRY_PIX_MONO8 :
                                SKRY_PIX_MONO16);
            break;

        case SER_RGB: // intentional fall-through
        case SER_BGR:
            data->pix_fmt = (bits_per_channel <= 8 ?
                                SKRY_PIX_RGB8 :
                                SKRY_PIX_RGB16);
            break;

        case SER_BAYER_BGGR:
            data->pix_fmt = (bits_per_channel <= 8 ?
                                SKRY_PIX_CFA_BGGR8 :
                                SKRY_PIX_CFA_BGGR16);
            break;

        case SER_BAYER_GBRG:
            data->pix_fmt = (bits_per_channel <= 8 ?
                                SKRY_PIX_CFA_GBRG8 :
                                SKRY_PIX_CFA_GBRG16);
            break;

        case SER_BAYER_GRBG:
            data->pix_fmt = (bits_per_channel <= 8 ?
                                SKRY_PIX_CFA_GRBG8 :
                                SKRY_PIX_CFA_GRBG16);
            break;

        case SER_BAYER_RGGB:
            data->pix_fmt = (bits_per_channel <= 8 ?
                                SKRY_PIX_CFA_RGGB8 :
                                SKRY_PIX_CFA_RGGB16);
            break;
    }

    data->little_endian_data = (SER_LITTLE_ENDIAN == cnd_swap_32(fheader.little_endian, is_machine_b_e));
    data->width = cnd_swap_32(fheader.img_width, is_machine_b_e);
    data->height = cnd_swap_32(fheader.img_height, is_machine_b_e);
    img_seq->num_images = cnd_swap_32(fheader.frame_count, is_machine_b_e);

    LOG_MSG(SKRY_LOG_SER, "Video size: %ux%u (%s), %zu frames",
            data->width, data->height,
            SER_color_format_str[color_id],
            img_seq->num_images);

    base_init(img_seq, img_pool);

    fclose(data->file);
    data->file = 0;

    if (result) *result = SKRY_SUCCESS;

    return img_seq;
}
