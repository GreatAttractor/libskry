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
    Internal image-handling functions header.
*/

#ifndef LIB_STACKISTRY_IMAGE_INTERNAL_HEADER
#define LIB_STACKISTRY_IMAGE_INTERNAL_HEADER

#include <stddef.h>
#include <stdint.h>

#include <skry/image.h>



typedef     struct SKRY_image *fn_free(struct SKRY_image *);
typedef               unsigned fn_get_width(const struct SKRY_image *);
typedef               unsigned fn_get_height(const struct SKRY_image *);
typedef              ptrdiff_t fn_get_line_stride_in_bytes(const struct SKRY_image *);
typedef                 size_t fn_get_bytes_per_pixel(const struct SKRY_image *);
typedef                  void *fn_get_line(const struct SKRY_image *, size_t);
typedef       enum SKRY_result fn_get_palette(const struct SKRY_image *, struct SKRY_palette *);

struct SKRY_image
{
    void *data;
    enum SKRY_pixel_format pix_fmt;

    fn_free                     *free; ///< Always returns null
    fn_get_width                *get_width;
    fn_get_height               *get_height;
    fn_get_line_stride_in_bytes *get_line_stride_in_bytes;
    fn_get_bytes_per_pixel      *get_bytes_per_pixel;
    fn_get_line                 *get_line;
    fn_get_palette              *get_palette;
};

struct internal_img_data
{
    int width;
    int height;
    struct SKRY_palette palette;

    /// Lines stored top-to-bottom, no padding
    void *pixels;
};

/// 'img' is a pointer to 'struct SKRY_image'
#define IMG_DATA(img) ((struct internal_img_data *)(img)->data)

/// Allocates and initializes an empty internal image structure; returns null if out of memory
struct SKRY_image *create_internal_img(void);

#endif // LIB_STACKISTRY_IMAGE_INTERNAL_HEADER
