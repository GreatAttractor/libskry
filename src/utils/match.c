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
    Block-matching implementation.
*/

#include <assert.h>
#include <limits.h>
#include <stdint.h>

#include "match.h"


#define MIN_FRACTION_OF_BLOCK_TO_MATCH 4

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
    )
{
    uint64_t result = 0;

    int block_width = SKRY_get_img_width(ref_block);
    int block_height = SKRY_get_img_height(ref_block);

    uint8_t * restrict img_line = SKRY_get_line(img, pos->y - block_height/2 + refblk_rect.y);
    ptrdiff_t img_stride = SKRY_get_line_stride_in_bytes(img);

    uint8_t * restrict blk_line = SKRY_get_line(ref_block, refblk_rect.y);
    ptrdiff_t blk_stride = SKRY_get_line_stride_in_bytes(ref_block);

    assert(pos->x - (int)block_width/2  + refblk_rect.x >= 0);
    assert(pos->y - (int)block_height/2 + refblk_rect.y >= 0);

    // Both 'x' and 'y' will be non-negative at all times (ensured by the caller)
    for (int y = pos->y - (int)block_height/2 + refblk_rect.y;
             y < pos->y - (int)block_height/2 + refblk_rect.y + (int)refblk_rect.height;
             y++)
    {
        for (int x = pos->x - (int)block_width/2 + refblk_rect.x;
                 x < pos->x - (int)block_width/2 + refblk_rect.x + (int)refblk_rect.width;
                 x++)
        {
            result += SKRY_SQR(img_line[x] - blk_line[x - pos->x + (int)block_width/2]);
        }

        img_line += img_stride;
        blk_line += blk_stride;
    }


    return result;
}

#define CLAMP_xmin(val)  ((val) > search_envelope.x + block_width/2  ? (val) : search_envelope.x + block_width/2)
#define CLAMP_ymin(val)  ((val) > search_envelope.y + block_height/2 ? (val) : search_envelope.y + block_height/2)
#define CLAMP_xmax(val) SKRY_MIN(val, search_envelope.x + search_envelope.width - block_width/2)
#define CLAMP_ymax(val) SKRY_MIN(val, search_envelope.x + search_envelope.height - block_height/2)

//TODO: add comments
void find_matching_position(
    struct SKRY_point ref_pos,
    const SKRY_Image *ref_block,
    const SKRY_Image *image,
    unsigned search_radius,
    unsigned initial_search_step,
    struct SKRY_point *new_pos)
{
    assert(SKRY_get_img_pix_fmt(image) == SKRY_PIX_MONO8);
    assert(SKRY_get_img_pix_fmt(ref_block) == SKRY_PIX_MONO8);

    unsigned blkw = SKRY_get_img_width(ref_block),
             blkh = SKRY_get_img_height(ref_block),
             imgw = SKRY_get_img_width(image),
             imgh = SKRY_get_img_height(image);

    // At first using a coarse step when trying to match 'ref_block'
    // with 'img' at different positions. Once an approximate matching
    // position is determined, the search continues around it repeatedly
    // using a smaller step, until the step becomes 1.
    unsigned search_step = initial_search_step;

    // Range of positions where 'ref_block' will be match-tested with 'img'.
    // Using signed type is necessary, as the positions may be negative
    // (then the block is appropriately clipped before comparison).
    struct search_pos_t
    {
        int xmin, ymin; // inclusive
        int xmax, ymax; // exclusive
    } search_pos = { .xmin = ref_pos.x - (int)search_radius,
                     .ymin = ref_pos.y - (int)search_radius,
                     .xmax = ref_pos.x + (int)search_radius,
                     .ymax = ref_pos.y + (int)search_radius };

    struct SKRY_point best_pos = { .x = 0, .y = 0 };

    while (search_step)
    {
        // Min. sum of squared differences between pixel values of
        // the reference block and the image at candidate positions.
        uint64_t min_sq_diff_sum = UINT64_MAX;

        // (x, y) = position in 'img' for which a block match test is performed.
        for (int y = search_pos.ymin; y < search_pos.ymax;  y += search_step)
            for (int x = search_pos.xmin; x < search_pos.xmax;  x += search_step)
            {
                /*
                    It is allowed for 'ref_block' to not be entirely inside 'image'.
                    Before calling calc_sum_of_squared_diffs(), find a sub-rectangle
                    'refblk_rect' of 'ref_block' which lies within 'image':

                    +======== ref_block ========+
                    |                           |
                    |   +-------- img ----------|-------
                    |   |.......................|
                    |   |..........*............|
                    |   |.......................|
                    |   |.......................|
                    +===========================+
                        |
                        |

                    *: current search position (x, y); corresponds with the middle
                       of 'ref_block' during block matching

                    Dotted area is the 'refblk_rect'. Start coordinates of 'refblk_rect'
                    are relative to the 'ref_block'; if whole 'ref_block' fits in 'image',
                    then refblk_rect = {0, 0, blkw, blkh}.
                */

                struct SKRY_rect refblk_rect =
                    { .x = (x >= (int)blkw/2) ? 0 : (int)blkw/2 - x,
                      .y = (y >= (int)blkh/2) ? 0 : (int)blkh/2 - y };

                int refblk_rect_xmax = (x + (int)blkw/2 <= (int)imgw) ? blkw : blkw - (x + (int)blkw/2 - (int)imgw);
                int refblk_rect_ymax = (y + (int)blkh/2 <= (int)imgh) ? blkh : blkh - (y + (int)blkh/2 - (int)imgh);

                uint64_t sum_sq_diffs;

                if (refblk_rect.x >= refblk_rect_xmax ||
                    refblk_rect.y >= refblk_rect_ymax)
                {
                    // ref. block completely outside image
                   sum_sq_diffs = UINT64_MAX;
                }
                else
                {
                    refblk_rect.width = refblk_rect_xmax - refblk_rect.x;
                    refblk_rect.height = refblk_rect_ymax - refblk_rect.y;

                    if (refblk_rect.width < blkw/MIN_FRACTION_OF_BLOCK_TO_MATCH ||
                        refblk_rect.height < blkh/MIN_FRACTION_OF_BLOCK_TO_MATCH)
                    {
                        // ref. block too small to compare
                        sum_sq_diffs = UINT64_MAX;
                    }
                    else
                    {
                        sum_sq_diffs = calc_sum_of_squared_diffs(image, ref_block,
                                                                &(struct SKRY_point) { .x = x, .y = y },
                                                                refblk_rect);

                        // The sum must be normalized in order to be comparable with others
                        sum_sq_diffs *= blkw*blkh / (refblk_rect.width*refblk_rect.height);
                    }
                }

                if (sum_sq_diffs < min_sq_diff_sum)
                {
                    min_sq_diff_sum = sum_sq_diffs;
                    best_pos.x = x;
                    best_pos.y = y;
                }
            }

        search_pos = (struct search_pos_t) { .xmin = best_pos.x - search_step,
                                             .ymin = best_pos.y - search_step,
                                             .xmax = best_pos.x + search_step,
                                             .ymax = best_pos.y + search_step };
        search_step /= 2;
    }

    new_pos->x = best_pos.x;
    new_pos->y = best_pos.y;
}
