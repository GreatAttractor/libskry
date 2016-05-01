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
    Miscellaneous functions implementation.
*/

#include <assert.h>
#include <ctype.h>   // for tolower()
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "misc.h"


int compare_extension(const char *file_name,
                      const char *extension
                     )
{
    assert(file_name);
    char *last_period = strrchr(file_name, '.');
    if (last_period && (size_t)(last_period-file_name) == strlen(file_name)-strlen(extension)-1)
    {
        for (size_t i = 1; i <= strlen(extension); i++)
            if (tolower(last_period[i]) != extension[i-1])
                return 0;

        return 1;
    }
    else
        return 0;
}

static
uint32_t get_sum_sqr_diffs_from_histogram(
    uint64_t histogram[256],
    unsigned i_min, unsigned i_max)
{
    float avg = 0.0f;
    size_t num_pix = 0;
    for (size_t i = i_min; i <= i_max; i++)
    {
        avg += histogram[i] * i;
        num_pix += histogram[i];
    }
    avg /= num_pix;

    uint32_t sum_sqr_diffs = 0.0f;
    for (unsigned i = i_min; i <= i_max; i++)
        sum_sqr_diffs += histogram[i] * SKRY_SQR(i - avg);

    return sum_sqr_diffs;
}

uint8_t get_background_threshold(const SKRY_Image *img)
{
    assert(SKRY_get_img_pix_fmt(img) == SKRY_PIX_MONO8);

    uint64_t histogram[256] = { 0 };
    for (unsigned i = 0; i < SKRY_get_img_height(img); i++)
        for (unsigned j = 0; j < SKRY_get_img_width(img); j++)
            histogram[*((uint8_t *)SKRY_get_line(img, i) + j)] += 1;

    // Use bisection to find the value 'curr_div_pos' in histogram which has
    // the lowest sum of squared pixel value differences from the average
    // for all pixels darker and brighter than 'curr_div_pos'.
    //
    // This identifies the most significant "dip" in the histogram
    // which separates two groups of "similar" values: those belonging
    // to the disc and to the background.

    unsigned i_low = 0, i_high = 255;
    unsigned curr_div_pos = (i_high - i_low) / 2;

    while (i_high - i_low > 1)
    {
        unsigned div_pos_left = (i_low + curr_div_pos) / 2,
                 div_pos_right = (i_high + curr_div_pos) / 2;

        float var_sum_left = get_sum_sqr_diffs_from_histogram(histogram, 0, div_pos_left)
                + get_sum_sqr_diffs_from_histogram(histogram, div_pos_left, 255);

        float var_sum_right = get_sum_sqr_diffs_from_histogram(histogram, 0, div_pos_right)
                + get_sum_sqr_diffs_from_histogram(histogram, div_pos_right, 255);

        if (var_sum_left < var_sum_right)
        {
            i_high = curr_div_pos;
            curr_div_pos = div_pos_left;
        }
        else
        {
            i_low = curr_div_pos;
            curr_div_pos = div_pos_right;
        }
    }

    return curr_div_pos;
}

/// 1-second accuracy
static
double default_clock_func(void)
{
    time_t now = time(0);
    struct tm loct = *localtime(&now);
    loct.tm_sec = 0;
    loct.tm_min = 0;
    loct.tm_hour = 0;
    loct.tm_mday = 1;
    loct.tm_mon = 1;
    loct.tm_year = 1980 - 1900;
    time_t distant_past = mktime(&loct);
    return difftime(now, distant_past);
}

SKRY_clock_sec_fn *clock_func = default_clock_func;

void SKRY_set_clock_func(SKRY_clock_sec_fn new_clock_func)
{
    clock_func = new_clock_func;
}

double SKRY_clock_sec()
{
    return clock_func();
}

void find_min_max_brightness(const SKRY_Image *img, uint8_t *bmin, uint8_t *bmax)
{
    assert(SKRY_get_img_pix_fmt(img) == SKRY_PIX_MONO8);

    *bmin = WHITE_8bit, *bmax = 0;
    for (unsigned y = 0; y < SKRY_get_img_height(img); y++)
    {
        uint8_t *line = SKRY_get_line(img, y);
        for (unsigned x = 0; x < SKRY_get_img_width(img); x++)
        {
            if (line[x] < *bmin)
                *bmin = line[x];
            if (line[x] > *bmax)
                *bmax = line[x];
        }
    }
}

void swap_words16(SKRY_Image *img)
{
    for (size_t j = 0; j < SKRY_get_img_height(img); j++)
    {
        uint16_t *line = SKRY_get_line(img, j);
        for (size_t i = 0; i < SKRY_get_img_width(img); i++)
            line[i] = (line[i] << 8) | (line[i] >> 8);
    }
}

int is_machine_big_endian()
{
    static int one = 1;
    return *(char *)&one == 0;
}

uint32_t cnd_swap_32(uint32_t x, int do_swap)
{
    if (do_swap)
        return (x << 24) | ((x & 0x00FF0000) >> 8) | ((x & 0x0000FF00) << 8) | (x >> 24);
    else
        return x;
}


uint32_t cnd_swap_16_in_32(uint32_t x, int do_swap)
{
    if (do_swap)
        return ((x & 0xFF) << 8) | (x >> 8);
    else
        return x;

}

uint16_t cnd_swap_16(uint16_t x, int do_swap)
{
    if (do_swap)
        return (x << 8) | (x >> 8);
    else
        return x;
}
