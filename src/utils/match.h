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
    Block-matching header.
*/

#ifndef LIBSKRY_IMG_MATCHING_HEADER
#define LIBSKRY_IMG_MATCHING_HEADER

#include <skry/defs.h>
#include <skry/image.h>


/** Returns the sum of squared differences between pixels of 'img' and 'ref_block',
    with 'ref_block's center aligned on 'pos' over 'img'. The differences are
    calculated only for the 'refblk_rect' portion of 'ref_block'.

    Both 'ref_block' and 'img' must be SKRY_PIX_MONO8. The result is 64-bit, so for
    8-bit images it can accommodate a block of 2^(64-2*8) = 2^48 pixels.
*/
uint64_t calc_sum_of_squared_diffs(
    const SKRY_Image *img,       ///< Image (SKRY_PIX_MONO8) to compare with the reference area in 'comp_block'
    const SKRY_Image *ref_block, ///< Block of pixels (reference area) used for comparison; smaller than 'img'
    const struct SKRY_point *pos,      ///< Position of 'ref_block's center inside 'img'
    const struct SKRY_rect refblk_rect ///< Part of 'ref_block' to be used for comparison
);


//TODO: add comments
void find_matching_position(
    struct SKRY_point ref_pos,
    const SKRY_Image *ref_block,
    const SKRY_Image *image,
    unsigned search_radius,
    unsigned initial_search_step,
    struct SKRY_point *new_pos);

#endif // LIBSKRY_IMG_MATCHING_HEADER
