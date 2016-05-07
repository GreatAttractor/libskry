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
    Image-handling code implementation.
*/

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <skry/defs.h>
#include <skry/image.h>

#include "bmp.h"
#include "tiff.h"
#include "image_internal.h"
#include "../utils/logging.h"
#include "../utils/misc.h"


const size_t BYTES_PER_PIXEL[SKRY_NUM_PIX_FORMATS] =
{
    [SKRY_PIX_INVALID] = 0,  // unused

    [SKRY_PIX_PAL8]    = 1,
    [SKRY_PIX_MONO8]   = 1,
    [SKRY_PIX_RGB8]    = 3,
    [SKRY_PIX_BGRA8]   = 4,

    [SKRY_PIX_MONO16]  = 2,
    [SKRY_PIX_RGB16]   = 6,
    [SKRY_PIX_RGBA16]  = 8,

    [SKRY_PIX_MONO32F] = 4,
    [SKRY_PIX_RGB32F]  = 12,

    [SKRY_PIX_MONO64F] = 8,
    [SKRY_PIX_RGB64F]  = 24
};

const size_t NUM_CHANNELS[SKRY_NUM_PIX_FORMATS] =
{
    [SKRY_PIX_INVALID] = 0,  // unused

    [SKRY_PIX_PAL8]    = 3,
    [SKRY_PIX_MONO8]   = 1,
    [SKRY_PIX_RGB8]    = 3,
    [SKRY_PIX_BGRA8]   = 4,

    [SKRY_PIX_MONO16]  = 1,
    [SKRY_PIX_RGB16]   = 3,
    [SKRY_PIX_RGBA16]  = 4,

    [SKRY_PIX_MONO32F] = 1,
    [SKRY_PIX_RGB32F]  = 3,

    [SKRY_PIX_MONO64F] = 1,
    [SKRY_PIX_RGB64F]  = 3
};

const size_t BITS_PER_CHANNEL[SKRY_NUM_PIX_FORMATS] =
{
    [SKRY_PIX_INVALID] = 0,  // unused

    [SKRY_PIX_PAL8]    = 8,
    [SKRY_PIX_MONO8]   = 8,
    [SKRY_PIX_RGB8]    = 8,
    [SKRY_PIX_BGRA8]   = 8,

    [SKRY_PIX_MONO16]  = 16,
    [SKRY_PIX_RGB16]   = 16,
    [SKRY_PIX_RGBA16]  = 16,

    [SKRY_PIX_MONO32F] = 32,
    [SKRY_PIX_RGB32F]  = 32,

    [SKRY_PIX_MONO64F] = 64,
    [SKRY_PIX_RGB64F]  = 64
};

const size_t OUTPUT_FMT_BITS_PER_CHANNEL[SKRY_OUTP_FMT_LAST] =
{
    [SKRY_BMP_8]   = 8,
    [SKRY_PNG_8]   = 8,
    [SKRY_TIFF_16] = 16
};

const unsigned SUPPORTED_OUTP_FMT[] =
{
    SKRY_BMP_8,
    SKRY_TIFF_16,
};


// ------------ Implementation of the internal image class ---------------------

static
struct SKRY_image *free_internal_img(struct SKRY_image *img)
{
    LOG_MSG(SKRY_LOG_IMAGE, "Freeing image pixels array at %p.", IMG_DATA(img)->pixels);
    free(IMG_DATA(img)->pixels);
    free(IMG_DATA(img));
    free(img);
    return 0;
}

static
unsigned get_internal_img_width(const struct SKRY_image *img)
{
    return IMG_DATA(img)->width;
}

static
unsigned get_internal_img_height(const struct SKRY_image *img)
{
    return IMG_DATA(img)->height;
}

static
ptrdiff_t get_internal_line_stride_in_bytes(const struct SKRY_image *img)
{
    return (ptrdiff_t)(BYTES_PER_PIXEL[IMG_DATA(img)->pix_fmt] * IMG_DATA(img)->width);
}

static
size_t get_internal_img_bytes_per_pixel(const struct SKRY_image *img)
{
    return BYTES_PER_PIXEL[IMG_DATA(img)->pix_fmt];
}

static
void *get_internal_img_line(const struct SKRY_image *img, size_t line)
{
    return (char *)IMG_DATA(img)->pixels + line * get_internal_line_stride_in_bytes(img);
}

static
enum SKRY_pixel_format get_internal_img_pix_fmt(const struct SKRY_image *img)
{
    return IMG_DATA(img)->pix_fmt;
}

/// Fills 'pal'; returns SKRY_NO_PALETTE if image does not contain a palette
static
enum SKRY_result get_internal_img_palette (const struct SKRY_image *img, struct SKRY_palette *pal)
{
    if (IMG_DATA(img)->pix_fmt != SKRY_PIX_PAL8)
        return SKRY_NO_PALETTE;
    else
    {
        memcpy(pal, &IMG_DATA(img)->palette, sizeof(*pal));
    }
    return SKRY_SUCCESS;
}

struct SKRY_image *create_internal_img(void)
{
    struct SKRY_image *img = malloc(sizeof(*img));
    if (!img)
        return 0;

    memset(img, 0, sizeof(*img));

    img->data = malloc(sizeof(struct internal_img_data));
    if (!img->data)
    {
        free(img);
        return 0;
    }

    IMG_DATA(img)->pix_fmt = SKRY_PIX_INVALID;
    IMG_DATA(img)->width = IMG_DATA(img)->height = 0;
    IMG_DATA(img)->pixels = 0;

    img->free                     = free_internal_img;
    img->get_width                = get_internal_img_width;
    img->get_height               = get_internal_img_height;
    img->get_line_stride_in_bytes = get_internal_line_stride_in_bytes;
    img->get_bytes_per_pixel      = get_internal_img_bytes_per_pixel;
    img->get_line                 = get_internal_img_line;
    img->get_pix_fmt              = get_internal_img_pix_fmt;
    img->get_palette              = get_internal_img_palette;

    return img;
}

// -------------- Public interface implementation ------------------------------

struct SKRY_image *SKRY_free_image(struct SKRY_image *img)
{
    if (img)
    {
        LOG_MSG(SKRY_LOG_IMAGE, "Freeing image %p.", (void *)img);
        img->free(img);
    }
    return 0;
}

unsigned SKRY_get_img_width(const struct SKRY_image *img)
{
    return img->get_width(img);
}

unsigned SKRY_get_img_height(const struct SKRY_image *img)
{
    return img->get_height(img);
}

ptrdiff_t SKRY_get_line_stride_in_bytes(const struct SKRY_image *img)
{
    return img->get_line_stride_in_bytes(img);
}

size_t SKRY_get_bytes_per_pixel(const struct SKRY_image *img)
{
    return img->get_bytes_per_pixel(img);
}

void *SKRY_get_line (const struct SKRY_image *img, size_t line)
{
    return img->get_line(img, line);
}

enum SKRY_pixel_format SKRY_get_img_pix_fmt(const struct SKRY_image *img)
{
    return img->get_pix_fmt(img);
}

void SKRY_resize_and_translate(
    const struct SKRY_image *src_img,
    struct SKRY_image *dest_img,
    int src_x_min,
    int src_y_min,
    unsigned width,
    unsigned height,
    int dest_x_ofs,
    int dest_y_ofs,
    int clear_to_zero
)
{
    assert(SKRY_get_img_pix_fmt(src_img) == SKRY_get_img_pix_fmt(dest_img));

    size_t bytes_per_pixel = BYTES_PER_PIXEL[SKRY_get_img_pix_fmt(src_img)];

    // start and end (inclusive) coordinates to fill in the output image
    int dest_x_start = (dest_x_ofs < 0) ? 0 : dest_x_ofs;
    int dest_y_start = (dest_y_ofs < 0) ? 0 : dest_y_ofs;

    int dest_x_end = SKRY_MIN(dest_x_ofs + src_x_min + (int)width - 1, (int)SKRY_get_img_width(dest_img)-1);
    int dest_y_end = SKRY_MIN(dest_y_ofs + src_y_min + (int)height - 1, (int)SKRY_get_img_height(dest_img)-1);

    if (dest_y_end < dest_y_start || dest_x_end < dest_x_start)
    {
        if (clear_to_zero)
            for (unsigned y = 0; y < SKRY_get_img_height(dest_img); y++)
                memset(SKRY_get_line(dest_img, y), 0, SKRY_get_img_width(dest_img) * bytes_per_pixel);
        return;
    }

    if (clear_to_zero)
    {
        // unchanged lines at the top
        for (int y = 0; y < dest_y_start; y++)
            memset(SKRY_get_line(dest_img, y), 0, SKRY_get_img_width(dest_img) * bytes_per_pixel);

        // unchanged lines at the bottom
        for (unsigned y = dest_y_end + 1; y < SKRY_get_img_height(dest_img); y++)
            memset(SKRY_get_line(dest_img, y), 0, SKRY_get_img_width(dest_img) * bytes_per_pixel);

        for (int y = dest_y_start; y <= dest_y_end; y++)
        {
            // columns to the left of the target area
            memset(SKRY_get_line(dest_img, y), 0, dest_x_start * bytes_per_pixel);

            // columns to the right of the target area
            memset((uint8_t *)SKRY_get_line(dest_img, y) + (dest_x_end + 1)*bytes_per_pixel, 0,
                   (SKRY_get_img_width(dest_img) - 1 - dest_x_end)*bytes_per_pixel);
        }
    }

    // copy the pixels line by line
    for (int y = dest_y_start; y <= dest_y_end; y++)
    {
        memcpy((uint8_t *)SKRY_get_line(dest_img, y) + dest_x_start * bytes_per_pixel,
               (uint8_t *)SKRY_get_line(src_img, y - dest_y_ofs + src_y_min) + (dest_x_start - dest_x_ofs + src_x_min) * bytes_per_pixel,
               (dest_x_end - dest_x_start + 1) * bytes_per_pixel);

    }
}

struct SKRY_image *SKRY_new_image(
    unsigned width, unsigned height, enum SKRY_pixel_format pixel_format,
    const struct SKRY_palette *palette,
    int zero_fill)
{
    assert(width > 0);
    assert(height > 0);
    assert(pixel_format > SKRY_PIX_INVALID && pixel_format < SKRY_NUM_PIX_FORMATS);

    struct SKRY_image *img = create_internal_img();
    if (!img)
        return 0;

    IMG_DATA(img)->width = width;
    IMG_DATA(img)->height = height;
    IMG_DATA(img)->pix_fmt = pixel_format;
    size_t pixel_total_bytes = width * height * BYTES_PER_PIXEL[pixel_format];
    IMG_DATA(img)->pixels = malloc(pixel_total_bytes);
    if (!IMG_DATA(img)->pixels)
    {
        SKRY_free_image(img);
        return 0;
    }
    if (zero_fill)
        memset(IMG_DATA(img)->pixels, 0, pixel_total_bytes);

    if (pixel_format == SKRY_PIX_PAL8 && palette)
        memcpy(&IMG_DATA(img)->palette, palette, sizeof(*palette));

    return img;
}

struct SKRY_image *SKRY_convert_pix_fmt(const struct SKRY_image *src_img, enum SKRY_pixel_format dest_pix_fmt)
{
    return SKRY_convert_pix_fmt_of_subimage(
            src_img, dest_pix_fmt,
            0, 0, SKRY_get_img_width(src_img), SKRY_get_img_height(src_img));
}

void SKRY_convert_pix_fmt_of_subimage_into(
        const struct SKRY_image *src_img, struct SKRY_image *dest_img,
        int src_x0, int src_y0, int dest_x0, int dest_y0, unsigned width, unsigned height
        )
{
    unsigned src_width = SKRY_get_img_width(src_img);
    unsigned src_height = SKRY_get_img_height(src_img);
    enum SKRY_pixel_format src_pix_fmt = SKRY_get_img_pix_fmt(src_img);
    enum SKRY_pixel_format dest_pix_fmt = SKRY_get_img_pix_fmt(dest_img);

    LOG_MSG(SKRY_LOG_IMAGE, "Converting image %p (%dx%d, %s) to %s "
                        "using fragment of size %dx%d starting at (%d, %d) and writing to image %p at (%d, %d).",
            (void *)src_img, src_width, src_height, pix_fmt_str[src_pix_fmt], pix_fmt_str[dest_pix_fmt],
            width, height, src_x0, src_y0, (void *)dest_img, dest_x0, dest_y0);

    assert(dest_pix_fmt > SKRY_PIX_INVALID
           && dest_pix_fmt < SKRY_NUM_PIX_FORMATS
           && !(dest_pix_fmt == SKRY_PIX_PAL8 && src_pix_fmt != SKRY_PIX_PAL8));

    // Source position cropped so that the source rectangle fits in 'src_img'
    struct SKRY_point src_pos;

    src_pos.x = SKRY_MAX(0, src_x0);
    src_pos.y = SKRY_MAX(0, src_y0);
    width = SKRY_MIN(width, SKRY_get_img_width(src_img) - src_pos.x);
    height = SKRY_MIN(height, SKRY_get_img_height(src_img) - src_pos.y);

    // Destination position based on 'src_pos' and further cropped so that the dest. rectangle fits in 'dest_img'
    struct SKRY_point dest_pos = { .x = dest_x0 + (src_x0 - src_pos.x),
                                   .y = dest_y0 + (src_y0 - src_pos.y) };

    if (dest_pos.x >= (int)SKRY_get_img_width(dest_img) ||
        dest_pos.y >= (int)SKRY_get_img_height(dest_img) ||
        src_pos.x >= (int)SKRY_get_img_width(src_img) ||
        src_pos.y >= (int)SKRY_get_img_height(src_img))
    {
        return;
    }

    dest_pos.x = SKRY_MAX(0, dest_pos.x);
    dest_pos.y = SKRY_MAX(0, dest_pos.y);

    width = SKRY_MIN(width, SKRY_get_img_width(dest_img) - dest_pos.x);
    height = SKRY_MIN(height, SKRY_get_img_height(dest_img) - dest_pos.y);

    // Reflect in the source rectangle any cropping imposed by 'dest_img'
    src_pos.x += dest_pos.x - dest_x0;
    src_pos.y += dest_pos.y - dest_y0;

    struct SKRY_palette src_palette;
    SKRY_get_palette(src_img, &src_palette);

    if (src_pix_fmt == dest_pix_fmt)
    {
        // no conversion required, just copy the data

        for (unsigned y = 0; y < height; y++)
            memcpy((uint8_t *)SKRY_get_line(dest_img, dest_pos.y + y) + dest_pos.x * BYTES_PER_PIXEL[src_pix_fmt],
                   (uint8_t *)SKRY_get_line(src_img, src_pos.y + y) + src_pos.x * BYTES_PER_PIXEL[src_pix_fmt],
                   width * BYTES_PER_PIXEL[src_pix_fmt]);

        return;
    }

    ptrdiff_t in_ptr_step = BYTES_PER_PIXEL[src_pix_fmt],
              out_ptr_step = BYTES_PER_PIXEL[dest_pix_fmt];

    for (unsigned y = 0; y < height; y++)
    {
        uint8_t * restrict in_ptr = (uint8_t *)SKRY_get_line(src_img, y + src_pos.y) + src_pos.x * BYTES_PER_PIXEL[src_pix_fmt];
        uint8_t * restrict out_ptr = (uint8_t *)SKRY_get_line(dest_img, y + dest_pos.y);

        for (unsigned x = 0; x < width; x++)
        {
            if (src_pix_fmt == SKRY_PIX_MONO8)
            {
                uint8_t src = *in_ptr;
                switch (dest_pix_fmt)
                {
                case SKRY_PIX_MONO16: *(uint16_t *)out_ptr = (uint16_t)src << 8; break;
                case SKRY_PIX_MONO32F: *(float *)out_ptr = src * 1.0f/0xFF; break;
                case SKRY_PIX_MONO64F: *(double *)out_ptr = src * 1.0/0xFF; break;
                case SKRY_PIX_RGB32F:
                    ((float *)out_ptr)[0] =
                        ((float *)out_ptr)[1] =
                        ((float *)out_ptr)[2] = src * 1.0f/0xFF;
                    break;
                case SKRY_PIX_RGB64F:
                    ((double *)out_ptr)[0] =
                        ((double *)out_ptr)[1] =
                        ((double *)out_ptr)[2] = src * 1.0/0xFF;
                    break;

                case SKRY_PIX_BGRA8:
                     out_ptr[3] = 0xFF;
                case SKRY_PIX_RGB8:     // intentional fall-through
                    out_ptr[0] = out_ptr[1] = out_ptr[2] = src;
                    break;

                case SKRY_PIX_RGB16:
                    ((uint16_t *)out_ptr)[0] =
                        ((uint16_t *)out_ptr)[1] =
                        ((uint16_t *)out_ptr)[2] = (uint16_t)src << 8;
                    break;
                default:
                    break;
                }
            }
            else if (src_pix_fmt == SKRY_PIX_MONO16)
            {
                uint16_t src = *(uint16_t *)in_ptr;
                switch (dest_pix_fmt)
                {
                case SKRY_PIX_MONO8: *out_ptr = (uint8_t)(src >> 8); break;
                case SKRY_PIX_MONO32F: *(float *)out_ptr = src * 1.0f/0xFFFF; break;
                case SKRY_PIX_RGB32F:
                    ((float *)out_ptr)[0] =
                        ((float *)out_ptr)[1] =
                        ((float *)out_ptr)[2] = src * 1.0f/0xFFFF;
                    break;

                case SKRY_PIX_BGRA8:
                    out_ptr[3] = 0xFF;
                case SKRY_PIX_RGB8:    // intentional fall-through
                    out_ptr[0] = out_ptr[1] = out_ptr[2] = (uint8_t)(src >> 8);
                    break;

                case SKRY_PIX_RGB16:
                    ((uint16_t *)out_ptr)[0] =
                        ((uint16_t *)out_ptr)[1] =
                        ((uint16_t *)out_ptr)[2] = src;
                    break;
                case SKRY_PIX_MONO64F: *(double *)out_ptr = src * 1.0/0xFFFF; break;
                case SKRY_PIX_RGB64F:
                    ((double *)out_ptr)[0] =
                        ((double *)out_ptr)[1] =
                        ((double *)out_ptr)[2] = src * 1.0/0xFFFF;
                    break;
                default:
                    break;
                }
            }
            else if (src_pix_fmt == SKRY_PIX_MONO32F)
            {
                float src = *(float *)in_ptr;
                switch (dest_pix_fmt)
                {
                case SKRY_PIX_MONO8: *out_ptr = (uint8_t)(src * 0xFF); break;
                case SKRY_PIX_MONO16: *(uint16_t *)out_ptr = (uint16_t)(src * 0xFFFF); break;

                case SKRY_PIX_BGRA8:
                    out_ptr[3] = 0xFF;
                case SKRY_PIX_RGB8:    // intentional fall-through
                    out_ptr[0] = out_ptr[1] = out_ptr[2] = (uint8_t)(src * 0xFF);
                    break;

                case SKRY_PIX_RGB16:
                    ((uint16_t *)out_ptr)[0] =
                        ((uint16_t *)out_ptr)[1] =
                        ((uint16_t *)out_ptr)[2] = (uint16_t)(src * 0xFFFF); break;
                case SKRY_PIX_RGB32F:
                    ((float *)out_ptr)[0] =
                        ((float *)out_ptr)[1] =
                        ((float *)out_ptr)[2] = src;
                    break;
                case SKRY_PIX_MONO64F:
                    *(double *)out_ptr = src; break;
                case SKRY_PIX_RGB64F:
                    ((double *)out_ptr)[0] =
                        ((double *)out_ptr)[1] =
                        ((double *)out_ptr)[2] = src;
                    break;
                default:
                    break;
                }
            }
            else if (src_pix_fmt == SKRY_PIX_MONO64F)
            {
                double src = *(double *)in_ptr;
                switch (dest_pix_fmt)
                {
                case SKRY_PIX_MONO8: *out_ptr = (uint8_t)(src * 0xFF); break;
                case SKRY_PIX_MONO16: *(uint16_t *)out_ptr = (uint16_t)(src * 0xFFFF); break;

                case SKRY_PIX_BGRA8:
                    out_ptr[3] = 0xFF;
                case SKRY_PIX_RGB8:   // intentional fall-through
                    out_ptr[0] = out_ptr[1] = out_ptr[2] = (uint8_t)(src * 0xFF);
                    break;

                case SKRY_PIX_RGB16:
                    ((uint16_t *)out_ptr)[0] =
                        ((uint16_t *)out_ptr)[1] =
                        ((uint16_t *)out_ptr)[2] = (uint16_t)(src * 0xFFFF);
                    break;
                case SKRY_PIX_MONO32F:
                    *(float *)out_ptr = src; break;
                case SKRY_PIX_RGB32F:
                    ((float *)out_ptr)[0] =
                        ((float *)out_ptr)[1] =
                        ((float *)out_ptr)[2] = src;
                    break;
                case SKRY_PIX_RGB64F:
                    ((double *)out_ptr)[0] =
                        ((double *)out_ptr)[1] =
                        ((double *)out_ptr)[2] = src;
                    break;
                default:
                    break;
                }
            }
            // When converting from a color format to mono, use sum (scaled) of all channels as the pixel brightness.
            else if (src_pix_fmt == SKRY_PIX_PAL8)
            {
                uint8_t src = *in_ptr;
                switch (dest_pix_fmt)
                {
                case SKRY_PIX_MONO8: *out_ptr = (uint8_t)(((int)src_palette.pal[3*src] + src_palette.pal[3*src+1] + src_palette.pal[3*src+2])/3); break;
                case SKRY_PIX_MONO16: *(uint16_t *)out_ptr = ((uint16_t)src_palette.pal[3*src] + src_palette.pal[3*src+1] + src_palette.pal[3*src+2])/3; break;
                case SKRY_PIX_MONO32F: *(float *)out_ptr = ((int)src_palette.pal[3*src] + src_palette.pal[3*src+1] + src_palette.pal[3*src+2]) * 1.0f/(3*0xFF); break;
                case SKRY_PIX_MONO64F: *(double *)out_ptr = ((int)src_palette.pal[3*src] + src_palette.pal[3*src+1] + src_palette.pal[3*src+2]) * 1.0/(3*0xFF); break;



                case SKRY_PIX_BGRA8:
                    out_ptr[3] = 0xFF;
                case SKRY_PIX_RGB8: // intentional fall-through
                    out_ptr[0] = src_palette.pal[3*src+2];
                    out_ptr[1] = src_palette.pal[3*src+1];
                    out_ptr[2] = src_palette.pal[3*src+0];
                    break;
                case SKRY_PIX_RGB16:
                    ((uint16_t *)out_ptr)[0] = (uint16_t)src_palette.pal[3*src] << 8;
                    ((uint16_t *)out_ptr)[1] = (uint16_t)src_palette.pal[3*src+1] << 8;
                    ((uint16_t *)out_ptr)[2] = (uint16_t)src_palette.pal[3*src+2] << 8;
                    break;
                case SKRY_PIX_RGB32F:
                    ((float *)out_ptr)[0] = (float)src_palette.pal[3*src] / 0xFF;
                    ((float *)out_ptr)[1] = (float)src_palette.pal[3*src+1] / 0xFF;
                    ((float *)out_ptr)[2] = (float)src_palette.pal[3*src+2] / 0xFF;
                    break;
                case SKRY_PIX_RGB64F:
                    ((double *)out_ptr)[0] = (double)src_palette.pal[3*src] / 0xFF;
                    ((double *)out_ptr)[1] = (double)src_palette.pal[3*src+1] / 0xFF;
                    ((double *)out_ptr)[2] = (double)src_palette.pal[3*src+2] / 0xFF;
                    break;
                default:
                    break;
                }
            }
            else if (src_pix_fmt == SKRY_PIX_RGB8)
            {
                switch (dest_pix_fmt)
                {
                case SKRY_PIX_MONO8: *out_ptr = (uint8_t)(((int)in_ptr[0] + in_ptr[1] + in_ptr[2])/3); break;
                case SKRY_PIX_MONO16: *(uint16_t *)out_ptr = ((uint16_t)in_ptr[0] + in_ptr[1] + in_ptr[2])/3 * 0xFF; break;
                case SKRY_PIX_MONO32F: *(float *)out_ptr = ((int)in_ptr[0] + in_ptr[1] + in_ptr[2]) * 1.0f/(3*0xFF); break;
                case SKRY_PIX_BGRA8:
                    out_ptr[0] = in_ptr[2];
                    out_ptr[1] = in_ptr[1];
                    out_ptr[2] = in_ptr[0];
                    out_ptr[3] = 0xFF;
                    break;
                case SKRY_PIX_RGB16:
                    ((uint16_t *)out_ptr)[0] = (uint16_t)in_ptr[0] << 8;
                    ((uint16_t *)out_ptr)[1] = (uint16_t)in_ptr[1] << 8;
                    ((uint16_t *)out_ptr)[2] = (uint16_t)in_ptr[2] << 8;
                    break;
                case SKRY_PIX_RGB32F:
                    ((float *)out_ptr)[0] = in_ptr[0] * 1.0f/0xFF;
                    ((float *)out_ptr)[1] = in_ptr[1] * 1.0f/0xFF;
                    ((float *)out_ptr)[2] = in_ptr[2] * 1.0f/0xFF;
                    break;
                default:
                    break;
                }
            }
            else if (src_pix_fmt == SKRY_PIX_RGB16)
            {
                uint16_t *in_ptr16 = (uint16_t *)in_ptr;
                switch (dest_pix_fmt)
                {
                case SKRY_PIX_MONO8: *out_ptr = (uint8_t)((((int)in_ptr16[0] + in_ptr16[1] + in_ptr16[2])>>8) / 3); break;
                case SKRY_PIX_MONO16: *(uint16_t *)out_ptr = (uint16_t)(((int)in_ptr16[0] + in_ptr16[1] + in_ptr16[2])/3); break;
                case SKRY_PIX_MONO32F: *(float *)out_ptr = ((int)in_ptr16[0] + in_ptr16[1] + in_ptr16[2]) * 1.0f/(3*0xFFFF); break;

                case SKRY_PIX_BGRA8:
                    out_ptr[3] = 0xFF;
                    out_ptr[2] = (uint8_t)(in_ptr16[0] >> 8);
                    out_ptr[1] = (uint8_t)(in_ptr16[1] >> 8);
                    out_ptr[0] = (uint8_t)(in_ptr16[2] >> 8);
                    break;

                case SKRY_PIX_RGB8:
                    out_ptr[0] = (uint8_t)(in_ptr16[0] >> 8);
                    out_ptr[1] = (uint8_t)(in_ptr16[1] >> 8);
                    out_ptr[2] = (uint8_t)(in_ptr16[2] >> 8);
                    break;

                case SKRY_PIX_RGB32F:
                    ((float *)out_ptr)[0] = in_ptr16[0] * 1.0f/0xFFFF;
                    ((float *)out_ptr)[1] = in_ptr16[1] * 1.0f/0xFFFF;
                    ((float *)out_ptr)[2] = in_ptr16[2] * 1.0f/0xFFFF;
                    break;
                default:
                    break;
                }
            }
            else if (src_pix_fmt == SKRY_PIX_RGB32F)
            {
                float *src = (float *)in_ptr;
                switch (dest_pix_fmt)
                {
                case SKRY_PIX_MONO8: *out_ptr = (uint8_t)((src[0] + src[1] + src[2]) * 0xFF/3.0f); break;
                case SKRY_PIX_MONO16: *(uint16_t *)out_ptr = (uint16_t)((src[0] + src[1] + src[2]) * 0xFFFF/3.0f); break;
                case SKRY_PIX_MONO32F: *(float *)out_ptr = (src[0] + src[1] + src[2])/3; break;
                case SKRY_PIX_MONO64F: *(double *)out_ptr = (src[0] + src[1] + src[2])/3; break;

                case SKRY_PIX_BGRA8:
                    out_ptr[3] = 0xFF;
                case SKRY_PIX_RGB8: // intentional fall-through
                    out_ptr[0] = (uint8_t)(src[2] * 0xFF);
                    out_ptr[1] = (uint8_t)(src[1] * 0xFF);
                    out_ptr[2] = (uint8_t)(src[0] * 0xFF);
                    break;

                case SKRY_PIX_RGB16:
                    ((uint16_t *)out_ptr)[0] = (uint16_t)(src[0] * 0xFFFF);
                    ((uint16_t *)out_ptr)[1] = (uint16_t)(src[1] * 0xFFFF);
                    ((uint16_t *)out_ptr)[2] = (uint16_t)(src[2] * 0xFFFF);
                    break;
                case SKRY_PIX_RGB64F:
                    ((double *)out_ptr)[0] = src[0];
                    ((double *)out_ptr)[1] = src[1];
                    ((double *)out_ptr)[2] = src[2];
                    break;
                default:
                    break;
                }
            }
            else if (src_pix_fmt == SKRY_PIX_RGB64F)
            {
                double *src = (double *)in_ptr;
                switch (dest_pix_fmt)
                {
                case SKRY_PIX_MONO8: *out_ptr = (uint8_t)((src[0] + src[1] + src[2]) * 0xFF/3.0f); break;
                case SKRY_PIX_MONO16: *(uint16_t *)out_ptr = (uint16_t)((src[0] + src[1] + src[2]) * 0xFFFF/3.0f); break;
                case SKRY_PIX_MONO32F: *(float *)out_ptr = (src[0] + src[1] + src[2])/3; break;
                case SKRY_PIX_MONO64F: *(double *)out_ptr = (src[0] + src[1] + src[2])/3; break;
                case SKRY_PIX_BGRA8:
                    out_ptr[3] = 0xFF;
                case SKRY_PIX_RGB8: // intentional fall-through
                    out_ptr[0] = (uint8_t)(src[2] * 0xFF);
                    out_ptr[1] = (uint8_t)(src[1] * 0xFF);
                    out_ptr[2] = (uint8_t)(src[0] * 0xFF);
                    break;
                case SKRY_PIX_RGB16:
                    ((uint16_t *)out_ptr)[0] = (uint16_t)(src[0] * 0xFFFF);
                    ((uint16_t *)out_ptr)[1] = (uint16_t)(src[1] * 0xFFFF);
                    ((uint16_t *)out_ptr)[2] = (uint16_t)(src[2] * 0xFFFF);
                    break;
                case SKRY_PIX_RGB32F:
                    ((float *)out_ptr)[0] = src[0];
                    ((float *)out_ptr)[1] = src[1];
                    ((float *)out_ptr)[2] = src[2];
                    break;
                default:
                    break;
                }
            }

            in_ptr += in_ptr_step;
            out_ptr += out_ptr_step;
        }
    }
}

struct SKRY_image *SKRY_convert_pix_fmt_of_subimage(
        const struct SKRY_image *src_img, enum SKRY_pixel_format dest_pix_fmt,
        int x0, int y0, unsigned width, unsigned height
        )
{
    struct SKRY_palette src_palette;
    SKRY_get_palette(src_img, &src_palette);

    struct SKRY_image *dest_img = SKRY_new_image(width, height, dest_pix_fmt, &src_palette, 0);

    SKRY_convert_pix_fmt_of_subimage_into(src_img, dest_img, x0, y0, 0, 0, width, height);

    return dest_img;
}

enum SKRY_result SKRY_get_palette(const struct SKRY_image *img, struct SKRY_palette *pal)
{
    return img->get_palette(img, pal);
}

struct SKRY_image *SKRY_get_img_copy(const struct SKRY_image *img)
{
    if (!img)
        return 0;

    unsigned width = SKRY_get_img_width(img),
             height = SKRY_get_img_height(img);
    enum SKRY_pixel_format pix_fmt = SKRY_get_img_pix_fmt(img);

    struct SKRY_palette palette;
    SKRY_get_palette(img, &palette);

    struct SKRY_image *img_copy = SKRY_new_image(width, height, pix_fmt, &palette, 0);
    if (!img_copy)
        return 0;

    for (unsigned line = 0; line < height; line++)
        memcpy(SKRY_get_line(img_copy, line), SKRY_get_line(img, line), width * BYTES_PER_PIXEL[pix_fmt]);

    LOG_MSG(SKRY_LOG_IMAGE, "Returning copy %p of image %p.", (void *)img_copy, (void *)img);
    return img_copy;
}

struct SKRY_image *SKRY_load_image(const char *file_name,
                                   enum SKRY_result *result ///< If not null, receives operation result
                                  )
{
    if (compare_extension(file_name, "bmp"))
        return load_BMP(file_name, result);
    else if (compare_extension(file_name, "tif") || compare_extension(file_name, "tiff"))
        return load_TIFF(file_name, result);
    else
    {
        if (result) *result = SKRY_UNSUPPORTED_FILE_FORMAT;
        return 0;
    }
}

enum SKRY_result SKRY_get_image_metadata(const char *file_name,
                                         unsigned *width,
                                         unsigned *height,
                                         enum SKRY_pixel_format *pix_fmt
                                        )
{
    if (compare_extension(file_name, "bmp"))
        return get_BMP_metadata(file_name, width, height, pix_fmt);
    else if (compare_extension(file_name, "tif") || compare_extension(file_name, "tiff"))
        return get_TIFF_metadata(file_name, width, height, pix_fmt);
    else
        return SKRY_UNSUPPORTED_FILE_FORMAT;
}

enum SKRY_result SKRY_save_image(const struct SKRY_image *img, const char *file_name,
                                 enum SKRY_output_format output_fmt)
{
    if (SKRY_BMP_8 == output_fmt)
        return save_BMP(img, file_name);
    else if (SKRY_TIFF_16 == output_fmt)
        return save_TIFF(img, file_name);
    else
        return SKRY_UNSUPPORTED_FILE_FORMAT;
}

struct SKRY_rect SKRY_get_img_rect(const struct SKRY_image *img)
{
    return (struct SKRY_rect) { .x = 0, .y = 0,
                                .width = img->get_width(img),
                                .height = img->get_height(img) };
}

const unsigned *SKRY_get_supported_output_formats(
                    size_t *num_formats)
{
    *num_formats = sizeof(SUPPORTED_OUTP_FMT)/sizeof(*SUPPORTED_OUTP_FMT);
    return SUPPORTED_OUTP_FMT;
}

size_t SKRY_get_img_byte_count(const struct SKRY_image *img)
{
    ptrdiff_t lstride = SKRY_get_line_stride_in_bytes(img);
    size_t bytes_per_line = (lstride > 0 ? lstride : -lstride);
    return sizeof(*img) + SKRY_get_img_height(img) * bytes_per_line;
}
