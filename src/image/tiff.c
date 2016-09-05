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
    TIFF-related functions implementation.
*/

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "image_internal.h"
#include "tiff.h"
#include "../utils/logging.h"
#include "../utils/misc.h"


#pragma pack(push)

#pragma pack(1)
struct TIFF_field
{
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint32_t value;
};

#pragma pack(1)
struct TIFF_header
{
    uint16_t id;
    uint16_t version;
    uint32_t dir_offset;
};

#pragma pack(pop)

enum tag_type { TT_BYTE = 1, TT_ASCII, TT_WORD, TT_DWORD, TT_RATIONAL };

const unsigned field_type_length[] =
{
    [TT_BYTE]     = 1,
    [TT_ASCII]    = 1,
    [TT_WORD]     = 2,
    [TT_DWORD]    = 4,
    [TT_RATIONAL] = 8
};

#define TIFF_VERSION                    42
#define TAG_IMAGE_WIDTH                 0x100
#define TAG_IMAGE_HEIGHT                0x101
#define TAG_BITS_PER_SAMPLE             0x102
#define TAG_COMPRESSION                 0x103
#define TAG_PHOTOMETRIC_INTERPRETATION  0x106
#define TAG_STRIP_OFFSETS               0x111
#define TAG_SAMPLES_PER_PIXEL           0x115
#define TAG_ROWS_PER_STRIP              0x116
#define TAG_STRIP_BYTE_COUNTS           0x117
#define TAG_PLANAR_CONFIGURATION        0x11C

#define NO_COMPRESSION              1
#define PLANAR_CONFIGURATION_CHUNKY 1
#define INTEL_BYTE_ORDER            (((uint16_t)'I' << 8) + 'I')
#define MOTOROLA_BYTE_ORDER         (((uint16_t)'M' << 8) + 'M')

#define PHMET_WHITE_IS_ZERO 0
#define PHMET_BLACK_IS_ZERO 1
#define PHMET_RGB           2

/// Reverses 8-bit grayscale values
static
void negate_grayscale_8(SKRY_Image *img)
{
    for (size_t j = 0; j < SKRY_get_img_height(img); j++)
    {
        uint8_t *line = (uint8_t *)SKRY_get_line(img, j);
        for (size_t i = 0; i < SKRY_get_img_width(img); i++)
            line[i] = 0xFF - line[i];
    }
}

/// Reverses values of a 16-bit grayscale buffer
static
void negate_grayscale_16(SKRY_Image *img)
{
    for (size_t j = 0; j < SKRY_get_img_height(img); j++)
    {
        uint16_t *line = (uint16_t *)SKRY_get_line(img, j);
        for (size_t i = 0; i < SKRY_get_img_width(img); i++)
            line[i] = 0xFFFF - line[i];
    }
}

enum SKRY_result parse_tag_BITS_PER_SAMPLE(const struct TIFF_field tiff_field,
                                           FILE *file,
                                           int endianess_diff,
                                           size_t *bits_per_sample)
{
    if (tiff_field.count == 1)
        *bits_per_sample = tiff_field.value;
    else
    {
        // Some files may have as many "bits per sample" values specified
        // as there are channels. Make sure they are all the same.
        fseek(file, tiff_field.value, SEEK_SET);

        uint16_t *field_buf = malloc(tiff_field.count * sizeof(*field_buf));
        fread(field_buf, tiff_field.count * sizeof(*field_buf), 1, file);

        int all_equal = 1;
        uint16_t first = field_buf[0];
        for (size_t j = 1; j < tiff_field.count; j++)
        {
            if (field_buf[j] != first)
            {
                all_equal = 0;
                break;
            }
        }
        free(field_buf);

        if (!all_equal)
            return SKRY_TIFF_DIFF_CHANNEL_BIT_DEPTHS;

        *bits_per_sample = cnd_swap_16(first, endianess_diff);
    }

    if (*bits_per_sample != 8 && *bits_per_sample != 16) //TODO: add 32-bit floating point support
        return SKRY_UNSUPPORTED_FILE_FORMAT;

    return SKRY_SUCCESS;
}

#define FAIL_ON_ERROR(error_code)         \
    do {                                  \
        SKRY_free_image(img);          \
        free(strip_offsets);              \
        free(strip_byte_counts);          \
        fclose(file);                     \
        if (result) *result = error_code; \
        return 0;                         \
    } while(0)

/// Returns null on error
SKRY_Image *load_TIFF(const char *file_name,
                      enum SKRY_result *result ///< If not null, receives operation result
)
{
    unsigned img_width = 0, img_height = 0;
    enum SKRY_pixel_format pix_fmt = SKRY_PIX_INVALID;
    SKRY_Image *img = 0;
    enum SKRY_result loc_result;

    uint32_t *strip_offsets = 0;
    uint32_t *strip_byte_counts = 0;

    FILE *file = fopen(file_name, "rb");
    if (!file)
    {
        if (result) *result = SKRY_CANNOT_OPEN_FILE;
        return 0;
    }

    struct TIFF_header tiff_header;
    if (1 != fread(&tiff_header, sizeof(tiff_header), 1, file))
        FAIL_ON_ERROR(SKRY_TIFF_INCOMPLETE_HEADER);

    int is_machine_b_e = is_machine_big_endian();
    int is_file_b_e = (tiff_header.id == MOTOROLA_BYTE_ORDER);
    int endianess_diff = (is_machine_b_e != is_file_b_e);

    if (cnd_swap_16(tiff_header.version, endianess_diff) != TIFF_VERSION)
        FAIL_ON_ERROR(SKRY_TIFF_UNKNOWN_VERSION);

    // Seek to the first TIFF directory
    fseek(file, cnd_swap_32(tiff_header.dir_offset, endianess_diff), SEEK_SET);

    uint16_t num_dir_entries;
    if (1 != fread(&num_dir_entries, sizeof(num_dir_entries), 1, file))
        FAIL_ON_ERROR(SKRY_TIFF_NUM_DIR_ENTR_TAG_INCOMPLETE);

    num_dir_entries = cnd_swap_16(num_dir_entries, endianess_diff);

    size_t num_strips = 0;
    size_t bits_per_sample = 0;
    unsigned rows_per_strip = 0;
    int photometric_interpretation = -1;
    int samples_per_pixel = 0;

    long next_field_pos = ftell(file);
    for (size_t i = 0; i < num_dir_entries; i++)
    {
        struct TIFF_field tiff_field;

        fseek(file, next_field_pos, SEEK_SET);

        if (1 != fread(&tiff_field, sizeof(tiff_field), 1, file))
            FAIL_ON_ERROR(SKRY_TIFF_INCOMPLETE_FIELD);

        next_field_pos = ftell(file);

        tiff_field.tag = cnd_swap_16(tiff_field.tag, endianess_diff);
        tiff_field.type = cnd_swap_16(tiff_field.type, endianess_diff);
        tiff_field.count = cnd_swap_32(tiff_field.count, endianess_diff);
        if (tiff_field.count > 1 || tiff_field.type == TT_DWORD)
        {
            tiff_field.value = cnd_swap_32(tiff_field.value, endianess_diff);
        }
        else if (tiff_field.count == 1 && tiff_field.type == TT_WORD)
        {
            // This is a special case where a 16-bit value is stored in
            // a 32-bit field, always in the lower-address bytes. So if
            // the machine is big-endian, the value always has to be
            // shifted right by 16 bits first, regardless of the file's
            // endianess, and only then swapped, if the machine and file
            // endianesses differ.
            if (is_machine_b_e)
                tiff_field.value >>= 16;

            tiff_field.value = cnd_swap_16_in_32(tiff_field.value, endianess_diff);
        }

        switch (tiff_field.tag)
        {
        case TAG_IMAGE_WIDTH: img_width = tiff_field.value; break;

        case TAG_IMAGE_HEIGHT: img_height = tiff_field.value; break;

        case TAG_BITS_PER_SAMPLE:
            loc_result = parse_tag_BITS_PER_SAMPLE(tiff_field, file, endianess_diff, &bits_per_sample);
            if (SKRY_SUCCESS != loc_result)
                FAIL_ON_ERROR(loc_result);
            break;

        case TAG_COMPRESSION:
            if (tiff_field.value != NO_COMPRESSION)
                FAIL_ON_ERROR(SKRY_TIFF_COMPRESSED);
            break;

        case TAG_PHOTOMETRIC_INTERPRETATION: photometric_interpretation = tiff_field.value; break;

        case TAG_STRIP_OFFSETS:
            num_strips = tiff_field.count;
            free(strip_offsets);
            strip_offsets = malloc(num_strips * sizeof(*strip_offsets));
            if (1 == num_strips)
                strip_offsets[0] = tiff_field.value;
            else
            {
                fseek(file, tiff_field.value, SEEK_SET);
                for (size_t i = 0; i < num_strips; i++)
                {
                    fread(&strip_offsets[i], sizeof(strip_offsets[i]), 1, file);
                    strip_offsets[i] = cnd_swap_32(strip_offsets[i], endianess_diff);
                }
            }
            break;

        case TAG_SAMPLES_PER_PIXEL: samples_per_pixel = tiff_field.value; break;

        case TAG_ROWS_PER_STRIP: rows_per_strip = tiff_field.value; break;

        case TAG_STRIP_BYTE_COUNTS:
            free(strip_byte_counts);
            strip_byte_counts = malloc(tiff_field.count * sizeof(*strip_byte_counts));
            if (1 == tiff_field.count)
                strip_byte_counts[0] = tiff_field.value;
            else
            {
                fseek(file, tiff_field.value, SEEK_SET);
                for (size_t i = 0; i < tiff_field.count; i++)
                {
                    fread(&strip_byte_counts[i], sizeof(strip_byte_counts[i]), 1, file);
                    strip_byte_counts[i] = cnd_swap_32(strip_byte_counts[i], endianess_diff);
                }
            }
            break;

        case TAG_PLANAR_CONFIGURATION:
            if (tiff_field.value != PLANAR_CONFIGURATION_CHUNKY)
                FAIL_ON_ERROR(SKRY_TIFF_UNSUPPORTED_PLANAR_CONFIG);
            break;
        }
    }

    if (0 == rows_per_strip && 1 == num_strips)
        // If there is only 1 strip, it contains all the rows
        rows_per_strip = img_height;

    // Validate the values

    if (samples_per_pixel == 1 && photometric_interpretation != PHMET_BLACK_IS_ZERO && photometric_interpretation != PHMET_WHITE_IS_ZERO ||
        samples_per_pixel == 3 && photometric_interpretation != PHMET_RGB ||
        samples_per_pixel != 1 && samples_per_pixel != 3)
    {
        FAIL_ON_ERROR(SKRY_UNSUPPORTED_PIXEL_FORMAT);
    }

    if (samples_per_pixel == 1)
    {
        if (bits_per_sample == 8)
            pix_fmt = SKRY_PIX_MONO8;
        else if (bits_per_sample == 16)
            pix_fmt = SKRY_PIX_MONO16;
    }
    else if (samples_per_pixel == 3)
    {
        if (bits_per_sample == 8)
            pix_fmt = SKRY_PIX_RGB8;
        else if (bits_per_sample == 16)
            pix_fmt = SKRY_PIX_RGB16;
    }

    img = SKRY_new_image(img_width, img_height, pix_fmt, 0, 0);
    if (!img)
        FAIL_ON_ERROR(SKRY_OUT_OF_MEMORY);

    size_t curr_line = 0;
    for (size_t i = 0; i < num_strips; i++)
    {
        fseek(file, strip_offsets[i], SEEK_SET);

        for (size_t j = 0; j < rows_per_strip && curr_line < img_height; j++, curr_line++)
        {
            size_t num_bytes_to_read = img_width * BYTES_PER_PIXEL[pix_fmt];
            if (1 != fread(SKRY_get_line(img, curr_line), num_bytes_to_read, 1, file))
            {
                LOG_MSG(SKRY_LOG_IMAGE, "The file is incomplete: pixel data in strip %zu is too short; "
                                        "expected %"PRIu32" bytes.",
                                        i, strip_byte_counts[i]);
                FAIL_ON_ERROR(SKRY_TIFF_INCOMPLETE_PIXEL_DATA);
            }
        }
    }

    if ((pix_fmt == SKRY_PIX_MONO16 || pix_fmt == SKRY_PIX_RGB16) && endianess_diff)
        swap_words16(img);

    if (photometric_interpretation == PHMET_WHITE_IS_ZERO)
    {
        // Reverse the values so that "black" is zero, "white" is 255 or 65535.
        if (pix_fmt == SKRY_PIX_MONO8)
            negate_grayscale_8(img);
        else if (pix_fmt == SKRY_PIX_MONO16)
            negate_grayscale_16(img);
    }

    fclose(file);

    if (result)
        *result = SKRY_SUCCESS;

    LOG_MSG(SKRY_LOG_IMAGE, "Loaded TIFF image from \"%s\" as object %p with pixel data at %p, "
                        "size %ux%u, %s.",
        file_name, (void *)img, (void *)IMG_DATA(img)->pixels,
        img_width, img_height, pix_fmt_str[img->pix_fmt]);


    return img;
}

#undef FAIL_ON_ERROR

enum SKRY_result save_TIFF(const SKRY_Image *img, const char *file_name)
{
    enum SKRY_pixel_format pix_fmt = SKRY_get_img_pix_fmt(img);
    assert(pix_fmt == SKRY_PIX_MONO8 ||
           pix_fmt == SKRY_PIX_MONO16 ||
           pix_fmt == SKRY_PIX_RGB8 ||
           pix_fmt == SKRY_PIX_RGB16);

    unsigned img_width = SKRY_get_img_width(img),
             img_height = SKRY_get_img_height(img);

    FILE *file = fopen(file_name, "wb");
    if (!file)
        return SKRY_CANNOT_CREATE_FILE;

    int is_machine_b_e = is_machine_big_endian();

    // Note: a 16-bit value (a "ttWord") stored in a 32-bit 'tiff_field.value' has to be
    // always "left-aligned", i.e. stored in the lower-address two bytes in the file,
    // regardless of the file's and machine's endianess.
    //
    // This means that on a big-endian machine it has to be always shifted left by 16 bits
    // prior to writing to file.

    struct TIFF_header tiff_header;
    tiff_header.id = is_machine_b_e ? MOTOROLA_BYTE_ORDER : INTEL_BYTE_ORDER;
    tiff_header.version = TIFF_VERSION;
    tiff_header.dir_offset = sizeof(tiff_header);
    fwrite(&tiff_header, sizeof(tiff_header), 1, file);

    uint16_t num_dir_entries = 10;
    fwrite(&num_dir_entries, sizeof(num_dir_entries), 1, file);

    uint32_t next_dir_offset = 0;

    struct TIFF_field field;

    field.tag = TAG_IMAGE_WIDTH;
    field.type = TT_WORD;
    field.count = 1;
    field.value = img_width;
    if (is_machine_b_e) field.value <<= 16;
    fwrite(&field, sizeof(field), 1, file);

    field.tag = TAG_IMAGE_HEIGHT;
    field.type = TT_WORD;
    field.count = 1;
    field.value = img_height;
    if (is_machine_b_e) field.value <<= 16;
    fwrite(&field, sizeof(field), 1, file);

    field.tag = TAG_BITS_PER_SAMPLE;
    field.type = TT_WORD;
    field.count = 1;
    switch (pix_fmt)
    {
    case SKRY_PIX_MONO8:
    case SKRY_PIX_RGB8:
        field.value = 8; break;
    case SKRY_PIX_MONO16:
    case SKRY_PIX_RGB16:
        field.value = 16; break;
    default:
        assert(0); break;
    }
    if (is_machine_b_e) field.value <<= 16;
    fwrite(&field, sizeof(field), 1, file);

    field.tag = TAG_COMPRESSION;
    field.type = TT_WORD;
    field.count = 1;
    field.value = NO_COMPRESSION;
    if (is_machine_b_e) field.value <<= 16;
    fwrite(&field, sizeof(field), 1, file);

    field.tag = TAG_PHOTOMETRIC_INTERPRETATION;
    field.type = TT_WORD;
    field.count = 1;
    switch (pix_fmt)
    {
    case SKRY_PIX_MONO8:
    case SKRY_PIX_MONO16:
        field.value = PHMET_BLACK_IS_ZERO; break;
    case SKRY_PIX_RGB8:
    case SKRY_PIX_RGB16:
        field.value = PHMET_RGB; break;
    default:
        assert(0); break;
    }
    if (is_machine_b_e) field.value <<= 16;
    fwrite(&field, sizeof(field), 1, file);

    field.tag = TAG_STRIP_OFFSETS;
    field.type = TT_WORD;
    field.count = 1;
    // we write the header, num. of directory entries, 10 fields and a next directory offset (==0); pixel data starts next
    field.value = sizeof(tiff_header) + sizeof(num_dir_entries) + 10*sizeof(field) + sizeof(next_dir_offset);
    fwrite(&field, sizeof(field), 1, file);

    field.tag = TAG_SAMPLES_PER_PIXEL;
    field.type = TT_WORD;
    field.count = 1;
    switch (pix_fmt)
    {
    case SKRY_PIX_MONO8:
    case SKRY_PIX_MONO16:
        field.value = 1; break;
    case SKRY_PIX_RGB8:
    case SKRY_PIX_RGB16:
        field.value = 3; break;
    default:
        assert(0); break;
    }
    if (is_machine_b_e) field.value <<= 16;
    fwrite(&field, sizeof(field), 1, file);

    field.tag = TAG_ROWS_PER_STRIP;
    field.type = TT_WORD;
    field.count = 1;
    field.value = img_height; // there is only one strip for the whole image
    if (is_machine_b_e) field.value <<= 16;
    fwrite(&field, sizeof(field), 1, file);

    field.tag = TAG_STRIP_BYTE_COUNTS;
    field.type = TT_DWORD;
    field.count = 1;
    field.value = img_width * img_height * BYTES_PER_PIXEL[pix_fmt]; // there is only one strip for the whole image
    fwrite(&field, sizeof(field), 1, file);

    field.tag = TAG_PLANAR_CONFIGURATION;
    field.type = TT_WORD;
    field.count = 1;
    field.value = PLANAR_CONFIGURATION_CHUNKY; // there is only one strip for the whole image
    if (is_machine_b_e) field.value <<= 16;
    fwrite(&field, sizeof(field), 1, file);

    // write the next directory offset (0 = no other directories)
    fwrite(&next_dir_offset, sizeof(next_dir_offset), 1, file);

    for (unsigned i = 0; i < img_height; i++)
        fwrite(SKRY_get_line(img, i), img_width * BYTES_PER_PIXEL[pix_fmt], 1, file);

    fclose(file);
    return SKRY_SUCCESS;
}


#define FAIL_ON_ERROR(error_code)         \
    do {                                  \
        fclose(file);                     \
        return error_code;                \
    } while(0)

/// Returns metadata without reading the pixel data
enum SKRY_result get_TIFF_metadata(const char *file_name,
                                   unsigned *width,  ///< If not null, receives image width
                                   unsigned *height, ///< If not null, receives image height
                                   enum SKRY_pixel_format *pix_fmt ///< If not null, receives pixel format
)
{
    FILE *file = fopen(file_name, "rb");

    if (!file)
        return SKRY_CANNOT_OPEN_FILE;

    enum SKRY_result loc_result;
    struct TIFF_header tiff_header;
    if (1 != fread(&tiff_header, sizeof(tiff_header), 1, file))
        FAIL_ON_ERROR(SKRY_TIFF_INCOMPLETE_HEADER);

    int is_machine_b_e = is_machine_big_endian();
    int is_file_b_e = (tiff_header.id == MOTOROLA_BYTE_ORDER);
    int endianess_diff = (is_machine_b_e != is_file_b_e);

    if (cnd_swap_16(tiff_header.version, endianess_diff) != TIFF_VERSION)
        FAIL_ON_ERROR(SKRY_TIFF_UNKNOWN_VERSION);

    // Seek to the first TIFF directory
    fseek(file, cnd_swap_32(tiff_header.dir_offset, endianess_diff), SEEK_SET);

    uint16_t num_dir_entries;
    if (1 != fread(&num_dir_entries, sizeof(num_dir_entries), 1, file))
        FAIL_ON_ERROR(SKRY_TIFF_NUM_DIR_ENTR_TAG_INCOMPLETE);

    num_dir_entries = cnd_swap_16(num_dir_entries, endianess_diff);

    unsigned img_width = UINT_MAX,
             img_height = UINT_MAX;

    size_t bits_per_sample = 0, samples_per_pixel = 0;
    unsigned photometric_interpretation = UINT_MAX;

    long next_field_pos = ftell(file);
    for (size_t i = 0; i < num_dir_entries; i++)
    {
        struct TIFF_field tiff_field;

        fseek(file, next_field_pos, SEEK_SET);
        if (1 != fread(&tiff_field, sizeof(tiff_field), 1, file))
            FAIL_ON_ERROR(SKRY_TIFF_INCOMPLETE_FIELD);

        next_field_pos = ftell(file);

        tiff_field.tag = cnd_swap_16(tiff_field.tag, endianess_diff);
        tiff_field.type = cnd_swap_16(tiff_field.type, endianess_diff);
        tiff_field.count = cnd_swap_32(tiff_field.count, endianess_diff);
        if (tiff_field.count > 1 || tiff_field.type == TT_DWORD)
            tiff_field.value = cnd_swap_32(tiff_field.value, endianess_diff);
        else if (tiff_field.count == 1 && tiff_field.type == TT_WORD)
        {
            // This is a special case where a 16-bit value is stored in
            // a 32-bit field, always in the lower-address bytes. So if
            // the machine is big-endian, the value always has to be
            // shifted right by 16 bits first, regardless of the file's
            // endianess, and only then swapped, if the machine and file
            // endianesses differ.
            if (is_machine_b_e)
                tiff_field.value >>= 16;

            tiff_field.value = cnd_swap_16_in_32(tiff_field.value, endianess_diff);
        }

        switch (tiff_field.tag)
        {
        case TAG_IMAGE_WIDTH: img_width = tiff_field.value; break;
        case TAG_IMAGE_HEIGHT: img_height = tiff_field.value; break;
        case TAG_SAMPLES_PER_PIXEL: samples_per_pixel = tiff_field.value; break;
        case TAG_PHOTOMETRIC_INTERPRETATION: photometric_interpretation = tiff_field.value; break;
        case TAG_BITS_PER_SAMPLE:
            loc_result = parse_tag_BITS_PER_SAMPLE(tiff_field,
                                                   file,
                                                   endianess_diff,
                                                   &bits_per_sample);
            if (SKRY_SUCCESS != loc_result)
                FAIL_ON_ERROR(loc_result);
            break;

        }
    }

    if (samples_per_pixel == 1 && photometric_interpretation != PHMET_BLACK_IS_ZERO && photometric_interpretation != PHMET_WHITE_IS_ZERO ||
        samples_per_pixel == 3 && photometric_interpretation != PHMET_RGB ||
        samples_per_pixel != 1 && samples_per_pixel != 3)
    {
        FAIL_ON_ERROR(SKRY_UNSUPPORTED_PIXEL_FORMAT);
    }

    fclose(file);

    if (width) *width = img_width;
    if (height) *height = img_height;
    if (pix_fmt)
    {
        if (samples_per_pixel == 1)
        {
            if (bits_per_sample == 8)
                *pix_fmt = SKRY_PIX_MONO8;
            else if (bits_per_sample == 16)
                *pix_fmt = SKRY_PIX_MONO16;
        }
        else if (samples_per_pixel == 3)
        {
            if (bits_per_sample == 8)
                *pix_fmt = SKRY_PIX_RGB8;
            else if (bits_per_sample == 16)
                *pix_fmt = SKRY_PIX_RGB16;
        }
    }

    return SKRY_SUCCESS;
}

#undef FAIL_ON_ERROR
