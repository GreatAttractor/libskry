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
    AVI-related functions implementation.
*/

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <skry/imgseq.h>
#include <skry/image.h>

#include "../image/bmp.h"
#include "imgseq_internal.h"
#include "../utils/logging.h"
#include "../utils/misc.h"
#include "video.h"


// Returns the highest multiple of 4 which is <= x
#define DOWN4MULT(x) ((x)/4*4)

// Returns the lowest multiple of 4 which is >= x
#define UP4MULT(x) ((x+3)/4*4)

#define CHARS_TO_UINT32(str) ((uint32_t)(str)[0] + ((uint32_t)(str)[1]<<8) + ((uint32_t)(str)[2]<<16) + ((uint32_t)(str)[3]<<24))

#define AVIF_HAS_INDEX 0x00000010U

#define FCC_COMPARE(fcc, string) ((fcc)[0] == string[0] \
                               && (fcc)[1] == string[1] \
                               && (fcc)[2] == string[2] \
                               && (fcc)[3] == string[3])

/// Four Character Code (FCC)
typedef uint8_t fourcc_t[4];

#pragma pack(push)

#pragma pack(1)
struct AVI_file_header
{
    fourcc_t riff;
    uint32_t file_size;
    fourcc_t avi;
};

#pragma pack(1)
struct AVI_stream_header
{
    fourcc_t fcc_type;
    fourcc_t fcc_handler;
    uint32_t flags;
    uint16_t priority;
    uint16_t language;
    uint32_t initial_frames;
    uint32_t scale;
    uint32_t rate;
    uint32_t start;
    uint32_t length;
    uint32_t suggested_buffer_size;
    uint32_t quality;
    uint32_t sample_size;
    struct
    {
        int16_t left;
        int16_t top;
        int16_t right;
        int16_t bottom;
    } frame;
};

/*
#pragma pack(1)
typedef struct {
    uint32_t fcc;
    uint32_t cb;
    uint32_t dwGrandFrames;
    uint32_t dwFuture[61];
} ODMLExtendedAVIHeader_t;*/

#pragma pack(1)
struct AVI_list
{
    fourcc_t list; // contains "LIST"
    uint32_t list_size; // does not include 'list' and 'list_type'
    fourcc_t list_type;
};

#pragma pack(1)
struct AVI_chunk
{
    fourcc_t ck_id;
    uint32_t ck_size; // does not include 'ck_id' and 'ck_size'
};

/// List or chunk (used when skipping JUNK chunks)
#pragma pack(1)
struct AVI_fragment
{
    fourcc_t fcc;
    uint32_t size;
};

#pragma pack(1)
struct AVI_main_header
{
    uint32_t microsec_per_frame;
    uint32_t max_bytes_per_sec;
    uint32_t padding_granularity;
    uint32_t flags;
    uint32_t total_frames;
    uint32_t initial_frames;
    uint32_t streams;
    uint32_t suggested_buffer_size;
    uint32_t width;
    uint32_t height;
    uint32_t reserved[4];
};

#pragma pack(1)
struct AVI_old_index
{
    fourcc_t chunk_id;
    uint32_t flags;
    // Offset of frame contents counted from the beginning
    // of 'movi' list's 'list_type' field
    uint32_t offset;
    uint32_t frame_size;
};

#pragma pack(pop)

enum AVI_pixel_format
{
    AVI_PIX_DIB_MIN,

        AVI_PIX_DIB_RGB8,  ///< DIB, RGB 8-bit
        AVI_PIX_DIB_PAL8,  ///< DIB, 256-color 8-bit RGB palette
        AVI_PIX_DIB_MONO8, ///< DIB, 256-color grayscale palette

    AVI_PIX_DIB_MAX,

    AVI_PIX_Y800, /// 8 bits per pixel, luminance only
};

const char *AVI_pixel_format_str[] =
{
    [AVI_PIX_DIB_RGB8]  = "DIB/RGB 8-bit",
    [AVI_PIX_DIB_PAL8]  = "DIB/8-bit palette",
    [AVI_PIX_DIB_MONO8] = "DIB/8-bit grayscale",
    [AVI_PIX_Y800]      = "Y800 (8-bit grayscale)"
};

#define IS_DIB(pix_fmt)  ((pix_fmt) > AVI_PIX_DIB_MIN && (pix_fmt) < AVI_PIX_DIB_MAX)

enum SKRY_pixel_format AVI_to_SKRY_pix_fmt[] =
{
    [AVI_PIX_DIB_MONO8] = SKRY_PIX_MONO8,
    [AVI_PIX_DIB_RGB8] = SKRY_PIX_RGB8,
    [AVI_PIX_DIB_PAL8] = SKRY_PIX_PAL8,
    [AVI_PIX_Y800]     = SKRY_PIX_MONO8
};

struct AVI_data
{
    char *file_name;
    FILE *file;
    uint32_t *frame_offsets; ///< Absolute file offsets (point to each frame's 'AVI_chunk')
    struct SKRY_palette palette; ///< Valid for an AVI with palette
    enum AVI_pixel_format pix_fmt;
    unsigned width, height;
};

#define AVI_DATA(data) ((struct AVI_data *)data)

static
enum SKRY_result AVI_get_curr_img_metadata(const struct SKRY_img_sequence *img_seq,
                                           unsigned *width, unsigned *height,
                                           enum SKRY_pixel_format *pix_fmt)
{
    if (width) *width = AVI_DATA(img_seq->data)->width;
    if (height) *height = AVI_DATA(img_seq->data)->height;
    if (pix_fmt) *pix_fmt = AVI_DATA(img_seq->data)->pix_fmt;
    return SKRY_SUCCESS;
}

#define FRAME_FAIL(error_code) \
    {                          \
        SKRY_free_image(img);  \
        if (result) *result = error_code; \
        return 0;              \
    }

static
struct SKRY_image *AVI_get_img_by_index(const struct SKRY_img_sequence *img_seq,
                                        size_t index, enum SKRY_result *result)
{
    assert(index < img_seq->num_images);
    struct AVI_data *data = (struct AVI_data *)img_seq->data;

    if (!data->file)
        data->file = fopen(data->file_name, "rb");

    if (!data->file)
    {
        LOG_MSG(SKRY_LOG_AVI, "Cannot open %s.", data->file_name);
        if (result) *result = SKRY_CANNOT_OPEN_FILE;
    }

    SKRY_Image *img = SKRY_new_image(data->width, data->height, AVI_to_SKRY_pix_fmt[data->pix_fmt],
                                     &data->palette, 0);

    if (!img)
    {
        if (result) *result = SKRY_OUT_OF_MEMORY;
        return 0;
    }

    if (fseek(data->file, data->frame_offsets[index], SEEK_SET))
    {
        LOG_MSG(SKRY_LOG_AVI, "Cannot seek to frame %zu.", index);
        FRAME_FAIL(SKRY_FILE_IO_ERROR);
    }

    struct AVI_chunk chunk;
    if (1 != fread(&chunk, sizeof(chunk), 1, data->file))
    {
        LOG_MSG(SKRY_LOG_AVI, "Could not read frame chunk %zu.", index);
        FRAME_FAIL(SKRY_FILE_IO_ERROR);
    }

    size_t line_byte_count = data->width * BYTES_PER_PIXEL[AVI_to_SKRY_pix_fmt[data->pix_fmt]];
    if (IS_DIB(data->pix_fmt))
        line_byte_count = UP4MULT(line_byte_count);

    if (!FCC_COMPARE(chunk.ck_id, "00db") && !FCC_COMPARE(chunk.ck_id, "00dc")
        || chunk.ck_size != line_byte_count * data->height)
    {
        LOG_MSG(SKRY_LOG_AVI, "Invalid frame %zu.", index);
        FRAME_FAIL(SKRY_AVI_MALFORMED_FILE);
    }

    uint8_t *line = malloc(line_byte_count);
    for (unsigned y = 0; y < data->height; y++)
    {
        fread(line, line_byte_count, 1, data->file);
        uint8_t *img_line =
            SKRY_get_line(img, IS_DIB(data->pix_fmt) ? SKRY_get_img_height(img) - y - 1 // line order in a DIB is reversed
                                                     : y);

        if (data->pix_fmt == AVI_PIX_DIB_RGB8)
        {
            // Rearrange channels to RGB order
            for (unsigned x = 0; x < data->width; x++)
            {
                img_line[3*x + 0] = line[3*x + 2];
                img_line[3*x + 1] = line[3*x + 1];
                img_line[3*x + 2] = line[3*x + 0];
            }
        }
        else
            memcpy(img_line, line, SKRY_get_bytes_per_pixel(img) * SKRY_get_img_width(img));
    }
    free(line);

    if (result) *result = SKRY_SUCCESS;

    return img;
}

static
struct SKRY_image *AVI_get_current_img(const struct SKRY_img_sequence *img_seq, enum SKRY_result *result)
{
    return AVI_get_img_by_index(img_seq, SKRY_get_curr_img_idx(img_seq), result);
}

static
void AVI_deactivate_img_seq(struct SKRY_img_sequence *img_seq)
{
    if (AVI_DATA(img_seq->data)->file)
    {
        fclose(AVI_DATA(img_seq->data)->file);
        AVI_DATA(img_seq->data)->file = 0;
    }
}

static
void AVI_free(struct SKRY_img_sequence *img_seq)
{
    if (img_seq)
    {
        if (img_seq->data)
        {
            struct AVI_data *avi_data = (struct AVI_data *)img_seq->data;
            free(avi_data->frame_offsets);
            free(avi_data->file_name);
            if (avi_data->file)
                fclose(avi_data->file);

            free(img_seq->data);
        }
        free(img_seq);
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

/// Returns on error
#define READ_CHUNK(error_msg)                   \
    do {                                        \
        last_chunk_pos = ftell(avi_data->file); \
        if (1 != fread(&chunk, sizeof(chunk), 1, avi_data->file))  \
        {                                       \
            LOG_MSG(SKRY_LOG_AVI, error_msg);   \
            FAIL(SKRY_AVI_MALFORMED_FILE);      \
        }                                       \
        last_chunk_size = cnd_swap_32(chunk.ck_size, is_machine_b_e); \
    } while (0);

/// Seeks to next AVI fragment (list or chunk)
#define SEEK_TO_NEXT()              \
    fseek(avi_data->file, last_chunk_pos + last_chunk_size \
          + sizeof(chunk.ck_id)     \
          + sizeof(chunk.ck_size),  \
          SEEK_SET);                \

struct SKRY_img_sequence *init_AVI(const char *file_name,
                                   SKRY_ImagePool *img_pool,
                                   enum SKRY_result *result)
{
    /*
        Expected AVI file structure:

            RIFF/AVI                         // AVI_file_header
            LIST: hdrl
            | avih                           // AVI_main_header
            | LIST: strl
            | | strh                         // AVI_stream_header
            | | for DIB: strf                // bitmap_info_header
            | | for DIB/8-bit: BMP palette   // BMP_palette
            | | ...
            | | (ignored)
            | | ...
            | |_____
            |_________
            ...
            (ignored; possibly 'JUNK' chunks, LIST:INFO)
            ...
            LIST: movi
            | ...
            | (frames)
            | ...
            |_________
            ...
            (ignored)
            ...
            idx1
              ...
              (index entries)                // AVI_old_index
              ...

    */
    struct AVI_chunk chunk;
    struct AVI_list list;
    long last_chunk_pos;
    uint32_t last_chunk_size;
    int is_machine_b_e = is_machine_big_endian();

    struct SKRY_img_sequence *img_seq = malloc(sizeof(*img_seq));
    FAIL_ON_NULL(img_seq);

    *img_seq = (struct SKRY_img_sequence) { 0 };
    img_seq->data = malloc(sizeof(struct AVI_data));
    FAIL_ON_NULL(img_seq->data);

    img_seq->type = SKRY_IMG_SEQ_AVI;
    img_seq->free = AVI_free;
    img_seq->get_curr_img = AVI_get_current_img;
    img_seq->get_curr_img_metadata = AVI_get_curr_img_metadata;
    img_seq->get_img_by_index = AVI_get_img_by_index;
    img_seq->deactivate_img_seq = AVI_deactivate_img_seq;

    struct AVI_data *avi_data = (struct AVI_data *)img_seq->data;
    *avi_data = (struct AVI_data) { 0 };
    avi_data->file_name = malloc(strlen(file_name) + 1);
    FAIL_ON_NULL(avi_data->file_name);
    strcpy(avi_data->file_name, file_name);
    avi_data->file = fopen(file_name, "rb");
    FAIL_ON_NULL(avi_data->file);

    struct AVI_file_header fheader;
    if (1 != fread(&fheader, sizeof(fheader), 1, avi_data->file))
    {
        LOG_MSG(SKRY_LOG_AVI, "Could not read file header.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }

    if (!FCC_COMPARE(fheader.riff, "RIFF") || !FCC_COMPARE(fheader.avi, "AVI "))
    {
        LOG_MSG(SKRY_LOG_AVI, "Invalid file header.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }

    struct AVI_list header_list;

    long header_list_pos = ftell(avi_data->file);

    if (1 != fread(&header_list, sizeof(header_list), 1, avi_data->file))
    {
        LOG_MSG(SKRY_LOG_AVI, "Could not load header list.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }

    if (!FCC_COMPARE(header_list.list, "LIST") ||
        !FCC_COMPARE(header_list.list_type, "hdrl"))
    {
        LOG_MSG(SKRY_LOG_AVI, "Invalid header list.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }

    READ_CHUNK("Could not read AVI header.");
    if (!FCC_COMPARE(chunk.ck_id, "avih"))
    {
        LOG_MSG(SKRY_LOG_AVI, "Invalid AVI header.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }
    struct AVI_main_header avi_header;
    if (1 != fread(&avi_header, sizeof(avi_header), 1, avi_data->file))
    {
        LOG_MSG(SKRY_LOG_AVI, "Could not read AVI header.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }

    img_seq->num_images = cnd_swap_32(avi_header.total_frames, is_machine_b_e);
    // This may be zero; if this is the case, we'll use the stream  header's 'length' field

    avi_data->width = cnd_swap_32(avi_header.width, is_machine_b_e);
    avi_data->height = cnd_swap_32(avi_header.height, is_machine_b_e);

    if (!(cnd_swap_32(avi_header.flags, is_machine_b_e) & AVIF_HAS_INDEX))
    {
        //TODO: don't do this; add ODML index support
        LOG_MSG(SKRY_LOG_AVI, "Index not present.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }

    SEEK_TO_NEXT();

    if (1 != fread(&list, sizeof(list), 1, avi_data->file))
    {
        LOG_MSG(SKRY_LOG_AVI, "Could not read stream list.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }
    if (!FCC_COMPARE(list.list, "LIST") || !FCC_COMPARE(list.list_type, "strl"))
    {
        LOG_MSG(SKRY_LOG_AVI, "Invalid stream list.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }

    READ_CHUNK("Could not read stream header.");
    if (!FCC_COMPARE(chunk.ck_id, "strh"))
    {
        LOG_MSG(SKRY_LOG_AVI, "Invalid stream header.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }
    struct AVI_stream_header stream_header;
    if (1 != fread(&stream_header, sizeof(stream_header), 1, avi_data->file))
    {
        LOG_MSG(SKRY_LOG_AVI, "Could not read stream header.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }
    if (!FCC_COMPARE(stream_header.fcc_type, "vids"))
    {
        LOG_MSG(SKRY_LOG_AVI, "Invalid stream header; expected type \"vids\", got \"%c%c%c%c\".",
                stream_header.fcc_type[0],
                stream_header.fcc_type[1],
                stream_header.fcc_type[2],
                stream_header.fcc_type[3]);
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }

    if (FCC_COMPARE(stream_header.fcc_handler, "\0\0\0"))
    {
        // Empty 'fcc_handler' means DIB by default
        stream_header.fcc_handler[0] = 'D';
        stream_header.fcc_handler[1] = 'I';
        stream_header.fcc_handler[2] = 'B';
        stream_header.fcc_handler[3] = ' ';
    }

    if (!FCC_COMPARE(stream_header.fcc_handler, "DIB ") &&
        !FCC_COMPARE(stream_header.fcc_handler, "Y800") &&
        !FCC_COMPARE(stream_header.fcc_handler, "Y8  "))
    {
        LOG_MSG(SKRY_LOG_AVI, "Unsupported video FCC: \"%c%c%c%c\".",
                stream_header.fcc_handler[0],
                stream_header.fcc_handler[1],
                stream_header.fcc_handler[2],
                stream_header.fcc_handler[3]);
        FAIL(SKRY_AVI_UNSUPPORTED_FORMAT);
    }
    int is_DIB = FCC_COMPARE(stream_header.fcc_handler, "DIB ");

    if (img_seq->num_images == 0)
    {
        img_seq->num_images = stream_header.length;
    }

    SEEK_TO_NEXT();

    READ_CHUNK("Could not read stream format.");
    if (!FCC_COMPARE(chunk.ck_id, "strf"))
    {
        LOG_MSG(SKRY_LOG_AVI, "Invalid stream format.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }
    struct bitmap_info_header bmp_hdr;
    if (1 != fread(&bmp_hdr, sizeof(bmp_hdr), 1, avi_data->file))
    {
        LOG_MSG(SKRY_LOG_AVI, "Could not read stream format.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }

    bmp_hdr.compression = cnd_swap_32(bmp_hdr.compression, is_machine_b_e);
    bmp_hdr.bit_count = cnd_swap_16(bmp_hdr.bit_count, is_machine_b_e);
    bmp_hdr.planes = cnd_swap_16(bmp_hdr.planes, is_machine_b_e);
    bmp_hdr.clr_used = cnd_swap_32(bmp_hdr.clr_used, is_machine_b_e);

    if (is_DIB && bmp_hdr.compression != BI_BITFIELDS && bmp_hdr.compression != BI_RGB ||
        bmp_hdr.planes != 1 ||
        bmp_hdr.bit_count != 8 && bmp_hdr.bit_count != 24)
    {
        LOG_MSG(SKRY_LOG_AVI, "Unsupported video format.");
        FAIL(SKRY_AVI_UNSUPPORTED_FORMAT);
    }
    if (is_DIB && bmp_hdr.bit_count == 8)
    {
        struct BMP_palette palette;

        if (1 != fread(&palette, sizeof(palette), 1, avi_data->file))
        {
            LOG_MSG(SKRY_LOG_AVI, "Could not read palette.");
            FAIL(SKRY_AVI_MALFORMED_FILE);
        }

        if (bmp_hdr.clr_used == 0)
            bmp_hdr.clr_used = 256;

        int is_mono_8 = (bmp_hdr.clr_used == 256);

        // Convert to an RGB-order palette
        for (size_t i = 0; i < bmp_hdr.clr_used; i++)
        {
            uint8_t r = palette.pal[i*4 + 2],
                    g = palette.pal[i*4 + 1],
                    b = palette.pal[i*4 + 0];

            if (r != i || g != i || b != i)
                is_mono_8 = 0;

            avi_data->palette.pal[3*i + 0] = r;
            avi_data->palette.pal[3*i + 1] = g;
            avi_data->palette.pal[3*i + 2] = b;
        }

        avi_data->pix_fmt = is_mono_8 ? AVI_PIX_DIB_MONO8 : AVI_PIX_DIB_PAL8;
    }
    else if (is_DIB && bmp_hdr.bit_count == 24)
        avi_data->pix_fmt = AVI_PIX_DIB_RGB8;
    else
        avi_data->pix_fmt = AVI_PIX_Y800;

    // Jump to the location immediately after 'hdrl'

    fseek(avi_data->file, header_list_pos + cnd_swap_32(header_list.list_size, is_machine_b_e)
          + sizeof(header_list.list)
          + sizeof(header_list.list_size),
          SEEK_SET);

    // Skip any additional fragments (e.g. 'JUNK' chunks)
    long stored_pos;
    do
    {
        stored_pos = ftell(avi_data->file);
        struct AVI_fragment fragment;
        if (1 != fread(&fragment, sizeof(fragment), 1, avi_data->file))
        {
            LOG_MSG(SKRY_LOG_AVI, "Missing MOVI list.");
            FAIL(SKRY_AVI_MALFORMED_FILE);
        }

        if (FCC_COMPARE(fragment.fcc, "LIST"))
        {
            fourcc_t list_type;
            if (1 != fread(&list_type, sizeof(list_type), 1, avi_data->file))
            {
                LOG_MSG(SKRY_LOG_AVI, "Incomplete LIST chunk.");
                FAIL(SKRY_AVI_MALFORMED_FILE);
            }

            // Found a list; if it is the 'movi' list, move the file pointer back;
            // the list will be re-read after the current 'while' loop.
            if (FCC_COMPARE(list_type, "movi"))
            {
                fseek(avi_data->file, stored_pos, SEEK_SET);
                break;
            }
            else
            {
                // Not the 'movi' list; skip it.
                // Must rewind back by length of the 'size' field,
                // because in a list it is not counted in 'size'.
                fseek(avi_data->file, -(int)sizeof(fragment.size), SEEK_CUR);
            }
        }

        // Skip the current fragment, whatever it is
        fseek(avi_data->file, cnd_swap_32(fragment.size, is_machine_b_e), SEEK_CUR);

    } while (1);

    if (1 != fread(&list, sizeof(list), 1, avi_data->file))
    {
        LOG_MSG(SKRY_LOG_AVI, "Could not read MOVI list.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }

    if (!FCC_COMPARE(list.list, "LIST") ||
        !FCC_COMPARE(list.list_type, "movi"))
    {
        LOG_MSG(SKRY_LOG_AVI, "Invalid MOVI list.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }

    long frame_chunks_start_ofs = ftell(avi_data->file) - sizeof(list.list_type);

    // Jump to the old-style AVI index
    fseek(avi_data->file, cnd_swap_32(list.list_size, is_machine_b_e) - sizeof(list.list_size),
          SEEK_CUR);

    READ_CHUNK("Could not read index.");
    if (!FCC_COMPARE(chunk.ck_id, "idx1")
        || cnd_swap_32(chunk.ck_size, is_machine_b_e) < img_seq->num_images * sizeof(struct AVI_old_index))
    {
        LOG_MSG(SKRY_LOG_AVI, "Invalid index.");
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }

    // Index may contain bogus entries, this will make it longer
    // than img_seq->num_images * sizeof(struct AVI_old_index)
    uint32_t index_length = cnd_swap_32(chunk.ck_size, is_machine_b_e);

    avi_data->frame_offsets = malloc(img_seq->num_images * sizeof(*avi_data->frame_offsets));
    FAIL_ON_NULL(avi_data->frame_offsets);

    struct AVI_old_index *avi_old_index = malloc(index_length);
    if (1 != fread(avi_old_index, index_length, 1, avi_data->file))
    {
        LOG_MSG(SKRY_LOG_AVI, "Index incomplete.");
        free(avi_old_index);
        FAIL(SKRY_AVI_MALFORMED_FILE);
    }

    size_t line_byte_count = avi_data->width * BYTES_PER_PIXEL[AVI_to_SKRY_pix_fmt[avi_data->pix_fmt]];
    if (IS_DIB(avi_data->pix_fmt))
        line_byte_count = UP4MULT(line_byte_count);

    struct AVI_old_index *curr_entry = avi_old_index;
    size_t valid_entry_counter = 0;
    while (curr_entry - avi_old_index < (ptrdiff_t)(index_length / sizeof(curr_entry))
           && valid_entry_counter < img_seq->num_images)
    {
        // Ignore bogus entries (they may have "7Fxx" as their ID)
        if (FCC_COMPARE(curr_entry->chunk_id, "00db") ||
            FCC_COMPARE(curr_entry->chunk_id, "00dc"))
        {
            if (cnd_swap_32(curr_entry->frame_size, is_machine_b_e) != line_byte_count * avi_data->height)
            {
                free(avi_old_index);
                FAIL(SKRY_AVI_MALFORMED_FILE);
            }
            else
            {
                avi_data->frame_offsets[valid_entry_counter] = frame_chunks_start_ofs + cnd_swap_32(curr_entry->offset, is_machine_b_e);
                valid_entry_counter++;
            }
        }
        curr_entry++;
    }

    // Check if frame offsets in the index are actually absolute file offsets
    fseek(avi_data->file, avi_data->frame_offsets[0] - frame_chunks_start_ofs, SEEK_SET);
    READ_CHUNK("Could not read first frame.");
    if ((FCC_COMPARE(chunk.ck_id, "00db") || FCC_COMPARE(chunk.ck_id, "00dc"))
        && chunk.ck_size == line_byte_count*avi_data->height)
    {
        // Indeed, index frame offsets are absolute; must correct the values in `avi_data`
        for (size_t i = 0; i < img_seq->num_images; i++)
            avi_data->frame_offsets[i] -= frame_chunks_start_ofs;
    }

    free(avi_old_index);
    fclose(avi_data->file);
    avi_data->file = 0;

    LOG_MSG(SKRY_LOG_AVI, "Video size: %ux%u, %zu frames, %s",
            avi_data->width, avi_data->height,
            img_seq->num_images,
            AVI_pixel_format_str[avi_data->pix_fmt]);

    base_init(img_seq, img_pool);

    if (result) *result = SKRY_SUCCESS;

    return img_seq;
}
