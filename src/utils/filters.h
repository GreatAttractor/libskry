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
    Image filters header.
*/

#ifndef LIBSKRY_FILTERS_HEADER
#define LIBSKRY_FILTERS_HEADER

#include <skry/defs.h>
#include <skry/image.h>


/// Returns blurred image (SKRY_PIX_MONO8) or null if out of memory
/** Requirements: img is SKRY_PIX_MONO8, box_radius < 2^11 */
struct SKRY_image *box_blur_img(struct SKRY_image *img, unsigned box_radius, size_t iterations);

/// Estimates quality of the specified area (8 bits per pixel)
/** Quality is the sum of differences between input image
    and its blurred version. In other words, sum of values
    of the high-frequency component. The sum is normalized
    by dividing by the number of pixels. */
SKRY_quality_t estimate_quality(uint8_t *pixels, unsigned width, unsigned height, ptrdiff_t line_stride, unsigned box_blur_radius);

/// Perform median filtering on 'array'
void median_filter(double array[],
                   double output[], ///< Receives filtered contents of 'array'
                   size_t array_len,
                   size_t window_radius);

#endif // LIBSKRY_FILTERS_HEADER
