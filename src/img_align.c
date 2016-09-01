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
    Image alignment (video stabilization) implementation.
*/

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <skry/defs.h>
#include <skry/image.h>
#include <skry/imgseq.h>
#include <skry/img_align.h>

#include "utils/dnarray.h"
#include "utils/filters.h"
#include "utils/logging.h"
#include "utils/match.h"
#include "utils/misc.h"


#define QUALITY_EST_BOX_BLUR_RADIUS 2

struct anchor_data
{
    struct SKRY_point pos; ///< Current position
    int is_valid;
    /// Square image fragment (of the best quality so far) centered (after alignment) on 'pos'
    SKRY_Image *ref_block;
    SKRY_quality_t ref_block_qual;
};

struct SKRY_img_alignment
{
    int is_complete;
    SKRY_ImgSequence *img_seq;
    size_t curr_img_idx;

    enum SKRY_img_alignment_method algn_method;

    /** Anchor points used for alignment (there is at least one).
        Coordinates are relative to the current image's origin. */
    DA_DECLARE(struct anchor_data) anchors;
    size_t active_anchor_idx;

    /// Radius (in pixels) of anchors' reference blocks
    unsigned block_radius;

    /** Images are aligned by matching blocks that are offset (horizontally and vertically)
        by up to 'search_radius' pixels. */
    unsigned search_radius;

    /// Min. image brightness that an anchor can be placed at (values: [0; 1])
    /** Value is relative to the image's darkest (0.0) and brightest (1.0) pixels. */
    float placement_brightness_threshold;

    struct SKRY_point centroid_pos;

    /// Set-theoretic intersection of all images after alignment (i.e. the fragment which is visible in all images)
    struct
    {
        /// Offset, relative to the first image's origin
        struct SKRY_point offset;
        /// Coordinates of the bottom right corner (belongs to the intersection), relative to the first image's origin
        struct SKRY_point bottom_right;

        unsigned width; ///< width of the intersection
        unsigned height; ///< height of the intersection
    } intersection;

    /// Image offsets (relative to each image's origin) necessary for them to be aligned
    struct SKRY_point *img_offsets;
};

struct SKRY_img_alignment *SKRY_free_img_alignment(struct SKRY_img_alignment *img_algn)
{
    // Note that we do not free the associated image sequence here; the user has to do it
    // (i.e. the one who allocated it).
    if (img_algn)
    {
        if (img_algn->anchors.data)
        {
            for (size_t i = 0; i < DA_SIZE(img_algn->anchors); i++)
                SKRY_free_image(img_algn->anchors.data[i].ref_block);

            DA_FREE(img_algn->anchors);
        }

        free(img_algn->img_offsets);

        free(img_algn);
    }
    return 0;
}

int SKRY_is_img_alignment_complete(const struct SKRY_img_alignment *img_algn)
{
    return img_algn->is_complete;
}

#define FAIL_ON_NULL(ptr)                         \
    if (!(ptr))                                   \
    {                                             \
        SKRY_free_img_alignment(img_algn);        \
        if (result) *result = SKRY_OUT_OF_MEMORY; \
        return 0;                                 \
    }

struct SKRY_img_alignment *SKRY_init_img_alignment(
    SKRY_ImgSequence *img_seq,
    enum SKRY_img_alignment_method method,

    // Parameters used if method==SKRY_IMG_ALGN_ANCHORS ------------

    /// If zero, anchors will be placed automatically
    size_t num_anchors,

    /// Coords relative to the first image's origin; may be null if num_anchors==0
    const struct SKRY_point *anchors,

    unsigned block_radius,  ///< Radius (in pixels) of square blocks used for matching images
    unsigned search_radius, ///< Max offset in pixels (horizontal and vertical) of blocks during matching

    /// Min. image brightness that an anchor can be placed at (values: [0; 1])
    /** Value is relative to the image's darkest (0.0) and brightest (1.0) pixels. */
    float placement_brightness_threshold,

    // -------------------------------------------------------------

    enum SKRY_result *result ///< If not null, receives operation result
)
{
    assert(SKRY_get_active_img_count(img_seq) > 0);

    if (SKRY_IMG_ALGN_ANCHORS == method)
        if (block_radius == 0 || search_radius == 0)
        {
            if (result)
                *result = SKRY_INVALID_PARAMETERS;
            return 0;
        }

    enum SKRY_result local_result;
    SKRY_seek_start(img_seq);
    SKRY_Image *first_img = SKRY_get_curr_img(img_seq, &local_result);
    if (local_result != SKRY_SUCCESS)
    {
        if (result)
            *result = local_result;
        return 0;
    }

    struct SKRY_img_alignment *img_algn = malloc(sizeof(*img_algn));
    FAIL_ON_NULL(img_algn);

    // Sets all pointer fields to null, so it is safe to call SKRY_free_img_alignment()
    // via FAIL_ON_NULL() in case one of allocation fails.
    *img_algn = (struct SKRY_img_alignment) { 0 };

    img_algn->img_seq = img_seq;
    img_algn->algn_method = method;

    img_algn->img_offsets = malloc(SKRY_get_active_img_count(img_seq) * sizeof(*img_algn->img_offsets));
    FAIL_ON_NULL(img_algn->img_offsets);
    img_algn->intersection.offset = (struct SKRY_point) { .x = 0, .y = 0 };
    img_algn->intersection.bottom_right = (struct SKRY_point) { .x = INT_MAX, .y = INT_MAX };
    img_algn->intersection.width = img_algn->intersection.height = 0;

    if (SKRY_IMG_ALGN_ANCHORS == method)
    {
        struct SKRY_point automatic_anchor;
        if (num_anchors == 0 || anchors == 0)
        {
            automatic_anchor = SKRY_suggest_anchor_pos(first_img, placement_brightness_threshold, 2*block_radius);
            LOG_MSG(SKRY_LOG_IMG_ALIGNMENT, "No anchors specified; adding anchor at (%d, %d).",
                    automatic_anchor.x, automatic_anchor.y);
            anchors = &automatic_anchor;
            num_anchors = 1;
        }

        img_algn->search_radius = search_radius;
        img_algn->block_radius = block_radius;
        img_algn->placement_brightness_threshold = placement_brightness_threshold;

        img_algn->active_anchor_idx = 0;
        DA_ALLOC(img_algn->anchors, num_anchors);
        DA_SET_SIZE(img_algn->anchors, num_anchors);
        FAIL_ON_NULL(img_algn->anchors.data);
        for (size_t i = 0; i < num_anchors; i++)
            img_algn->anchors.data[i].is_valid = 1;

        for (size_t i = 0; i < DA_SIZE(img_algn->anchors); i++)
        {
            img_algn->anchors.data[i].pos = anchors[i];
            SKRY_Image *blk = SKRY_convert_pix_fmt_of_subimage(first_img, SKRY_PIX_MONO8,
                                             anchors[i].x - block_radius,
                                             anchors[i].y - block_radius,
                                             2*block_radius, 2*block_radius, SKRY_DEMOSAIC_SIMPLE);
            img_algn->anchors.data[i].ref_block = blk;
            img_algn->anchors.data[i].ref_block_qual =
                estimate_quality(SKRY_get_line(blk, 0),
                    SKRY_get_img_width(blk),
                    SKRY_get_img_height(blk),
                    SKRY_get_line_stride_in_bytes(blk),
                    QUALITY_EST_BOX_BLUR_RADIUS);
        }
    }
    else if (SKRY_IMG_ALGN_CENTROID == method)
    {
        img_algn->centroid_pos = SKRY_get_centroid(first_img, SKRY_get_img_rect(first_img));
    }

    if (result)
        *result = SKRY_SUCCESS;

    SKRY_free_image(first_img);

    return img_algn;
}

static
struct SKRY_point determine_img_offset_using_anchors(SKRY_ImgAlignment *img_algn,
                                                     const SKRY_Image *img /* must be SKRY_PIX_MONO8 */)
{
    struct SKRY_point active_anchor_offset = { 0 };

    for (size_t i = 0; i < DA_SIZE(img_algn->anchors); i++)
    {
        struct anchor_data *anchor = &img_algn->anchors.data[i];
        if (img_algn->anchors.data[i].is_valid)
        {
            struct SKRY_point new_pos;
            find_matching_position(anchor->pos, anchor->ref_block,
                                   img, img_algn->search_radius, 4, &new_pos);

            unsigned blkw = SKRY_get_img_width(anchor->ref_block),
                     blkh = SKRY_get_img_height(anchor->ref_block);

            if (new_pos.x < (int)(blkw + img_algn->search_radius) ||
                new_pos.x > (int)(SKRY_get_img_width(img) - blkw - img_algn->search_radius) ||
                new_pos.y < (int)(blkh + img_algn->search_radius) ||
                new_pos.y > (int)(SKRY_get_img_height(img) - blkh - img_algn->search_radius))
            {
                anchor->is_valid = 0;
            }

            SKRY_quality_t new_qual = estimate_quality((uint8_t *)SKRY_get_line(img, new_pos.y) + new_pos.x,
                                                       blkw, blkh,
                                                       SKRY_get_line_stride_in_bytes(img),
                                                       QUALITY_EST_BOX_BLUR_RADIUS);
            if (new_qual > anchor->ref_block_qual)
            {
                anchor->ref_block_qual = new_qual;

                // Refresh the reference block using the current image at the block's new position
                SKRY_convert_pix_fmt_of_subimage_into(img, anchor->ref_block,
                                                      new_pos.x - blkw/2,
                                                      new_pos.y - blkh/2,
                                                      0, 0,
                                                      blkw, blkh,
                                                      SKRY_DEMOSAIC_SIMPLE);
            }

            if (i == img_algn->active_anchor_idx)
            {
                active_anchor_offset.x = new_pos.x - anchor->pos.x;
                active_anchor_offset.y = new_pos.y - anchor->pos.y;
            }

            anchor->pos = new_pos;
        }
    }

    if (!img_algn->anchors.data[img_algn->active_anchor_idx].is_valid)
    {
        // Select the next available valid anchor as "active"
        size_t new_active_idx = img_algn->active_anchor_idx+1;
        while (new_active_idx < DA_SIZE(img_algn->anchors))
        {
            if (img_algn->anchors.data[new_active_idx].is_valid)
            {
                break;
            }
            else
                new_active_idx++;
        }

        if (new_active_idx >= DA_SIZE(img_algn->anchors))
        {
            // There are no more existing valid anchors; choose and add a new one
            DA_SET_SIZE(img_algn->anchors, DA_SIZE(img_algn->anchors) + 1);
            struct anchor_data *new_anchor = &DA_LAST(img_algn->anchors);

            new_anchor->pos = SKRY_suggest_anchor_pos(img,
                                                      img_algn->placement_brightness_threshold,
                                                      2*img_algn->block_radius);
            new_anchor->ref_block = SKRY_new_image(2*img_algn->block_radius,
                                                   2*img_algn->block_radius,
                                                   SKRY_PIX_MONO8, 0, 0);
            SKRY_resize_and_translate(img, new_anchor->ref_block,
                                      new_anchor->pos.x - img_algn->block_radius,
                                      new_anchor->pos.y - img_algn->block_radius,
                                      img_algn->block_radius,
                                      img_algn->block_radius,
                                      0, 0, 0);
            new_anchor->ref_block_qual = estimate_quality(SKRY_get_line(new_anchor->ref_block, 0),
                                                          SKRY_get_img_width(new_anchor->ref_block),
                                                          SKRY_get_img_height(new_anchor->ref_block),
                                                          SKRY_get_line_stride_in_bytes(new_anchor->ref_block),
                                                          QUALITY_EST_BOX_BLUR_RADIUS);
            new_anchor->is_valid = 1;
            img_algn->active_anchor_idx = DA_SIZE(img_algn->anchors)-1;

            LOG_MSG(SKRY_LOG_IMG_ALIGNMENT, "No more valid active anchors. Adding new anchor at (%d, %d).",
                    new_anchor->pos.x, new_anchor->pos.y);
        }
        else
            LOG_MSG(SKRY_LOG_IMG_ALIGNMENT, "Current anchor invalidated, switching to anchor %zu at (%d, %d).",
                    new_active_idx,
                    img_algn->anchors.data[new_active_idx].pos.x,
                    img_algn->anchors.data[new_active_idx].pos.y);
    }

    return active_anchor_offset;
}

static
struct SKRY_point determine_img_offset_using_centroid(SKRY_ImgAlignment *img_algn, const SKRY_Image *img)
{
    struct SKRY_point new_centroid_pos = SKRY_get_centroid(img, SKRY_get_img_rect(img));
    return (struct SKRY_point)
        { .x = new_centroid_pos.x - img_algn->centroid_pos.x,
          .y = new_centroid_pos.y - img_algn->centroid_pos.y };
}

enum SKRY_result SKRY_img_alignment_step(struct SKRY_img_alignment *img_algn)
{
    enum SKRY_result result = SKRY_SUCCESS;

    if (img_algn->curr_img_idx == 0)
    {
        img_algn->img_offsets[0].x = 0;
        img_algn->img_offsets[0].y = 0;

        unsigned width, height;
        result = SKRY_get_curr_img_metadata(img_algn->img_seq, &width, &height, 0);
        if (SKRY_SUCCESS == result)
        {
            img_algn->intersection.bottom_right.x = width - 1;
            img_algn->intersection.bottom_right.y = height - 1;
            img_algn->curr_img_idx += 1;
        }

        return result;
    }
    else
    {
        //FIXME: report error
        if (SKRY_seek_next(img_algn->img_seq) != SKRY_SUCCESS)
        {
            img_algn->intersection.width = img_algn->intersection.bottom_right.x - img_algn->intersection.offset.x + 1;
            img_algn->intersection.height = img_algn->intersection.bottom_right.y - img_algn->intersection.offset.y + 1;

            img_algn->is_complete = 1;

            return SKRY_LAST_STEP;
        }

        SKRY_Image *img = SKRY_get_curr_img(img_algn->img_seq, &result);
        if (!img)
            return result;


        struct SKRY_point detected_img_offset = { 0 };

        if (SKRY_IMG_ALGN_ANCHORS == img_algn->algn_method)
        {
            if (SKRY_get_img_pix_fmt(img) != SKRY_PIX_MONO8)
            {
                SKRY_Image *img_mono8 = SKRY_convert_pix_fmt(img, SKRY_PIX_MONO8, SKRY_DEMOSAIC_SIMPLE);
                SKRY_free_image(img);
                if (!img_mono8)
                    return SKRY_OUT_OF_MEMORY;

                img = img_mono8;
            }

            detected_img_offset = determine_img_offset_using_anchors(img_algn, img);
        }
        else if (SKRY_IMG_ALGN_CENTROID == img_algn->algn_method)
        {
            detected_img_offset = determine_img_offset_using_centroid(img_algn, img);
            SKRY_ADD_POINT_TO(img_algn->centroid_pos, detected_img_offset);
        }

        // 'img_offsets' contain offsets relative to the first frame, so store the current offset incrementally w.r.t. the previous one
        struct SKRY_point *curr_img_ofs = &img_algn->img_offsets[img_algn->curr_img_idx];
        *curr_img_ofs = SKRY_ADD_POINTS(
                img_algn->img_offsets[img_algn->curr_img_idx-1],
                detected_img_offset);

        img_algn->intersection.offset.x = SKRY_MAX(img_algn->intersection.offset.x, -curr_img_ofs->x);
        img_algn->intersection.offset.y = SKRY_MAX(img_algn->intersection.offset.y, -curr_img_ofs->y);
        img_algn->intersection.bottom_right.x = SKRY_MIN(img_algn->intersection.bottom_right.x, -curr_img_ofs->x + (int)SKRY_get_img_width(img) - 1);
        img_algn->intersection.bottom_right.y = SKRY_MIN(img_algn->intersection.bottom_right.y, -curr_img_ofs->y + (int)SKRY_get_img_height(img) - 1);
        img_algn->curr_img_idx += 1;

        img = SKRY_free_image(img);

        return result;
    }
}

size_t SKRY_get_anchor_count(const struct SKRY_img_alignment *img_algn)
{
    return DA_SIZE(img_algn->anchors);
}

void SKRY_get_anchors(const struct SKRY_img_alignment *img_algn,
                      struct SKRY_point points[])
{
    for (size_t i = 0; i < DA_SIZE(img_algn->anchors); i++)
        points[i] = img_algn->anchors.data[i].pos;
}

struct SKRY_point SKRY_get_intersection_ofs(const struct SKRY_img_alignment *img_algn)
{
    return img_algn->intersection.offset;
}

void SKRY_get_intersection_size(const struct SKRY_img_alignment *img_algn,
                                unsigned *width,
                                unsigned *height)
{
    if (width)
        *width = img_algn->intersection.width;
    if (height)
        *height = img_algn->intersection.height;
}

struct SKRY_point SKRY_get_image_ofs(const struct SKRY_img_alignment *img_algn, size_t img_idx)
{
    return img_algn->img_offsets[img_idx];
}

SKRY_ImgSequence *SKRY_get_img_seq(const struct SKRY_img_alignment *img_algn)
{
    return img_algn->img_seq;
}

struct SKRY_rect SKRY_get_intersection(const struct SKRY_img_alignment *img_algn)
{
    return (struct SKRY_rect) { .x = img_algn->intersection.offset.x,
                                .y = img_algn->intersection.offset.y,
                                .width = img_algn->intersection.width,
                                .height = img_algn->intersection.height };
}

struct SKRY_point SKRY_suggest_anchor_pos(
    const SKRY_Image *image,
    float placement_brightness_threshold,
    unsigned ref_block_size)
{
    unsigned width = SKRY_get_img_width(image);
    unsigned height = SKRY_get_img_height(image);

    SKRY_Image *img8 = (SKRY_Image *)image;
    if (SKRY_get_img_pix_fmt(image) != SKRY_PIX_MONO8)
    {
        img8 = SKRY_convert_pix_fmt(image, SKRY_PIX_MONO8, SKRY_DEMOSAIC_SIMPLE);
    }

    uint8_t bmin, bmax;
    find_min_max_brightness(img8, &bmin, &bmax);

    struct SKRY_point result = { .x = width/2, .y = height/2 };
    SKRY_quality_t best_qual = 0;

    // Consider only the middle 3/4 of 'image'
    for (unsigned y = height/8; y < 7*height/8 - ref_block_size; y += ref_block_size/3)
        for (unsigned x = width/8; x < 7*width/8 - ref_block_size; x += ref_block_size/3)
        {
            // Reject locations at the limb of an overexposed (fully white) disc with little (<20%)
            // prominence details (i.e. pixels above the "placement threshold").
            size_t num_above_thresh = 0;
            for (unsigned ny = y - ref_block_size/2; ny < y + ref_block_size/2; ny++)
            {
                uint8_t *line = SKRY_get_line(img8, ny);
                for (unsigned nx = x - ref_block_size/2; nx < x + ref_block_size/2; nx++)
                    if (line[nx] != WHITE_8bit && line[nx] >= bmin + placement_brightness_threshold * (bmax-bmin))
                        num_above_thresh++;
            }
            if (num_above_thresh > SKRY_SQR(ref_block_size)/5)
            {
                SKRY_quality_t qual = estimate_quality((uint8_t *)SKRY_get_line(img8, y) + x, ref_block_size, ref_block_size,
                                                       SKRY_get_line_stride_in_bytes(img8), 4);

                if (qual > best_qual)
                {
                    best_qual = qual;
                    result.x = x + ref_block_size/2;
                    result.y = y + ref_block_size/2;
                }
            }
        }

    if (img8 != image)
        SKRY_free_image(img8);
    return result;
}

int SKRY_is_anchor_valid(const SKRY_ImgAlignment *img_algn, size_t anchor_idx)
{
    assert(anchor_idx < DA_SIZE(img_algn->anchors));
    return img_algn->anchors.data[anchor_idx].is_valid;
}

enum SKRY_img_alignment_method SKRY_get_alignment_method(const struct SKRY_img_alignment *img_algn)
{
    return img_algn->algn_method;
}

/// Returns current centroid position
struct SKRY_point SKRY_get_current_centroid_pos(const SKRY_ImgAlignment *img_algn)
{
    return img_algn->centroid_pos;
}
