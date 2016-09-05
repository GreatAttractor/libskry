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
    Quality estimation implementation.
*/

#include <assert.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

#include <skry/defs.h>
#include <skry/image.h>
#include <skry/img_align.h>
#include <skry/imgseq.h>
#include <skry/quality.h>

#include "utils/dnarray.h"
#include "utils/filters.h"
#include "utils/logging.h"
#include "utils/misc.h"


struct qual_est_area
{
    struct SKRY_rect rect; ///< Area's boundaries within the images' intersection

    /** Contains a fragment of the image in which the estimation area
        has the highest quality. The fragment is a rectangle 3x wider
        and higher than 'rect', with 'rect' in the middle;
        for areas near the border of intersection it may be smaller. */
    SKRY_Image *ref_block;

    /// Position of 'ref_block' within the images' intersection
    struct SKRY_point ref_block_pos;
};

struct area_quality_summary_t
{
    SKRY_quality_t min, max, avg;
    unsigned best_img_idx;
};

struct SKRY_quality_estimation
{
    size_t num_areas; ///< Number of elements in 'area_defs'

    // Number of quality est. areas that span the images' intersection
    // horizontally and vertically; 'num_areas' is their product
    size_t num_areas_horz;
    size_t num_areas_vert;

    /// Most areas are squares with sides of 'area_size' length (border areas may be smaller)
    unsigned area_size;

    struct qual_est_area *area_defs; ///< Array of quality estimation area definitions

    int is_estimation_complete;
    const SKRY_ImgAlignment *img_algn;

    /// 2-dimensional array of areas' quality in all images
    /** dimension 1: images, element count: img_algn->img_seq->num_active_images
        dimension 2: areas,  element count: num_areas  */
    SKRY_quality_t *area_quality;

    /// Array of min, max and average quality for each area, contains 'num_areas' elements
    struct area_quality_summary_t *qual_summary;

    struct
    {
        struct
        {
            SKRY_quality_t avg;
            SKRY_quality_t max_avg;
            SKRY_quality_t min_nonzero_avg;
        } area;

        struct
        {
            size_t best_img_idx;
            SKRY_quality_t best_quality;
        } image;
    } overall_quality;

    /// Array of images' overall quality (element count: img_algn->img_seq->num_images)
    SKRY_quality_t *img_quality;

    unsigned box_blur_radius;

    int first_step_complete;

    struct
    {
        struct
        {
            double start; ///< Stored at the beginning of SKRY_init_quality_est()
            double total_sec; ///< Difference between end of the last step and 'start'
        } time;
    } statistics;
};

int SKRY_is_qual_est_complete(const SKRY_QualityEstimation *qual_est)
{
    return qual_est->is_estimation_complete;
}

/// Returns ceil(a/b) for integer a, b
#define DIV_CEIL(a, b) (((a) + (b) - 1)/(b))

#define FAIL_ON_NULL(ptr)                \
    if (!(ptr))                          \
    {                                    \
        SKRY_free_quality_est(qual_est); \
        return 0;                        \
    }

/// Returns null if out of memory
SKRY_QualityEstimation *SKRY_init_quality_est(
        const SKRY_ImgAlignment *img_algn,
        /// Aligned image sequence will be divided into areas of this size for quality estimation
        unsigned estimation_area_size,
        /// Corresponds with box blur radius used for quality estimation
        unsigned detail_scale)
{
    assert(img_algn != 0);
    size_t num_active_imgs = SKRY_get_active_img_count(SKRY_get_img_seq(img_algn));
    assert(num_active_imgs > 0);
    assert(estimation_area_size != 0);
    assert(SKRY_is_img_alignment_complete(img_algn));
    assert(detail_scale > 0);

    SKRY_QualityEstimation *qual_est = malloc(sizeof *qual_est);
    if (!qual_est)
        return 0;

    // Sets all pointer fields to null, so it is safe to call SKRY_free_quality_est()
    // via FAIL_ON_NULL() in case one of allocation fails.
    *qual_est = (SKRY_QualityEstimation) { 0 };

    qual_est->area_size = estimation_area_size;
    qual_est->statistics.time.start = SKRY_clock_sec();
    qual_est->box_blur_radius = detail_scale;
    qual_est->img_algn = img_algn;
    qual_est->img_quality = malloc(num_active_imgs * sizeof(SKRY_quality_t));
    FAIL_ON_NULL(qual_est->img_quality);

    unsigned i_width, i_height;
    SKRY_get_intersection_size(img_algn, &i_width, &i_height);

    // Divide the aligned images' intersection into quality estimation areas.
    // Each area is a square of 'estimation_area_size' pixels. If there are
    // left-overs, assign them to appropriately smaller areas at the intersection's
    // right and bottom border.

    // Number of areas in the aligned images' intersection in horizontal and vertical direction
    qual_est->num_areas_horz = DIV_CEIL(i_width, estimation_area_size);
    qual_est->num_areas_vert = DIV_CEIL(i_height, estimation_area_size);

    qual_est->num_areas = qual_est->num_areas_horz * qual_est->num_areas_vert;
    qual_est->area_quality = malloc(num_active_imgs * qual_est->num_areas * sizeof(SKRY_quality_t));
    FAIL_ON_NULL(qual_est->area_quality);
    qual_est->qual_summary = malloc(qual_est->num_areas * sizeof(*qual_est->qual_summary));
    FAIL_ON_NULL(qual_est->qual_summary);
    for (size_t i = 0; i < qual_est->num_areas; i++)
        qual_est->qual_summary[i] =
            (struct area_quality_summary_t) { .min = FLT_MAX, .max = 0, .best_img_idx = 0 };

    // 2-dimensional indexing of 'qual_est->area_defs'
#define IDX(col, row) ((col) + (row) * qual_est->num_areas_horz)

    qual_est->area_defs = malloc(qual_est->num_areas * sizeof(*qual_est->area_defs));
    FAIL_ON_NULL(qual_est->area_defs);

    unsigned width_rem = i_width % estimation_area_size;
    unsigned height_rem = i_height % estimation_area_size;

    for (unsigned y = 0; y < i_height/estimation_area_size; y++)
    {
        for (unsigned x = 0; x < i_width/estimation_area_size; x++)
            qual_est->area_defs[IDX(x, y)] =
                (struct qual_est_area) { .rect.x = x*estimation_area_size,
                                         .rect.y = y*estimation_area_size,
                                         .rect.width = estimation_area_size,
                                         .rect.height = estimation_area_size,
                                         .ref_block = 0 };

        // Additional smaller area on the right
        if (width_rem != 0)
            qual_est->area_defs[IDX(qual_est->num_areas_horz - 1, y)] =
                (struct qual_est_area) { .rect.x = i_width - width_rem,
                                         .rect.y = y*estimation_area_size,
                                         .rect.width = width_rem,
                                         .rect.height = estimation_area_size,
                                         .ref_block = 0 };
    }

    // Row of additional smaller areas at the bottom
    if (height_rem != 0)
    {
        for (unsigned x = 0; x < i_width/estimation_area_size; x++)
            qual_est->area_defs[IDX(x, qual_est->num_areas_vert - 1)] =
                (struct qual_est_area) { .rect.x = x*estimation_area_size,
                                         .rect.y = i_height - height_rem,
                                         .rect.width = estimation_area_size,
                                         .rect.height = height_rem,
                                         .ref_block = 0 };

        if (width_rem != 0)
            qual_est->area_defs[IDX(qual_est->num_areas_horz - 1, qual_est->num_areas_vert - 1)] =
                (struct qual_est_area) { .rect.x = i_width - width_rem,
                                         .rect.y = i_height - height_rem,
                                         .rect.width = width_rem,
                                         .rect.height = height_rem,
                                         .ref_block = 0 };

    }

    SKRY_seek_start(SKRY_get_img_seq(img_algn));

    return qual_est;

#undef IDX
}

/// Creates reference blocks for the quality estimation areas (using images where the areas have the best quality)
static
enum SKRY_result create_reference_blocks(SKRY_QualityEstimation *qual_est)
{
    enum SKRY_result result = SKRY_SUCCESS;

    const SKRY_ImgAlignment *img_algn = qual_est->img_algn;
    SKRY_ImgSequence *img_seq = SKRY_get_img_seq(img_algn);

    unsigned i_width, i_height;
    SKRY_get_intersection_size(img_algn, &i_width, &i_height);
    struct SKRY_point intrs_ofs = SKRY_get_intersection_ofs(img_algn);

    SKRY_seek_start(img_seq);
    do
    {
        unsigned curr_img_idx = SKRY_get_curr_img_idx_within_active_subset(img_seq);
        SKRY_Image *curr_img = 0;

        for (size_t i = 0; i < qual_est->num_areas; i++)
        {
            if (qual_est->qual_summary[i].best_img_idx == curr_img_idx)
            {
                if (!curr_img)
                {
                    curr_img = SKRY_get_curr_img(img_seq, &result);
                    if (!curr_img)
                        return result;

                    SKRY_Image *img8 = SKRY_convert_pix_fmt(curr_img, SKRY_PIX_MONO8, SKRY_DEMOSAIC_SIMPLE);
                    if (!img8)
                    {
                        SKRY_free_image(curr_img);
                        return SKRY_OUT_OF_MEMORY;
                    }

                    SKRY_free_image(curr_img);
                    curr_img = img8;
                }
                struct SKRY_point curr_img_ofs = SKRY_get_image_ofs(img_algn, curr_img_idx);
                struct qual_est_area *area = &qual_est->area_defs[i];
                // Position of 'area' in 'curr_img'
                struct SKRY_point curr_area_pos =
                    { .x = intrs_ofs.x + curr_img_ofs.x + area->rect.x,
                      .y = intrs_ofs.y + curr_img_ofs.y + area->rect.y };

                int asize = (int)qual_est->area_size;

                // The desired size of ref. block is 3*asize in width and height;
                // make sure it fits in 'curr_img'.
                struct SKRY_rect img_fragment =
                { .x = SKRY_MAX(0, curr_area_pos.x + (int)area->rect.width/2 - 3*asize/2),
                  .y = SKRY_MAX(0, curr_area_pos.y + (int)area->rect.height/2 - 3*asize/2) };

                img_fragment.width = SKRY_MIN((int)SKRY_get_img_width(curr_img) - img_fragment.x, 3*asize);
                img_fragment.height = SKRY_MIN((int)SKRY_get_img_height(curr_img) - img_fragment.y, 3*asize);

                area->ref_block_pos = (struct SKRY_point)
                    { .x = img_fragment.x - intrs_ofs.x - curr_img_ofs.x,
                      .y = img_fragment.y - intrs_ofs.y - curr_img_ofs.y };
                area->ref_block = SKRY_new_image(img_fragment.width, img_fragment.height, SKRY_PIX_MONO8, 0, 0);
                if (!area->ref_block)
                    return SKRY_OUT_OF_MEMORY;

                SKRY_resize_and_translate(curr_img, area->ref_block,
                                          img_fragment.x, img_fragment.y,
                                          img_fragment.width,
                                          img_fragment.height,
                                          0, 0, 0);
            }
        }

        curr_img = SKRY_free_image(curr_img);
    } while (SKRY_seek_next(img_seq) == SKRY_SUCCESS);

    return result;
}

/// Returns null
SKRY_QualityEstimation *SKRY_free_quality_est(SKRY_QualityEstimation *qual_est)
{
    if (qual_est)
    {
        free(qual_est->area_quality);
        free(qual_est->qual_summary);
        free(qual_est->img_quality);
        if (qual_est->area_defs)
        {
            for (size_t i = 0; i < qual_est->num_areas; i++)
            {
                SKRY_free_image(qual_est->area_defs[i].ref_block);
            }
            free(qual_est->area_defs);
        }
        free(qual_est);
    }
    return 0;
}

static
enum SKRY_result on_final_step(SKRY_QualityEstimation *qual_est)
{
    unsigned num_active_imgs = SKRY_get_active_img_count(SKRY_get_img_seq(qual_est->img_algn));

    qual_est->overall_quality.area.max_avg = 0;
    qual_est->overall_quality.area.min_nonzero_avg = FLT_MAX;

    double overall_sum = 0;

    for (size_t i = 0; i < qual_est->num_areas; i++)
    {
        SKRY_quality_t quality_sum = 0;
        for (unsigned j = 0; j < num_active_imgs; j++)
        {
            SKRY_quality_t current_q = qual_est->area_quality[i + j * qual_est->num_areas];
            quality_sum += current_q;
            overall_sum += current_q;
        }

        SKRY_quality_t qavg = quality_sum / num_active_imgs;
        qual_est->qual_summary[i].avg = qavg;

        LOG_MSG(SKRY_LOG_QUALITY, "Area %6zu, avg. quality = %.3f", i, qavg);

        if (qavg > qual_est->overall_quality.area.max_avg)
            qual_est->overall_quality.area.max_avg = qavg;

        if (qavg > 0 && qavg < qual_est->overall_quality.area.min_nonzero_avg)
            qual_est->overall_quality.area.min_nonzero_avg = qavg;
    }

    qual_est->overall_quality.area.avg = overall_sum / (qual_est->num_areas * num_active_imgs);
    create_reference_blocks(qual_est);
    qual_est->is_estimation_complete = 1;

    qual_est->statistics.time.total_sec = SKRY_clock_sec() - qual_est->statistics.time.start;
    LOG_MSG(SKRY_LOG_QUALITY, "Processing time: %.3f s", qual_est->statistics.time.total_sec);

    return SKRY_LAST_STEP;
}

/// Returns SKRY_SUCCESS (i.e. more steps left to do), SKRY_LAST_STEP (no more steps) or an error
enum SKRY_result SKRY_quality_est_step(SKRY_QualityEstimation *qual_est)
{
    enum SKRY_result result;

    SKRY_ImgSequence *img_seq = SKRY_get_img_seq(qual_est->img_algn);
    assert(img_seq);

    if (qual_est->first_step_complete)
    {
        result = SKRY_seek_next(img_seq);
        if (SKRY_NO_MORE_IMAGES == result)
            return on_final_step(qual_est);
        else if (result != SKRY_SUCCESS)
            return result;
    }

    size_t curr_img_idx = SKRY_get_curr_img_idx_within_active_subset(img_seq);
    SKRY_Image *curr_img = SKRY_get_curr_img(img_seq, &result);
    if (result != SKRY_SUCCESS)
        return result;

    if (SKRY_get_img_pix_fmt(curr_img) != SKRY_PIX_MONO8)
    {
        SKRY_Image *img_mono8 = SKRY_convert_pix_fmt(curr_img, SKRY_PIX_MONO8, SKRY_DEMOSAIC_SIMPLE);
        SKRY_free_image(curr_img);
        curr_img = img_mono8;
    }

    // Ptr to the row containing qualities of the current image's quality estimation areas
    SKRY_quality_t *curr_img_area_quality = &qual_est->area_quality[curr_img_idx * qual_est->num_areas];
    SKRY_quality_t curr_img_qual = 0;

    struct SKRY_point alignment_ofs = SKRY_get_image_ofs(qual_est->img_algn, curr_img_idx);
    struct SKRY_point intrs_ofs = SKRY_get_intersection_ofs(qual_est->img_algn);
    ptrdiff_t line_stride = SKRY_get_line_stride_in_bytes(curr_img);

    #pragma omp parallel for \
     reduction(+:curr_img_qual)
    for (size_t i = 0; i < qual_est->num_areas; i++)
    {
        struct SKRY_rect *area = &qual_est->area_defs[i].rect;

        SKRY_quality_t aqual = estimate_quality(
            (uint8_t *)SKRY_get_line(curr_img, 0) + area->x + intrs_ofs.x + alignment_ofs.x +
              line_stride * (area->y + intrs_ofs.y + alignment_ofs.y),
            area->width, area->height, line_stride, qual_est->box_blur_radius);

        curr_img_qual += aqual;
        curr_img_area_quality[i] = aqual;
        if (aqual > qual_est->qual_summary[i].max)
        {
            qual_est->qual_summary[i].max = aqual;
            qual_est->qual_summary[i].best_img_idx = curr_img_idx;
        }
        if (aqual < qual_est->qual_summary[i].min)
        {
            qual_est->qual_summary[i].min = aqual;
        }
    }

    qual_est->img_quality[curr_img_idx] = curr_img_qual;

    if (curr_img_qual > qual_est->overall_quality.image.best_quality)
    {
        qual_est->overall_quality.image.best_quality = curr_img_qual;
        qual_est->overall_quality.image.best_img_idx = curr_img_idx;
    }

    SKRY_free_image(curr_img);

    if (!qual_est->first_step_complete)
        qual_est->first_step_complete = 1;

    return SKRY_SUCCESS;
}

size_t SKRY_get_qual_est_num_areas(const SKRY_QualityEstimation *qual_est)
{
    return qual_est->num_areas;
}

/// Fills 'qual_array' with overall quality values of subsequent images
void SKRY_get_images_quality(const SKRY_QualityEstimation *qual_est,
    /// Element count = number of active images in img. sequence associated with 'qual_est'
    SKRY_quality_t qual_array[])
{
    memcpy(qual_array, qual_est->img_quality,
        sizeof(SKRY_quality_t) * SKRY_get_active_img_count(SKRY_get_img_seq(qual_est->img_algn)));
}

SKRY_quality_t SKRY_get_avg_area_quality(const SKRY_QualityEstimation *qual_est, size_t area_idx)
{
    return qual_est->qual_summary[area_idx].avg;
}

void SKRY_get_area_quality_summary(const SKRY_QualityEstimation *qual_est, size_t area_idx,
                                   SKRY_quality_t *qmin,
                                   SKRY_quality_t *qmax,
                                   SKRY_quality_t *qavg)
{
    *qmin = qual_est->qual_summary[area_idx].min;
    *qmax = qual_est->qual_summary[area_idx].max;
    *qavg = qual_est->qual_summary[area_idx].avg;
}

/// Returns the image alignment object associated with 'qual_est'
const SKRY_ImgAlignment *SKRY_get_img_align(const SKRY_QualityEstimation *qual_est)
{
    return qual_est->img_algn;
}

SKRY_quality_t SKRY_get_area_quality(const SKRY_QualityEstimation *qual_est, size_t area_idx, size_t img_idx)
{
    return qual_est->area_quality[area_idx + img_idx * qual_est->num_areas];
}

SKRY_quality_t SKRY_get_best_avg_area_quality(const SKRY_QualityEstimation *qual_est)
{
    return qual_est->overall_quality.area.max_avg;
}

size_t SKRY_get_area_idx_at_pos(const SKRY_QualityEstimation *qual_est,
                                /// Position within images' intersection
                                struct SKRY_point pos)
{
    // See SKRY_init_quality_est() for how the estimation areas are placed within the images' intersection.

    size_t col = pos.x / qual_est->area_size;
    size_t row = pos.y / qual_est->area_size;

    return row*qual_est->num_areas_horz + col;
}

/// Returns a square image to be used as reference block; returns null if out of memory
SKRY_Image *SKRY_create_reference_block(
    const SKRY_QualityEstimation *qual_est,
    /// Center of the reference block (within images' intersection)
    struct SKRY_point pos,
    /// Desired width & height; the result may be smaller than this (but always a square)
    unsigned blk_size)
{
    assert(qual_est->is_estimation_complete);
    const struct qual_est_area *area = &qual_est->area_defs[SKRY_get_area_idx_at_pos(qual_est, pos)];

    unsigned area_refb_w = SKRY_get_img_width(area->ref_block),
             area_refb_h = SKRY_get_img_height(area->ref_block);

    /* Caller is requesting a square block of 'blk_size'. We need to copy it from 'area->ref_block'.
       Determine the maximum size of square we can return (the square must be centered on 'pos'
       and fit in 'area->ref_block'):

       +----------images' intersection-------------...
       |
       |    area->ref_block_pos
       |                 *-----------area->ref_block-------+
       .                 |                                 |
       .                 |                    +============+
       .                 |                    |            |
                         |                    |            |
                         |      2*result_size{|     *pos   |
                         |                    |            |
                         |                    |            |
                         |                    +============+
                         |                                 |
                         ...


    */
    int result_size = blk_size;
    result_size = SKRY_MIN(result_size, 2*(pos.x - area->ref_block_pos.x));
    result_size = SKRY_MIN(result_size, 2*(pos.y - area->ref_block_pos.y));
    result_size = SKRY_MIN(result_size, 2*(area->ref_block_pos.x + (int)area_refb_w - pos.x));
    result_size = SKRY_MIN(result_size, 2*(area->ref_block_pos.y + (int)area_refb_h - pos.y));

    SKRY_Image *result = SKRY_new_image(result_size, result_size, SKRY_PIX_MONO8, 0, 0);
    SKRY_resize_and_translate(area->ref_block, result,
                              pos.x - area->ref_block_pos.x - result_size/2,
                              pos.y - area->ref_block_pos.y - result_size/2,
                              result_size, result_size,
                              0, 0, 0);
    return result;
}

struct SKRY_point SKRY_get_qual_est_area_center(const SKRY_QualityEstimation *qual_est, size_t area_idx)
{
    struct SKRY_rect arect = qual_est->area_defs[area_idx].rect;
    return (struct SKRY_point)
            { .x = arect.x + arect.width/2,
              .y = arect.y + arect.height/2 };
}

SKRY_quality_t SKRY_get_min_nonzero_avg_area_quality(const SKRY_QualityEstimation *qual_est)
{
    return qual_est->overall_quality.area.min_nonzero_avg;
}

SKRY_quality_t SKRY_get_overall_avg_area_quality(const SKRY_QualityEstimation *qual_est)
{
    return qual_est->overall_quality.area.avg;
}
