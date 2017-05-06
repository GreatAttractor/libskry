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
#include "../utils/demosaic.h"
#include "../utils/logging.h"
#include "../utils/misc.h"


const size_t BYTES_PER_PIXEL[SKRY_NUM_PIX_FORMATS] =
{
    [SKRY_PIX_INVALID] = 0,  // unused

    [SKRY_PIX_PAL8]    = 1,
    [SKRY_PIX_MONO8]   = 1,
    [SKRY_PIX_RGB8]    = 3,
    [SKRY_PIX_BGRA8]   = 4,

    [SKRY_PIX_CFA_RGGB8] = 1,
    [SKRY_PIX_CFA_GRBG8] = 1,
    [SKRY_PIX_CFA_GBRG8] = 1,
    [SKRY_PIX_CFA_BGGR8] = 1,

    [SKRY_PIX_CFA_RGGB16] = 2,
    [SKRY_PIX_CFA_GRBG16] = 2,
    [SKRY_PIX_CFA_GBRG16] = 2,
    [SKRY_PIX_CFA_BGGR16] = 2,

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
    [SKRY_PIX_RGB64F]  = 3,

    [SKRY_PIX_CFA_RGGB8] = 1,
    [SKRY_PIX_CFA_GRBG8] = 1,
    [SKRY_PIX_CFA_GBRG8] = 1,
    [SKRY_PIX_CFA_BGGR8] = 1,

    [SKRY_PIX_CFA_RGGB16] = 1,
    [SKRY_PIX_CFA_GRBG16] = 1,
    [SKRY_PIX_CFA_GBRG16] = 1,
    [SKRY_PIX_CFA_BGGR16] = 1,
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
    [SKRY_PIX_RGB64F]  = 64,

    [SKRY_PIX_CFA_RGGB8] = 8,
    [SKRY_PIX_CFA_GRBG8] = 8,
    [SKRY_PIX_CFA_GBRG8] = 8,
    [SKRY_PIX_CFA_BGGR8] = 8,

    [SKRY_PIX_CFA_RGGB16] = 16,
    [SKRY_PIX_CFA_GRBG16] = 16,
    [SKRY_PIX_CFA_GBRG16] = 16,
    [SKRY_PIX_CFA_BGGR16] = 16,
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

const char *SKRY_CFA_pattern_str[SKRY_NUM_CFA_PATTERNS] =
{
    [SKRY_CFA_RGGB] = "RGGB",
    [SKRY_CFA_BGGR] = "BGGR",
    [SKRY_CFA_GRBG] = "GRBG",
    [SKRY_CFA_GBRG] = "GBRG",
    [SKRY_CFA_NONE] = "(none)",
};

const enum SKRY_CFA_pattern SKRY_PIX_CFA_PATTERN[SKRY_NUM_PIX_FORMATS] =
{
    [SKRY_PIX_CFA_RGGB8] = SKRY_CFA_RGGB,
    [SKRY_PIX_CFA_GRBG8] = SKRY_CFA_GRBG,
    [SKRY_PIX_CFA_GBRG8] = SKRY_CFA_GBRG,
    [SKRY_PIX_CFA_BGGR8] = SKRY_CFA_BGGR,

    [SKRY_PIX_CFA_RGGB16] = SKRY_CFA_RGGB,
    [SKRY_PIX_CFA_GRBG16] = SKRY_CFA_GRBG,
    [SKRY_PIX_CFA_GBRG16] = SKRY_CFA_GBRG,
    [SKRY_PIX_CFA_BGGR16] = SKRY_CFA_BGGR
};

// ------------ Implementation of the internal image class ---------------------

static
SKRY_Image *free_internal_img(SKRY_Image *img)
{
    LOG_MSG(SKRY_LOG_IMAGE, "Freeing image pixels array at %p.", IMG_DATA(img)->pixels);
    free(IMG_DATA(img)->pixels);
    free(IMG_DATA(img));
    free(img);
    return 0;
}

static
unsigned get_internal_img_width(const SKRY_Image *img)
{
    return IMG_DATA(img)->width;
}

static
unsigned get_internal_img_height(const SKRY_Image *img)
{
    return IMG_DATA(img)->height;
}

static
ptrdiff_t get_internal_line_stride_in_bytes(const SKRY_Image *img)
{
    return (ptrdiff_t)(BYTES_PER_PIXEL[img->pix_fmt] * IMG_DATA(img)->width);
}

static
size_t get_internal_img_bytes_per_pixel(const SKRY_Image *img)
{
    return BYTES_PER_PIXEL[img->pix_fmt];
}

static
void *get_internal_img_line(const SKRY_Image *img, size_t line)
{
    return (char *)IMG_DATA(img)->pixels + line * get_internal_line_stride_in_bytes(img);
}

/// Fills 'pal'; returns SKRY_NO_PALETTE if image does not contain a palette
static
enum SKRY_result get_internal_img_palette (const SKRY_Image *img, struct SKRY_palette *pal)
{
    if (img->pix_fmt != SKRY_PIX_PAL8)
        return SKRY_NO_PALETTE;
    else
    {
        memcpy(pal, &IMG_DATA(img)->palette, sizeof(*pal));
    }
    return SKRY_SUCCESS;
}

/// Allocates and initializes an empty internal image structure; returns null if out of memory
SKRY_Image *create_internal_img(void)
{
    SKRY_Image *img = malloc(sizeof(*img));
    if (!img)
        return 0;

    memset(img, 0, sizeof(*img));

    img->data = malloc(sizeof(struct internal_img_data));
    if (!img->data)
    {
        free(img);
        return 0;
    }

    img->pix_fmt = SKRY_PIX_INVALID;
    IMG_DATA(img)->width = IMG_DATA(img)->height = 0;
    IMG_DATA(img)->pixels = 0;

    img->free                     = free_internal_img;
    img->get_width                = get_internal_img_width;
    img->get_height               = get_internal_img_height;
    img->get_line_stride_in_bytes = get_internal_line_stride_in_bytes;
    img->get_bytes_per_pixel      = get_internal_img_bytes_per_pixel;
    img->get_line                 = get_internal_img_line;
    img->get_palette              = get_internal_img_palette;

    return img;
}

// -------------- Public interface implementation ------------------------------

/// Returns null
SKRY_Image *SKRY_free_image(SKRY_Image *img)
{
    if (img)
    {
        LOG_MSG(SKRY_LOG_IMAGE, "Freeing image %p.", (void *)img);
        img->free(img);
    }
    return 0;
}

unsigned SKRY_get_img_width(const SKRY_Image *img)
{
    return img->get_width(img);
}

unsigned SKRY_get_img_height(const SKRY_Image *img)
{
    return img->get_height(img);
}

/// Result may be negative (means lines are stored bottom-to-top)
ptrdiff_t SKRY_get_line_stride_in_bytes(const SKRY_Image *img)
{
    return img->get_line_stride_in_bytes(img);
}

size_t SKRY_get_bytes_per_pixel(const SKRY_Image *img)
{
    return img->get_bytes_per_pixel(img);
}

/// Returns pointer to start of the specified line
void *SKRY_get_line(const SKRY_Image *img, size_t line)
{
    return img->get_line(img, line);
}

enum SKRY_pixel_format SKRY_get_img_pix_fmt(const SKRY_Image *img)
{
    return img->pix_fmt;
}

/** Copies (with cropping or padding) a fragment of image to another. There is no scaling.
    Pixel formats of source and destination must be the same. 'src_img' must not equal 'dest_img'.
    NOTE: care must be taken if pixel format is one of SKRY_CFA. The caller may need to adjust
    the CFA_pattern if source and destination X, Y offets are not simultaneously odd/even. */
void SKRY_resize_and_translate(
    const SKRY_Image *src_img,
    SKRY_Image *dest_img,
    int src_x_min,   ///< X min of input data in source image
    int src_y_min,   ///< Y min of input data in source image
    unsigned width,  ///< width of fragment to copy
    unsigned height, ///< height of fragment to copy
    int dest_x_ofs,  ///< X offset of input data in destination image
    int dest_y_ofs,  ///< Y offset of input data in destination image
    int clear_to_zero  ///< if 1, 'dest_img' areas not copied on will be cleared to zero
)
{
    assert(SKRY_get_img_pix_fmt(src_img) == SKRY_get_img_pix_fmt(dest_img));

    const unsigned src_w = SKRY_get_img_width(src_img),
                   src_h = SKRY_get_img_height(src_img),
                   dest_w = SKRY_get_img_width(dest_img),
                   dest_h = SKRY_get_img_height(dest_img);

    size_t bytes_per_pixel = BYTES_PER_PIXEL[SKRY_get_img_pix_fmt(src_img)];

    // Start and end (inclusive) coordinates to fill in the output image
    int dest_x_start = dest_x_ofs;
    int dest_x_end = dest_x_start + width - 1;

    int dest_y_start = dest_y_ofs;
    int dest_y_end = dest_y_start + height - 1;

    // Actual source coordinates to use
    int src_x_start = src_x_min;
    int src_y_start = src_y_min;

    // Perform any necessary cropping

    // Source image, left and top
    if (src_x_min < 0)
    {
        src_x_start -= src_x_min;
        dest_x_start -= src_x_min;
    }
    if (src_y_min < 0)
    {
        src_y_start -= src_y_min;
        dest_y_start -= src_y_min;
    }

    // Source image, right and bottom
    if (src_x_min + width > src_w)
        dest_x_end -= src_x_min + width - src_w;

    if (src_y_min + height > src_h)
        dest_y_end -= src_y_min + height - src_h;

    // Destination image, left and top
    if (dest_x_start < 0)
    {
        src_x_start -= dest_x_start;
        dest_x_start = 0;
    }
    if (dest_y_start < 0)
    {
        src_y_start -= dest_y_start;
        dest_y_start = 0;
    }

    // Destination image, right and bottom
    if (dest_x_end >= (int)dest_w)
        dest_x_end = dest_w - 1;
    if (dest_y_end >= (int)dest_h)
        dest_y_end = dest_h - 1;


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
               (uint8_t *)SKRY_get_line(src_img, y - dest_y_start + src_y_start) + src_x_start * bytes_per_pixel,
               (dest_x_end - dest_x_start + 1) * bytes_per_pixel);

    }
}

/// Allocates a new image (with lines stored top-to-bottom, no padding)
SKRY_Image *SKRY_new_image(
    unsigned width, unsigned height, enum SKRY_pixel_format pixel_format,
    /// Can be null; if not null, used only if 'pixel_format' is PIX_PAL8
    const struct SKRY_palette *palette,
    int zero_fill)
{
    assert(width > 0);
    assert(height > 0);
    assert(pixel_format > SKRY_PIX_INVALID && pixel_format < SKRY_NUM_PIX_FORMATS);

    SKRY_Image *img = create_internal_img();
    if (!img)
        return 0;

    IMG_DATA(img)->width = width;
    IMG_DATA(img)->height = height;
    img->pix_fmt = pixel_format;
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

/// Returned image has lines stored top-to-bottom, no padding
SKRY_Image *SKRY_convert_pix_fmt(const SKRY_Image *src_img,
                                 enum SKRY_pixel_format dest_pix_fmt,
                                 /// Used if 'img' contains raw color data
                                 enum SKRY_demosaic_method demosaic_method)
{
    return SKRY_convert_pix_fmt_of_subimage(
            src_img, dest_pix_fmt,
            0, 0, SKRY_get_img_width(src_img), SKRY_get_img_height(src_img), demosaic_method);
}

/// Converts a fragment of 'src_img' to 'dest_img's pixel format and writes it into 'dest_img'
/** Cropping is performed if necessary. If 'src_img' is in raw color format, the CFA pattern
    will be appropriately adjusted depending on 'x0', 'y0'. */
void SKRY_convert_pix_fmt_of_subimage_into(
        const SKRY_Image *src_img,
        SKRY_Image       *dest_img,
        int src_x0, int src_y0,
        int dest_x0, int dest_y0,
        unsigned width, unsigned height,
        /// Used if 'img' contains raw color data
        enum SKRY_demosaic_method demosaic_method)
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
           && !(dest_pix_fmt == SKRY_PIX_PAL8 && src_pix_fmt != SKRY_PIX_PAL8)
           && (dest_pix_fmt < SKRY_PIX_CFA_MIN || dest_pix_fmt > SKRY_PIX_CFA_MAX));

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

    if (src_pix_fmt > SKRY_PIX_CFA_MIN && src_pix_fmt < SKRY_PIX_CFA_MAX)
    {
        enum SKRY_CFA_pattern tpattern = translate_CFA_pattern(SKRY_PIX_CFA_PATTERN[src_pix_fmt], src_pos.x & 1, src_pos.y & 1);
        switch (tpattern)
        {
            case SKRY_CFA_BGGR:
                src_pix_fmt = (BITS_PER_CHANNEL[src_pix_fmt] == 8 ?
                               SKRY_PIX_CFA_BGGR8 :
                               SKRY_PIX_CFA_BGGR16);
                break;

            case SKRY_CFA_GBRG:
                src_pix_fmt = (BITS_PER_CHANNEL[src_pix_fmt] == 8 ?
                               SKRY_PIX_CFA_GBRG8 :
                               SKRY_PIX_CFA_GBRG16);
                break;

            case SKRY_CFA_GRBG:
                src_pix_fmt = (BITS_PER_CHANNEL[src_pix_fmt] == 8 ?
                               SKRY_PIX_CFA_GRBG8 :
                               SKRY_PIX_CFA_GRBG16);
                break;

            case SKRY_CFA_RGGB:
                src_pix_fmt = (BITS_PER_CHANNEL[src_pix_fmt] == 8 ?
                               SKRY_PIX_CFA_RGGB8 :
                               SKRY_PIX_CFA_RGGB16);
                break;

            default:
                break;
        }

        if (BITS_PER_CHANNEL[src_pix_fmt] == 8
            && dest_pix_fmt == SKRY_PIX_MONO8)
        {
            demosaic_8_as_mono8(
                (uint8_t *)SKRY_get_line(src_img, src_pos.y) + src_pos.x,
                width, height, SKRY_get_line_stride_in_bytes(src_img),
                (uint8_t *)SKRY_get_line(dest_img, dest_pos.y) + dest_pos.x,
                SKRY_get_line_stride_in_bytes(dest_img),
                SKRY_PIX_CFA_PATTERN[src_pix_fmt],
                demosaic_method);
        }
        else if (BITS_PER_CHANNEL[src_pix_fmt] == 8
                 && dest_pix_fmt == SKRY_PIX_RGB8)
        {
            demosaic_8_as_RGB(
                (uint8_t *)SKRY_get_line(src_img, src_pos.y) + src_pos.x,
                width, height, SKRY_get_line_stride_in_bytes(src_img),
                (uint8_t *)SKRY_get_line(dest_img, dest_pos.y) + 3*dest_pos.x,
                SKRY_get_line_stride_in_bytes(dest_img),
                SKRY_PIX_CFA_PATTERN[src_pix_fmt],
                demosaic_method);
        }
        else if (BITS_PER_CHANNEL[src_pix_fmt] == 16
            && dest_pix_fmt == SKRY_PIX_MONO8)
        {
            demosaic_16_as_mono8(
                (uint16_t *)SKRY_get_line(src_img, src_pos.y) + src_pos.x,
                width, height, SKRY_get_line_stride_in_bytes(src_img),
                (uint8_t *)SKRY_get_line(dest_img, dest_pos.y) + dest_pos.x,
                SKRY_get_line_stride_in_bytes(dest_img),
                SKRY_PIX_CFA_PATTERN[src_pix_fmt],
                demosaic_method);
        }
        else if (BITS_PER_CHANNEL[src_pix_fmt] == 16
                 && dest_pix_fmt == SKRY_PIX_RGB16)
        {
            demosaic_16_as_RGB(
                (uint16_t *)SKRY_get_line(src_img, src_pos.y) + src_pos.x,
                width, height, SKRY_get_line_stride_in_bytes(src_img),
                (uint16_t *)SKRY_get_line(dest_img, dest_pos.y) + 3*dest_pos.x,
                SKRY_get_line_stride_in_bytes(dest_img),
                SKRY_PIX_CFA_PATTERN[src_pix_fmt],
                demosaic_method);
        }
        else
        {
            // Cannot demosaic directly into 'dest_img'. First demosaic to RGB8/RGB16,
            // then convert to destination format.

            SKRY_Image *demosaiced = 0;
            if (BITS_PER_CHANNEL[src_pix_fmt] == 8)
            {
                demosaiced = SKRY_new_image(width, height, SKRY_PIX_RGB8, 0, 0);
                demosaic_8_as_RGB(
                  (uint8_t *)SKRY_get_line(src_img, src_pos.y) + src_pos.x,
                  width, height, SKRY_get_line_stride_in_bytes(src_img),
                  SKRY_get_line(demosaiced, 0), SKRY_get_line_stride_in_bytes(demosaiced),
                  SKRY_PIX_CFA_PATTERN[src_pix_fmt], demosaic_method);
            }
            else if (BITS_PER_CHANNEL[src_pix_fmt] == 16)
            {
                demosaiced = SKRY_new_image(width, height, SKRY_PIX_RGB16, 0, 0);
                demosaic_16_as_RGB(
                  (uint16_t *)SKRY_get_line(src_img, src_pos.y) + src_pos.x,
                  width, height, SKRY_get_line_stride_in_bytes(src_img),
                  SKRY_get_line(demosaiced, 0), SKRY_get_line_stride_in_bytes(demosaiced),
                  SKRY_PIX_CFA_PATTERN[src_pix_fmt], demosaic_method);
            }

            SKRY_convert_pix_fmt_of_subimage_into(
                demosaiced, dest_img, 0, 0,
                dest_pos.x, dest_pos.y, width, height,
                demosaic_method);

            SKRY_free_image(demosaiced);
        }

        return;
    }

    ptrdiff_t in_ptr_step = BYTES_PER_PIXEL[src_pix_fmt],
              out_ptr_step = BYTES_PER_PIXEL[dest_pix_fmt];

    for (unsigned y = 0; y < height; y++)
    {
        uint8_t * restrict in_ptr = (uint8_t *)SKRY_get_line(src_img, y + src_pos.y) + src_pos.x * BYTES_PER_PIXEL[src_pix_fmt];
        uint8_t * restrict out_ptr = (uint8_t *)SKRY_get_line(dest_img, y + dest_pos.y) + dest_pos.x * BYTES_PER_PIXEL[dest_pix_fmt];

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
                    out_ptr[0] = src_palette.pal[3*src+2];
                    out_ptr[1] = src_palette.pal[3*src+1];
                    out_ptr[2] = src_palette.pal[3*src+0];
                    break;
                case SKRY_PIX_RGB8:
                    out_ptr[0] = src_palette.pal[3*src+0];
                    out_ptr[1] = src_palette.pal[3*src+1];
                    out_ptr[2] = src_palette.pal[3*src+2];
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
                    out_ptr[0] = (uint8_t)(src[2] * 0xFF);
                    out_ptr[1] = (uint8_t)(src[1] * 0xFF);
                    out_ptr[2] = (uint8_t)(src[0] * 0xFF);
                    break;

                case SKRY_PIX_RGB8:
                    out_ptr[0] = (uint8_t)(src[0] * 0xFF);
                    out_ptr[1] = (uint8_t)(src[1] * 0xFF);
                    out_ptr[2] = (uint8_t)(src[2] * 0xFF);
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
                    out_ptr[0] = (uint8_t)(src[2] * 0xFF);
                    out_ptr[1] = (uint8_t)(src[1] * 0xFF);
                    out_ptr[2] = (uint8_t)(src[0] * 0xFF);
                    break;

                case SKRY_PIX_RGB8:
                    out_ptr[0] = (uint8_t)(src[0] * 0xFF);
                    out_ptr[1] = (uint8_t)(src[1] * 0xFF);
                    out_ptr[2] = (uint8_t)(src[2] * 0xFF);
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

/// Returned image has lines stored top-to-bottom, no padding
/** If 'src_img' is in raw color format, the CFA pattern will be
    appropriately adjusted depending on 'x0', 'y0'. */
SKRY_Image *SKRY_convert_pix_fmt_of_subimage(
        const SKRY_Image *src_img, enum SKRY_pixel_format dest_pix_fmt,
        int x0, int y0, unsigned width, unsigned height,
        /// Used if 'img' contains raw color data
        enum SKRY_demosaic_method demosaic_method)
{
    struct SKRY_palette src_palette;
    SKRY_get_palette(src_img, &src_palette);

    SKRY_Image *dest_img = SKRY_new_image(width, height, dest_pix_fmt, &src_palette, 0);
    if (dest_img)
        SKRY_convert_pix_fmt_of_subimage_into(src_img, dest_img, x0, y0, 0, 0, width, height,
                                              demosaic_method);

    return dest_img;
}

/// Fills 'pal'; returns SKRY_SUCCESS or SKRY_NO_PALETTE if image does not contain a palette
enum SKRY_result SKRY_get_palette(const SKRY_Image *img, struct SKRY_palette *pal)
{
    return img->get_palette(img, pal);
}

/// Returned image has lines stored top-to-bottom, no padding
SKRY_Image *SKRY_get_img_copy(const SKRY_Image *img)
{
    if (!img)
        return 0;

    unsigned width = SKRY_get_img_width(img),
             height = SKRY_get_img_height(img);
    enum SKRY_pixel_format pix_fmt = SKRY_get_img_pix_fmt(img);

    struct SKRY_palette palette;
    SKRY_get_palette(img, &palette);

    SKRY_Image *img_copy = SKRY_new_image(width, height, pix_fmt, &palette, 0);
    if (!img_copy)
        return 0;

    for (unsigned line = 0; line < height; line++)
        memcpy(SKRY_get_line(img_copy, line), SKRY_get_line(img, line), width * BYTES_PER_PIXEL[pix_fmt]);

    LOG_MSG(SKRY_LOG_IMAGE, "Returning copy %p of image %p.", (void *)img_copy, (void *)img);
    return img_copy;
}

/// Returns null on error
SKRY_Image *SKRY_load_image(const char *file_name,
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

/// Returns metadata without reading the pixel data
enum SKRY_result SKRY_get_image_metadata(const char *file_name,
                                         unsigned *width,  ///< If not null, receives image width
                                         unsigned *height, ///< If not null, receives image height
                                         enum SKRY_pixel_format *pix_fmt ///< If not null, receives pixel format
)
{
    if (compare_extension(file_name, "bmp"))
        return get_BMP_metadata(file_name, width, height, pix_fmt);
    else if (compare_extension(file_name, "tif") || compare_extension(file_name, "tiff"))
        return get_TIFF_metadata(file_name, width, height, pix_fmt);
    else
        return SKRY_UNSUPPORTED_FILE_FORMAT;
}

enum SKRY_result SKRY_save_image(const SKRY_Image *img, const char *file_name,
                                 enum SKRY_output_format output_fmt)
{
    if (SKRY_BMP_8 == output_fmt)
        return save_BMP(img, file_name);
    else if (SKRY_TIFF_16 == output_fmt)
        return save_TIFF(img, file_name);
    else
        return SKRY_UNSUPPORTED_FILE_FORMAT;
}

/// Returns a rectangle at (0, 0) and the same size as the image
struct SKRY_rect SKRY_get_img_rect(const SKRY_Image *img)
{
    return (struct SKRY_rect) { .x = 0, .y = 0,
                                .width = img->get_width(img),
                                .height = img->get_height(img) };
}

const unsigned *SKRY_get_supported_output_formats(
                    size_t *num_formats ///< Receives number of elements in returned array
)
{
    *num_formats = sizeof(SUPPORTED_OUTP_FMT)/sizeof(*SUPPORTED_OUTP_FMT);
    return SUPPORTED_OUTP_FMT;
}

/// Returns number of bytes occupied by the image
/** The value may not encompass some of image's metadata (ca. tens to hundreds of bytes). */
size_t SKRY_get_img_byte_count(const SKRY_Image *img)
{
    ptrdiff_t lstride = SKRY_get_line_stride_in_bytes(img);
    size_t bytes_per_line = (lstride > 0 ? lstride : -lstride);
    return sizeof(*img) + SKRY_get_img_height(img) * bytes_per_line;
}

/// Treat the image as containing raw color data (pixel format will be updated)
/** Can be used only if the image is 8- or 16-bit mono or raw color.
    Only pixel format is updated, the pixel data is unchanged. To demosaic,
    call this function and then use one of the pixel format conversion functions. */
void SKRY_reinterpret_as_CFA(SKRY_Image *img, enum SKRY_CFA_pattern CFA_pattern)
{
    if (NUM_CHANNELS[img->pix_fmt] == 1)
    {
        if (BITS_PER_CHANNEL[img->pix_fmt] == 8)
            switch (CFA_pattern)
            {
                case SKRY_CFA_BGGR: img->pix_fmt = SKRY_PIX_CFA_BGGR8; break;
                case SKRY_CFA_GBRG: img->pix_fmt = SKRY_PIX_CFA_GBRG8; break;
                case SKRY_CFA_GRBG: img->pix_fmt = SKRY_PIX_CFA_GRBG8; break;
                case SKRY_CFA_RGGB: img->pix_fmt = SKRY_PIX_CFA_RGGB8; break;
                default: break;
            }
        else if (BITS_PER_CHANNEL[img->pix_fmt] == 16)
            switch (CFA_pattern)
            {
                case SKRY_CFA_BGGR: img->pix_fmt = SKRY_PIX_CFA_BGGR16; break;
                case SKRY_CFA_GBRG: img->pix_fmt = SKRY_PIX_CFA_GBRG16; break;
                case SKRY_CFA_GRBG: img->pix_fmt = SKRY_PIX_CFA_GRBG16; break;
                case SKRY_CFA_RGGB: img->pix_fmt = SKRY_PIX_CFA_RGGB16; break;
                default: break;
            }
    }
}

/// Finds the centroid of the specified image fragment
/** Returned coords are relative to 'img_fragment'. */
struct SKRY_point SKRY_get_centroid(const SKRY_Image *img,
                                    const struct SKRY_rect img_fragment)
{
    double M00 = 0.0; // image moment 00, i.e. sum of pixels' brightness
    double M10 = 0.0; // image moment 10
    double M01 = 0.0; // image moment 01

    struct SKRY_palette palette;
    SKRY_get_palette(img, &palette);

    enum SKRY_pixel_format pix_fmt = SKRY_get_img_pix_fmt(img);
    size_t num_channels = NUM_CHANNELS[pix_fmt];

    for (unsigned y = img_fragment.y; y < img_fragment.y + img_fragment.height; y++)
    {
        void *line = SKRY_get_line(img, y);
        for (unsigned x = img_fragment.x; x < img_fragment.x + img_fragment.width; x++)
        {
            double current_brightness = 0.0;

            switch (pix_fmt)
            {
            case SKRY_PIX_PAL8:
                {
                    uint8_t pix_val = ((uint8_t *)line)[x];
                    current_brightness =
                           palette.pal[3*pix_val    ] +
                           palette.pal[3*pix_val + 1] +
                           palette.pal[3*pix_val + 2];
                    break;
                }

            case SKRY_PIX_MONO8:
            case SKRY_PIX_RGB8:
            case SKRY_PIX_BGRA8:
            case SKRY_PIX_CFA_RGGB8:
            case SKRY_PIX_CFA_GRBG8:
            case SKRY_PIX_CFA_GBRG8:
            case SKRY_PIX_CFA_BGGR8:
                for (size_t i = 0; i < num_channels; i++)
                {
                    current_brightness += ((uint8_t *)line)[num_channels*x + i];
                }
                break;

            case SKRY_PIX_MONO16:
            case SKRY_PIX_RGB16:
            case SKRY_PIX_RGBA16:
            case SKRY_PIX_CFA_RGGB16:
            case SKRY_PIX_CFA_GRBG16:
            case SKRY_PIX_CFA_GBRG16:
            case SKRY_PIX_CFA_BGGR16:
                for (size_t i = 0; i < num_channels; i++)
                {
                    current_brightness += ((uint16_t *)line)[num_channels*x + i];
                }
                break;

            case SKRY_PIX_MONO32F:
            case SKRY_PIX_RGB32F:
                for (size_t i = 0; i < num_channels; i++)
                {
                    current_brightness += ((float *)line)[num_channels*x + i];
                }
                break;

            case SKRY_PIX_MONO64F:
            case SKRY_PIX_RGB64F:
                for (size_t i = 0; i < num_channels; i++)
                {
                    current_brightness += ((double *)line)[num_channels*x + i];
                }
                break;

            default: break;
            }

            M00 += current_brightness;
            M10 += (x - img_fragment.x) * current_brightness;
            M01 += (y - img_fragment.y) * current_brightness;
        }
    }

    if (M00 == 0.0)
        return (struct SKRY_point) { .x = img_fragment.width/2, .y = img_fragment.height/2 };
    else
        return (struct SKRY_point) { .x = M10/M00, .y = M01/M00 };
}
