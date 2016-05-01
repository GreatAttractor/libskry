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
    BMP-related functions implementation.
*/

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bmp.h"
#include "../utils/logging.h"
#include "../utils/misc.h"
#include "image_internal.h"


#pragma pack(push)
#pragma pack(1)
struct bitmap_file_header
{
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t off_bits;
};
#pragma pack(pop)

/// Returns the least multiple of 4 which is >= x
#define UP4MULT(x) (((x) + 3)/4 * 4)

static
int is_mono8_palette(const struct SKRY_palette *palette)
{
    int is_mono8 = 1;
    for (size_t i = 0; i < SKRY_PALETTE_NUM_ENTRIES; i++)
    {
        const uint8_t *pal_entry = &palette->pal[3*i];
        if (pal_entry[0] != i ||
            pal_entry[1] != i ||
            pal_entry[2] != i)
        {
            is_mono8 = 0;
            break;
        }
    }
    return is_mono8;
}

static
void convert_BMP_palette(uint32_t num_used_pal_entries, const struct BMP_palette *bmp_pal, struct SKRY_palette *skry_pal)
{
    for (size_t i = 0; i < num_used_pal_entries; i++)
    {
        skry_pal->pal[3*i + 0] = bmp_pal->pal[i*4 + 2];
        skry_pal->pal[3*i + 1] = bmp_pal->pal[i*4 + 1];
        skry_pal->pal[3*i + 2] = bmp_pal->pal[i*4 + 0];
    }
}

SKRY_Image *load_BMP(const char *file_name,
                     enum SKRY_result *result)
{
    FILE *file = fopen(file_name, "rb");
    if (!file)
    {
        if (result)
            *result = SKRY_CANNOT_OPEN_FILE;
        return 0;
    }

    unsigned img_width, img_height;
    enum SKRY_pixel_format pix_fmt = SKRY_PIX_INVALID;

    int is_machine_b_e = is_machine_big_endian();

    struct bitmap_file_header BMP_file_hdr;
    struct bitmap_info_header BMP_info_hdr;

    if (fread(&BMP_file_hdr, sizeof(BMP_file_hdr), 1, file) != 1
        || fread(&BMP_info_hdr, sizeof(BMP_info_hdr), 1, file) != 1)
    {
        fclose(file);
        if (result)
            *result = SKRY_BMP_MALFORMED_FILE;
        return 0;
    }

    // Fields in a BMP are always little-endian, so swap them if running on a big-endian machine.
    uint16_t bits_per_pixel = cnd_swap_16(BMP_info_hdr.bit_count, is_machine_b_e);

    img_width = cnd_swap_32(BMP_info_hdr.width, is_machine_b_e);
    img_height = cnd_swap_32(BMP_info_hdr.height, is_machine_b_e);

    if (img_width == 0 || img_height == 0
        || cnd_swap_16(BMP_file_hdr.type, is_machine_b_e) != 'B'+((int)'M'<<8)
        || cnd_swap_16(BMP_info_hdr.planes, is_machine_b_e) != 1
        || bits_per_pixel != 8 && bits_per_pixel != 24 && bits_per_pixel != 32
        || cnd_swap_32(BMP_info_hdr.compression, is_machine_b_e) != BI_RGB && cnd_swap_32(BMP_info_hdr.compression, is_machine_b_e) != BI_BITFIELDS)
    {
        fclose(file);
        if (result)
            *result = SKRY_UNSUPPORTED_BMP_FILE;
        return 0;
    }

    size_t src_bytes_per_pixel = 0;

    if (bits_per_pixel == 8)
    {
        pix_fmt = SKRY_PIX_PAL8;
        src_bytes_per_pixel = 1;
    }
    else if (bits_per_pixel == 24 || bits_per_pixel == 32)
    {
        pix_fmt = SKRY_PIX_RGB8;
        src_bytes_per_pixel = bits_per_pixel/8;
    }

    SKRY_Image *img = create_internal_img();
    if (!img)
    {
        fclose(file);
        if (result) *result = SKRY_OUT_OF_MEMORY;
        return 0;
    }
    IMG_DATA(img)->pix_fmt = pix_fmt;
    IMG_DATA(img)->width = img_width;
    IMG_DATA(img)->height = img_height;
    IMG_DATA(img)->pixels = malloc(img_width * img_height * src_bytes_per_pixel);
    if (!IMG_DATA(img)->pixels)
    {
        fclose(file);
        SKRY_free_image(img);
        if (result)
            *result = SKRY_OUT_OF_MEMORY;
        return 0;
    }

    if (pix_fmt == SKRY_PIX_PAL8)
    {
        unsigned bmp_stride = UP4MULT(img_width); // line length in bytes in the BMP file's pixel data
        unsigned skip = bmp_stride - img_width;   // number of padding bytes at the end of a line

        uint32_t num_used_pal_entries = cnd_swap_32(BMP_info_hdr.clr_used, is_machine_b_e);
        if (num_used_pal_entries == 0)
            num_used_pal_entries = 256;

        // seek to the beginning of palette
        fseek(file, sizeof(BMP_file_hdr) + cnd_swap_32(BMP_info_hdr.size, is_machine_b_e), SEEK_SET);

        struct BMP_palette palette;
        if (fread(&palette, sizeof(palette), 1, file) != 1)
        {
            SKRY_free_image(img);
            fclose(file);
            if (result)
                *result = SKRY_BMP_MALFORMED_FILE;
            return img->free(img);
        }

        // convert to an RGB-order palette
        convert_BMP_palette(num_used_pal_entries, &palette, &IMG_DATA(img)->palette);

        fseek(file, cnd_swap_32(BMP_file_hdr.off_bits, is_machine_b_e), SEEK_SET);

        for (int y = img_height - 1; y >= 0; y--) // lines in BMP are stored bottom to top
        {
            if (fread(img->get_line(img, y), img_width, 1, file) != 1)
            {
                SKRY_free_image(img);
                fclose(file);
                if (result)
                    *result = SKRY_BMP_MALFORMED_FILE;
                return img->free(img);
            }

            if (skip > 0)
                fseek(file, skip, SEEK_CUR);
        }

        // If it is a 0-255 grayscale palette, set the pixel format accordingly
        if (is_mono8_palette(&IMG_DATA(img)->palette))
            IMG_DATA(img)->pix_fmt = SKRY_PIX_MONO8;

    }
    else if (pix_fmt == SKRY_PIX_RGB8)
    {
        unsigned bmp_stride = UP4MULT(img_width * src_bytes_per_pixel); // line length in bytes in the BMP file's pixel data
        unsigned skip = bmp_stride - img_width * src_bytes_per_pixel; // number of padding bytes at the end of a line

        fseek(file, cnd_swap_32(BMP_file_hdr.off_bits, is_machine_b_e), SEEK_SET);

        uint8_t *line =  malloc(img_width * src_bytes_per_pixel);
        if (!line)
        {
            fclose(file);
            SKRY_free_image(img);
            if (result)
                *result = SKRY_OUT_OF_MEMORY;
            return 0;
        }

        for (int y = img_height - 1; y >= 0; y--) // lines in BMP are stored bottom to top
        {
            uint8_t *img_line = img->get_line(img, y);

            if (src_bytes_per_pixel == 3)
            {
                if (fread(line, img_width * src_bytes_per_pixel, 1, file) != 1)
                {
                    SKRY_free_image(img);
                    fclose(file);
                    if (result)
                        *result = SKRY_BMP_MALFORMED_FILE;
                    return img->free(img);
                }

                // rearrange the channels to RGB order
                for (unsigned x = 0; x < img_width; x++)
                {
                    img_line[x*3 + 0] = line[x*3 + 2];
                    img_line[x*3 + 1] = line[x*3 + 1];
                    img_line[x*3 + 2] = line[x*3 + 0];
                }
            }
            else if (src_bytes_per_pixel == 4)
            {
                if (fread(line, img_width * src_bytes_per_pixel, 1, file) != 1)
                {
                    SKRY_free_image(img);
                    fclose(file);
                    if (result)
                        *result = SKRY_BMP_MALFORMED_FILE;
                    return img->free(img);
                }

                // remove the unused 4th byte from each pixel and rearrange the channels to RGB order
                for (unsigned x = 0; x < img_width; x++)
                {
                    img_line[x*3 + 0] = line[x*4 + 3];
                    img_line[x*3 + 1] = line[x*4 + 2];
                    img_line[x*3 + 2] = line[x*4 + 1];
                }
            }

            if (skip > 0)
                fseek(file, skip, SEEK_CUR);
        }

        free(line);
    }

    if (result)
        *result = SKRY_SUCCESS;

    fclose(file);

    LOG_MSG(SKRY_LOG_IMAGE, "Loaded BMP image from \"%s\" as object %p with pixel data at %p, "
                        "size %dx%d, %s.",
        file_name, (void *)img, (void *)IMG_DATA(img)->pixels,
        img_width, img_height, pix_fmt_str[IMG_DATA(img)->pix_fmt]);

    return img;
}

enum SKRY_result save_BMP(const SKRY_Image *img, const char *file_name)
{
    enum SKRY_pixel_format pix_fmt = SKRY_get_img_pix_fmt(img);

    assert(pix_fmt == SKRY_PIX_PAL8 ||
           pix_fmt == SKRY_PIX_RGB8 ||
           pix_fmt == SKRY_PIX_MONO8);

    unsigned width = SKRY_get_img_width(img),
             height = SKRY_get_img_height(img);


    struct bitmap_file_header bmfh;
    struct bitmap_info_header bmih;

    size_t bytes_per_pixel = BYTES_PER_PIXEL[pix_fmt];

    unsigned bmp_line_width = UP4MULT(width * bytes_per_pixel);

    // Fields in a BMP are always little-endian, so swap them if running on a big-endian machine
    int is_machine_b_e = is_machine_big_endian();

    bmfh.type = cnd_swap_16('B'+((int)'M'<<8), is_machine_b_e);
    bmfh.size = sizeof(bmfh) + sizeof(bmih) + height * bmp_line_width;
    if (pix_fmt == SKRY_PIX_PAL8 || pix_fmt == SKRY_PIX_MONO8)
    {
        bmfh.size += BMP_PALETTE_SIZE;
    }
    bmfh.size = cnd_swap_32(bmfh.size, is_machine_b_e);
    bmfh.reserved1 = 0;
    bmfh.reserved2 = 0;
    bmfh.off_bits = sizeof(bmih) + sizeof(bmfh);
    if (pix_fmt == SKRY_PIX_PAL8 || pix_fmt == SKRY_PIX_MONO8)
        bmfh.off_bits += BMP_PALETTE_SIZE;
    bmfh.off_bits = cnd_swap_32(bmfh.off_bits, is_machine_b_e);

    bmih.size = cnd_swap_32(sizeof(bmih), is_machine_b_e);
    bmih.width = cnd_swap_32(width, is_machine_b_e);
    bmih.height = cnd_swap_32(height, is_machine_b_e);
    bmih.planes = cnd_swap_16(1, is_machine_b_e);
    bmih.bit_count = cnd_swap_16(bytes_per_pixel * 8, is_machine_b_e);
    bmih.compression = cnd_swap_32(BI_RGB, is_machine_b_e);
    bmih.size_image = 0;
    bmih.x_pels_per_meter = cnd_swap_32(1000, is_machine_b_e);
    bmih.y_pels_per_meter = cnd_swap_32(1000, is_machine_b_e);
    bmih.clr_used = 0;
    bmih.clr_important = 0;

    FILE *file = fopen(file_name, "wb");
    if (!file)
        return SKRY_CANNOT_CREATE_FILE;

    fwrite(&bmfh, sizeof(bmfh), 1, file);
    fwrite(&bmih, sizeof(bmih), 1, file);

    if (pix_fmt == SKRY_PIX_PAL8 || pix_fmt == SKRY_PIX_MONO8)
    {
        uint8_t BMP_palette[BMP_PALETTE_SIZE];

        if (pix_fmt == SKRY_PIX_PAL8)
        {
            struct SKRY_palette img_pal;
            SKRY_get_palette(img, &img_pal);

            for (size_t i = 0; i < 256; i++)
            {
                BMP_palette[4*i + 0] = img_pal.pal[3*i + 2];
                BMP_palette[4*i + 1] = img_pal.pal[3*i + 1];
                BMP_palette[4*i + 2] = img_pal.pal[3*i + 0];
                BMP_palette[4*i + 3] = 0;
            }
        }
        else
        {
            for (size_t i = 0; i < 256; i++)
            {
                BMP_palette[4*i + 0] = i;
                BMP_palette[4*i + 1] = i;
                BMP_palette[4*i + 2] = i;
                BMP_palette[4*i + 3] = 0;
            }
        }

        fwrite(&BMP_palette, sizeof(BMP_palette), 1, file);
    }

    size_t skip = bmp_line_width - width*bytes_per_pixel;

    for (unsigned i = 0; i < height; i++)
    {
        void *line = SKRY_get_line(img, height-i-1);

        fwrite(line, width * bytes_per_pixel, 1, file);
        if (skip > 0)
            fwrite(line, skip, 1, file); // this is just padding, so write anything
    }

    fclose(file);

    return SKRY_SUCCESS;
}

enum SKRY_result get_BMP_metadata(const char *file_name,
                                  unsigned *width,
                                  unsigned *height,
                                  enum SKRY_pixel_format *pix_fmt)
{
    FILE *file = fopen(file_name, "rb");
    if (!file)
        return SKRY_CANNOT_OPEN_FILE;

    struct bitmap_file_header BMP_file_hdr;
    struct bitmap_info_header BMP_info_hdr;

    int is_machine_b_e = is_machine_big_endian();

    if (fread(&BMP_file_hdr, sizeof(BMP_file_hdr), 1, file) != 1
        || fread(&BMP_info_hdr, sizeof(BMP_info_hdr), 1, file) != 1
        || cnd_swap_16(BMP_file_hdr.type, is_machine_b_e) != 'B'+((int)'M'<<8))
    {
        fclose(file);
        return SKRY_BMP_MALFORMED_FILE;
    }

    // fields in a BMP are always little-endian, so swap them if running on a big-endian machine

    if (width)
        *width = cnd_swap_32(BMP_info_hdr.width, is_machine_b_e);

    if (height)
        *height = cnd_swap_32(BMP_info_hdr.height, is_machine_b_e);


    if (pix_fmt)
    {
        uint16_t bits_per_pixel = cnd_swap_16(BMP_info_hdr.bit_count, is_machine_b_e);
        switch (bits_per_pixel)
        {
        case 8:
            {
                uint32_t num_used_pal_entries = cnd_swap_32(BMP_info_hdr.clr_used, is_machine_b_e);
                if (num_used_pal_entries == 0)
                    num_used_pal_entries = 256;

                // seek to the beginning of palette
                fseek(file, sizeof(BMP_file_hdr) + cnd_swap_32(BMP_info_hdr.size, is_machine_b_e), SEEK_SET);

                struct BMP_palette bmp_pal;
                if (fread(&bmp_pal, sizeof(bmp_pal), 1, file) != 1)
                {
                    fclose(file);
                    return SKRY_BMP_MALFORMED_FILE;
                }

                struct SKRY_palette skry_pal;
                // convert to an RGB-order palette
                convert_BMP_palette(num_used_pal_entries, &bmp_pal, &skry_pal);
                if (is_mono8_palette(&skry_pal))
                    *pix_fmt = SKRY_PIX_MONO8;
                else
                    *pix_fmt = SKRY_PIX_PAL8;

                break;
            }

        case 24: // intentional fall-through
        case 32:
            *pix_fmt = SKRY_PIX_RGB8;
            break;

        default:
            fclose(file);
            return SKRY_UNSUPPORTED_BMP_FILE;
        }
    }

    fclose(file);

    return SKRY_SUCCESS;
}
