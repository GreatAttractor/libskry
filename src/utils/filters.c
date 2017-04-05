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
    Image filters implementation.
*/

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <skry/defs.h>

#include "filters.h"


// Result of 3 iterations is quite close to a Gaussian blur
#define QUALITY_ESTIMATE_BOX_BLUR_ITERATIONS 3

/** Performs a blurring pass of a single row or column.
    Using a macro, because 'src' can be a pointer to uint8_t or uint32_t. */
#define BOX_BLUR_PASS(src, pix_sum, box_radius, length, step)                                   \
{                                                                                               \
    /* Sum for the first pixel in the current line/row   */                                     \
    /* (count the last pixel multiple times if needed)   */                                     \
    pix_sum[0] = (box_radius + 1) * src[0];                                                     \
    for (unsigned i = step; i <= box_radius * step; i += step)                                  \
        pix_sum[0] += src[SKRY_MIN(i, (length - 1) * step)];                                    \
                                                                                                \
                                                                                                \
    /* Starting region */                                                                       \
    for (unsigned i = step; i <= SKRY_MIN((length - 1) * step, box_radius * step); i += step)   \
        pix_sum[i] = pix_sum[i - step] - src[0]                                                 \
            + src[SKRY_MIN((length - 1) * step, i + box_radius * step)];                        \
                                                                                                \
    if (length > box_radius)                                                                    \
    {                                                                                           \
        /* Middle region */                                                                     \
        for (unsigned i = (box_radius + 1) * step; i < (length - box_radius) * step; i += step) \
            pix_sum[i] = pix_sum[i - step] - src[i - (box_radius + 1) * step]                   \
            + src[i + box_radius * step];                                                       \
                                                                                                \
        /* End region */                                                                        \
        for (unsigned i = (length - box_radius) * step; i < length * step; i += step)           \
             pix_sum[i] = pix_sum[i - step]                                                     \
               - src[(i > (box_radius + 1) * step) ? i - (box_radius + 1) * step : 0]           \
                       + src[SKRY_MIN(i + box_radius * step, (length - 1) * step)];             \
    }                                                                                           \
}

static
enum SKRY_result box_blur(uint8_t *src, uint8_t *blurred, unsigned width, unsigned height,
                   ptrdiff_t src_line_stride, ptrdiff_t blurred_line_stride,
                   unsigned box_radius, unsigned iterations)
{
    assert(iterations > 0);
    assert(box_radius > 0);

    if (width == 0 || height == 0)
        return SKRY_SUCCESS;

    /* First the 32-bit unsigned sums of neighborhoods are calculated horizontally
       and (incrementally) vertically. The max value of a (unsigned) sum is:

            (2^8-1) * (box_radius*2 + 1)^2

       In order for it to fit in 32 bits, box_radius must be below ca. 2^11 - 1.
    */
    assert(box_radius < (1U<<11) - 1);

    // We need 2 summation buffers to act as source/destination (and then vice versa)
    uint32_t *pix_sum_1 = malloc(width * height * sizeof(*pix_sum_1));
    uint32_t *pix_sum_2 = malloc(width * height * sizeof(*pix_sum_2));
    if (!pix_sum_1 || !pix_sum_2)
    {
        free(pix_sum_1);
        free(pix_sum_2);
        return SKRY_OUT_OF_MEMORY;
    }

    unsigned divisor = SKRY_SQR(2*box_radius + 1);

    // For pixels less than 'box_radius' away from image border, assume
    // the off-image neighborhood consists of copies of the border pixel.

    uint32_t * restrict src_array = pix_sum_1,
             * restrict dest_array = pix_sum_2;

    for (unsigned n = 0; n < iterations; n++)
    {
        uint32_t *tmp = src_array;
        src_array = dest_array;
        dest_array = tmp;

        // Calculate horizontal neighborhood sums
        if (n == 0)
        {
            uint8_t * restrict src_line = src;
            uint32_t * restrict dest_line = dest_array;
            // Special case: in iteration 0 the source is the 8-bit 'src'
            for (unsigned y = 0; y < height; y++)
            {
                BOX_BLUR_PASS(src_line, dest_line, box_radius, width, 1);

                src_line += src_line_stride;
                dest_line += width;
            }
        }
        else
        {
            uint32_t * restrict src_line = src_array;
            uint32_t * restrict dest_line = dest_array;

            for (unsigned y = 0; y < height; y++)
            {
                BOX_BLUR_PASS(src_line, dest_line, box_radius, width, 1);

                src_line += width;
                dest_line += width;
            }
        }

        tmp = src_array;
        src_array = dest_array;
        dest_array = tmp;

        // Calculate vertical neighborhood sums
        uint32_t * restrict src_col = src_array;
        uint32_t * restrict dest_col = dest_array;
        for (unsigned x = 0; x < width; x++)
        {
            BOX_BLUR_PASS(src_col, dest_col, box_radius, height, width);

            src_col += 1;
            dest_col += 1;
        }

        // Divide to obtain normalized result. We choose not to divide just once
        // after completing all iterations, because the 32-bit intermediate values
        // would overflow in as little as 3 iterations with 8-pixel box radius
        // for an all-white input image. In such case the final sums would be:
        //
        //   255 * ((2*8+1)^2)^3 = 6'155'080'095
        //
        // (where the exponent 3 = number of iterations)
        uint32_t * line_ptr = dest_array;
        for (unsigned y = 0; y < height; y++)
        {
            for (unsigned x = 0; x < width; x++)
            {
                line_ptr[x] /= divisor;
            }
            line_ptr += width;
        }
    }

    uint8_t * restrict blurred_line = blurred;
    // 'dest_array' is where the last summation results were stored,
    // now use it as source for producing the final 8-bit image in 'blurred'
    uint32_t * restrict pix_sum_line = dest_array;

    for (unsigned y = 0; y < height; y++)
    {
        for (unsigned x = 0; x < width; x++)
        {
            blurred_line[x] = pix_sum_line[x];
        }

        blurred_line += blurred_line_stride;
        pix_sum_line += width;
    }

    free(pix_sum_1);
    free(pix_sum_2);

    return SKRY_SUCCESS;
}

/// Returns blurred image (SKRY_PIX_MONO8) or null if out of memory
/** Requirements: img is SKRY_PIX_MONO8, box_radius < 2^11 */
struct SKRY_image *box_blur_img(const struct SKRY_image *img, unsigned box_radius, size_t iterations)
{
    assert(SKRY_get_img_pix_fmt(img) == SKRY_PIX_MONO8);

    SKRY_Image *blurred = SKRY_new_image(SKRY_get_img_width(img), SKRY_get_img_height(img), SKRY_PIX_MONO8, 0, 0);
    if (blurred)
    {
        enum SKRY_result result = box_blur(SKRY_get_line(img, 0), SKRY_get_line(blurred, 0),
                                     SKRY_get_img_width(img), SKRY_get_img_height(img),
                                     SKRY_get_line_stride_in_bytes(img), SKRY_get_line_stride_in_bytes(blurred),
                                     box_radius, iterations);
        if (SKRY_SUCCESS == result)
            return blurred;
        else
        {
            SKRY_free_image(blurred);
            return 0;
        }
    }
    else
        return 0;
}

/// Estimates quality of the specified area (8 bits per pixel)
/** Quality is the sum of differences between input image
    and its blurred version. In other words, sum of values
    of the high-frequency component. The sum is normalized
    by dividing by the number of pixels. */
SKRY_quality_t estimate_quality(uint8_t *pixels, unsigned width, unsigned height, ptrdiff_t line_stride, unsigned box_blur_radius)
{
    uint8_t *blurred = malloc(width*height);
    box_blur(pixels, blurred, width, height, line_stride, width,
             box_blur_radius, QUALITY_ESTIMATE_BOX_BLUR_ITERATIONS);

    SKRY_quality_t quality = 0;

    uint8_t * restrict src_line = pixels;
    uint8_t * restrict blurred_line = blurred;
    for (unsigned y = 0; y < height; y++)
    {
        for (unsigned x = 0; x < width; x++)
            quality += abs(src_line[x] - blurred_line[x]);

        src_line += line_stride;
        blurred_line +=  width;
    }

    free(blurred);

    return quality / (width*height);
}

static
void insertion_sort(double array[], size_t array_len)
{
    for (int i = 1; i < (int)array_len; i++)
    {
        double x = array[i];
        int j = i - 1;
        while (j >= 0 && array[j] > x)
        {
            array[j+1] = array[j];
            j -= 1;
        }
        array[j+1] = x;
    }
}

/** Finds 'remove_val' in the sorted 'array', replaces it with 'new_val'
    and ensures 'array' remains sorted. */
static
void shift_sorted_window(double array[], size_t length, double remove_val, double new_val)
{
    // Use binary search to locate 'remove_val' in 'array'
    size_t idx_lo = 0, idx_hi = length, idx_mid;
    do
    {
        idx_mid = (idx_hi + idx_lo)/2;
        if (array[idx_mid] < remove_val)
            idx_lo = idx_mid;
        else if (array[idx_mid] > remove_val)
            idx_hi = idx_mid;
        else
            break;

    } while (idx_hi > idx_lo);

    // Insert 'new_val' into 'array' and (if needed) move it to keep the array sorted
    size_t curr_idx = idx_mid;
    array[curr_idx] = new_val;
    while (curr_idx <= length-2 && array[curr_idx] > array[curr_idx + 1])
    {
        double tmp = array[curr_idx + 1];
        array[curr_idx + 1] = array[curr_idx];
        array[curr_idx] = tmp;
        curr_idx += 1;
    }
    while (curr_idx > 0 && array[curr_idx] < array[curr_idx - 1])
    {
        double tmp = array[curr_idx - 1];
        array[curr_idx - 1] = array[curr_idx];
        array[curr_idx] = tmp;
        curr_idx -= 1;
    }
}

/// Perform median filtering on 'array'
void median_filter(const double array[],
                   double output[], ///< Receives filtered contents of 'array'
                   size_t array_len,
                   size_t window_radius)
{
    assert(window_radius > 0 && window_radius < array_len);

    size_t wnd_len = 2*window_radius + 1;
    // A sorted array
    double *window = malloc(wnd_len * sizeof(*window));

    // Set initial window contents
    for (size_t i = 0; i <= window_radius; i++)
    {
        // upper half
        window[window_radius + i] = array[i];

        // lower half (consists of repeated array[0] value)
        if (i < window_radius)
            window[i] = array[0];
    }
    insertion_sort(window, wnd_len);

    // Replace every 'array' element in 'output' with window's median and shift the window
    for (size_t i = 0; i < array_len; i++)
    {
        output[i] = window[window_radius];
        shift_sorted_window(window, wnd_len,
                            array[SKRY_MAX((int)i - (int)window_radius, 0)],
                            array[SKRY_MIN(i+1 + window_radius, array_len-1)]);
    }

    free(window);
}
