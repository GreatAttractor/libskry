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
    Image-handling functions header.
*/

#ifndef LIB_STACKISTRY_IMAGE_HEADER
#define LIB_STACKISTRY_IMAGE_HEADER

#include <stddef.h>
#include <stdint.h>

#include "defs.h"


typedef struct SKRY_image SKRY_Image;

#define SKRY_PALETTE_NUM_ENTRIES 256

struct SKRY_palette
{
    uint8_t pal[3 * SKRY_PALETTE_NUM_ENTRIES];
};

/// Elements correspond with 'SKRY_pixel_format'
extern const size_t BYTES_PER_PIXEL[SKRY_NUM_PIX_FORMATS];

/// Elements correspond with 'SKRY_pixel_format'
extern const size_t NUM_CHANNELS[SKRY_NUM_PIX_FORMATS];

/// Elements correspond with 'SKRY_pixel_format'
extern const size_t BITS_PER_CHANNEL[SKRY_NUM_PIX_FORMATS];

/// Elements correspond with 'SKRY_output_format'
extern const size_t OUTPUT_FMT_BITS_PER_CHANNEL[SKRY_OUTP_FMT_LAST];

SKRY_Image *SKRY_free_image(SKRY_Image *img); /// Returns null

/// Allocates a new image (with lines stored top-to-bottom, no padding)
SKRY_Image *SKRY_new_image(
    unsigned width, unsigned height, enum SKRY_pixel_format,
    /// Can be null; if not null, used only if 'pixel_format' is PIX_PAL8
    const struct SKRY_palette *palette,
    int zero_fill);

unsigned SKRY_get_img_width(const SKRY_Image *img);

unsigned SKRY_get_img_height(const SKRY_Image *img);

/// Result may be negative (means lines are stored bottom-to-top)
ptrdiff_t SKRY_get_line_stride_in_bytes(const SKRY_Image *img);

size_t SKRY_get_bytes_per_pixel(const SKRY_Image *img);

/// Returns pointer to start of the specified line
void *SKRY_get_line(const SKRY_Image *img, size_t line);

enum SKRY_pixel_format SKRY_get_img_pix_fmt(const SKRY_Image *img);

/// Fills 'pal'; returns SKRY_SUCCESS or SKRY_NO_PALETTE if image does not contain a palette
enum SKRY_result SKRY_get_palette(const SKRY_Image *img, struct SKRY_palette *pal);

/// Returned image has lines stored top-to-bottom, no padding
SKRY_Image *SKRY_get_img_copy(const SKRY_Image *img);

/** \brief Copies (with cropping or padding) a fragment of image to another. There is no scaling.
           Pixel formats of source and destination must be the same. 'src_img' must not equal 'dest_img'. */
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
);

/// Returned image has lines stored top-to-bottom, no padding
SKRY_Image *SKRY_convert_pix_fmt(const SKRY_Image *src_img,
                                 enum SKRY_pixel_format dest_pix_fmt,
                                 /// Used if 'img' contains raw color data
                                 enum SKRY_demosaic_method demosaic_method);

/// Returned image has lines stored top-to-bottom, no padding
SKRY_Image *SKRY_convert_pix_fmt_of_subimage(
        const SKRY_Image *src_img, enum SKRY_pixel_format dest_pix_fmt,
        int x0, int y0, unsigned width, unsigned height,
        /// Used if 'img' contains raw color data
        enum SKRY_demosaic_method demosaic_method);

/// Converts a fragment of 'src_img' to 'dest_img's pixel format and writes it into 'dest_img'
/** Cropping is performed if necessary. */
void SKRY_convert_pix_fmt_of_subimage_into(
        const SKRY_Image *src_img,
        SKRY_Image       *dest_img,
        int src_x0, int src_y0,
        int dest_x0, int dest_y0,
        unsigned width, unsigned height,
        /// Used if 'img' contains raw color data
        enum SKRY_demosaic_method demosaic_method);

/// Returns a rectangle at (0, 0) and the same size as the image
struct SKRY_rect SKRY_get_img_rect(const SKRY_Image *img);

const unsigned *SKRY_get_supported_output_formats(
                    size_t *num_formats ///< Receives number of elements in returned array
                    );

enum SKRY_pixel_format SKRY_get_output_pix_fmt(enum SKRY_output_format output_fmt);

/// Returns null on error
SKRY_Image *SKRY_load_image(const char *file_name,
                            enum SKRY_result *result ///< If not null, receives operation result
                           );

/// Returns metadata without reading the pixel data
enum SKRY_result SKRY_get_image_metadata(const char *file_name,
                                           unsigned *width,  ///< If not null, receives image width
                                           unsigned *height, ///< If not null, receives image height
                                           enum SKRY_pixel_format *pix_fmt ///< If not null, receives pixel format
                                          );


enum SKRY_result SKRY_save_image(const SKRY_Image *img, const char *file_name,
                                 enum SKRY_output_format output_fmt);

/// Returns number of bytes occupied by the image
/** The value may not encompass some of image's metadata (ca. tens to hundreds of bytes). */
size_t SKRY_get_img_byte_count(const SKRY_Image *img);

/// Treat the image as containing raw color data (pixel format will be updated)
/** Can be used only if the image is 8- or 16-bit mono. Only pixel format is updated,
    the pixel data is unchanged. To demosaic, call this function and then use one
    of the pixel format conversion functions. */
void SKRY_reinterpret_as_CFA(SKRY_Image *img, enum SKRY_CFA_pattern CFA_pattern);

#endif // LIB_STACKISTRY_IMAGE_HEADER
