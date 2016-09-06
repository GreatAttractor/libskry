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
    Reference point alignment implementation.
*/

#include <inttypes.h>
#include <float.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include <skry/defs.h>
#include <skry/image.h>
#include <skry/imgseq.h>
#include <skry/quality.h>
#include <skry/ref_pt_align.h>
#include <skry/skry.h>
#include <skry/triangulation.h>

#include "utils/dnarray.h"
#include "utils/logging.h"
#include "utils/match.h"
#include "utils/misc.h"


// Values in pixels
#define BLOCK_MATCHING_INITIAL_SEARCH_STEP 2
#define DEFAULT_SPACING 40

enum { NOT_UPDATED = 0, UPDATED = 1};

struct reference_point
{
    size_t qual_est_area;  ///< Index of the associated quality estimation area; may be SKRY_EMPTY
    SKRY_Image *ref_block; ///< Reference block used for block matching

    struct
    {
        struct SKRY_point pos;
        int is_valid; ///< True if the quality criteria for the image are met
    } *positions; ///< Array of positions in every active image

    size_t last_valid_pos_idx; ///< Initially equals SKRY_EMPTY
    struct
    {
        double sq_len, len;
    } last_transl_vec;
};

/// Sum of reference points translation vector lengths in an image
struct tvec_sum
{
    double sum_len, sum_sq_len;
    size_t num_terms;
};

/** Number of the most recent images used to keep a "sliding window"
    average of reference points translation vector lengths. */
#define TVEC_SUM_NUM_IMAGES 10

struct SKRY_ref_pt_alignment
{
    const SKRY_QualityEstimation *qual_est;

    enum SKRY_quality_criterion quality_criterion;

    /// Interpreted according to 'quality_criterion'
    unsigned quality_threshold;

    DA_DECLARE(struct reference_point) reference_pts;

    /// Delaunay triangulation of the reference points
    struct SKRY_triangulation *triangulation;

    /// Spacing in pixels between reference points
    unsigned spacing;

    /// Size of (square) reference blocks used for block matching
    /** Some ref. points (near image borders) may have smaller blocks (but always square). */
    unsigned ref_block_size;

    /// Array of boolean flags indicating if a ref. point has been updated during the current step
    uint8_t *update_flags;

    struct triangle_quality
    {
        // Min and max sum of triangle vertices' quality in an image
        SKRY_quality_t qmin, qmax;

        /** element count = num. of active images;
            i-th element: sorted quality index (0==worst) of the triangle in i-th image */
        size_t *sorted_idx;
    } *tri_quality; ///< Element count: number of triangles in 'triangulation'

    int is_complete;

    /** Summary (for all points) of the translation vectors between subsequent
        "valid" positions within the most recent TVEC_SUM_NUM_IMAGES.
        Used for clipping outliers in update_ref_pt_positions(). */
    struct
    {
        struct tvec_sum tvec_img_sum[TVEC_SUM_NUM_IMAGES];
        size_t next_entry; ///< Index in 'tvec_img_sum' to store the next sum at
    } t_vectors;

    struct
    {
        uint64_t num_valid_positions;
        uint64_t num_rejected_positions;
        struct
        {
            double start; ///< Stored at the beginning of SKRY_init_ref_pt_alignment()
            double total_sec; ///< Difference between end of the last step and 'start'
        } time;
    } statistics;
};

/** Makes sure that for every triangle there is at least 1 image
    where all 3 vertices are "valid". */
static
void ensure_tris_are_valid(SKRY_RefPtAlignment *ref_pt_align)
{
    unsigned num_active_imgs = SKRY_get_active_img_count(SKRY_get_img_seq(SKRY_get_img_align(ref_pt_align->qual_est)));

    const struct SKRY_triangle *triangles = SKRY_get_triangles(ref_pt_align->triangulation);

    for (size_t tri_idx = 0; tri_idx < SKRY_get_num_triangles(ref_pt_align->triangulation); tri_idx++)
    {
        struct reference_point *refp[3] =
            { &ref_pt_align->reference_pts.data[triangles[tri_idx].v0],
              &ref_pt_align->reference_pts.data[triangles[tri_idx].v1],
              &ref_pt_align->reference_pts.data[triangles[tri_idx].v2] };

        // Best quality and associated img index where the triangle's vertices are not all "valid"
        SKRY_quality_t best_tri_qual = 0;
        size_t best_tri_qual_img_idx = SKRY_EMPTY;

        for (unsigned img_idx = 0; img_idx < num_active_imgs; img_idx++)
        {
            if (refp[0]->positions[img_idx].is_valid &&
                refp[1]->positions[img_idx].is_valid &&
                refp[2]->positions[img_idx].is_valid)
            {
                best_tri_qual_img_idx = SKRY_EMPTY;
                break;
            }
            else
            {
                SKRY_quality_t tri_qual = 0;
                for (int i = 0; i < 3; i++)
                    if (refp[i]->qual_est_area != SKRY_EMPTY)
                        tri_qual += SKRY_get_area_quality(ref_pt_align->qual_est, refp[i]->qual_est_area, img_idx);

                if (tri_qual > best_tri_qual)
                {
                    best_tri_qual = tri_qual;
                    best_tri_qual_img_idx = img_idx;
                }
            }
        }

        if (best_tri_qual_img_idx != SKRY_EMPTY)
        {
            // The triangle's vertices turned out not to be simultaneously "valid" in any image,
            // which is required (in at least one image) during stacking phase.
            //
            // Mark them "valid" anyway in the image where their quality sum is highest.
            for (int i = 0; i < 3; i++)
                refp[i]->positions[best_tri_qual_img_idx].is_valid = 1;

            LOG_MSG(SKRY_LOG_REF_PT_ALIGNMENT,
                "Triangle %zu not valid in any image, forcing to valid in image %zu.",
                tri_idx, best_tri_qual_img_idx);
        }
    }
}

struct img_idx_to_quality
{
    size_t img_idx;
    SKRY_quality_t quality;
};

static
int comparator(const void *val1, const void *val2)
{
    if (((struct img_idx_to_quality *)val1)->quality < ((struct img_idx_to_quality *)val2)->quality)
        return -1;
    else if (((struct img_idx_to_quality *)val1)->quality > ((struct img_idx_to_quality *)val2)->quality)
        return 1;
    else
        return 0;
}

/// Returns null if out of memory
static
void *calc_triangle_quality(SKRY_RefPtAlignment *ref_pt_align)
{
    size_t num_active_imgs = SKRY_get_active_img_count(SKRY_get_img_seq(SKRY_get_img_align(ref_pt_align->qual_est)));

    struct img_idx_to_quality *img_to_qual = malloc(num_active_imgs * sizeof(*img_to_qual));

    if (!img_to_qual)
        return 0;

    for (size_t i = 0; i < SKRY_get_num_triangles(ref_pt_align->triangulation); i++)
    {
        const struct SKRY_triangle *tri = &SKRY_get_triangles(ref_pt_align->triangulation)[i];
        const struct reference_point *tri_points[3] =
            { &ref_pt_align->reference_pts.data[tri->v0],
              &ref_pt_align->reference_pts.data[tri->v1],
              &ref_pt_align->reference_pts.data[tri->v2] };

        ref_pt_align->tri_quality[i].qmin = FLT_MAX;
        ref_pt_align->tri_quality[i].qmax = 0;
        ref_pt_align->tri_quality[i].sorted_idx = malloc(num_active_imgs * sizeof(*ref_pt_align->tri_quality[i].sorted_idx));
        if (!ref_pt_align->tri_quality[i].sorted_idx)
        {
            free(img_to_qual);
            return 0;
        }

        for (size_t img_idx = 0; img_idx < num_active_imgs; img_idx++)
        {
            SKRY_quality_t qsum = 0;

            for (int j = 0; j < 3; j++)
                if (tri_points[j]->qual_est_area != SKRY_EMPTY)
                {
                    qsum += SKRY_get_area_quality(ref_pt_align->qual_est, tri_points[j]->qual_est_area, img_idx);
                }
                // else it is one of the fixed boundary points; does not affect triangle's quality

            if (qsum < ref_pt_align->tri_quality[i].qmin)
            {
                ref_pt_align->tri_quality[i].qmin = qsum;
            }
            if (qsum > ref_pt_align->tri_quality[i].qmax)
            {
                ref_pt_align->tri_quality[i].qmax = qsum;
            }

            img_to_qual[img_idx].img_idx = img_idx;
            img_to_qual[img_idx].quality = qsum;
        }

        qsort(img_to_qual, num_active_imgs, sizeof(*img_to_qual), comparator);
        // See comment at 'sorted_idx' declaration for details
        for (size_t img_idx = 0; img_idx < num_active_imgs; img_idx++)
            ref_pt_align->tri_quality[i].sorted_idx[img_to_qual[img_idx].img_idx] = img_idx;
    }

    free(img_to_qual);

    return ref_pt_align; // non-null result is not used by caller
}

static
void update_ref_pt_positions(
    SKRY_RefPtAlignment *ref_pt_align,
    const SKRY_Image *img, size_t img_idx,
    size_t num_active_imgs,
    enum SKRY_quality_criterion quality_criterion,
    unsigned quality_threshold,
    struct SKRY_rect intersection,
    struct SKRY_point img_alignment_ofs)
{
     /* Reminder: positions of reference points and quality estimation areas are specified
               within the intersection of all images after alignment. Therefore all accesses
               to the current image 'img' have to take it into account and use 'intersection'
               and 'img_alignment_ofs' to apply proper offsets. */

    memset(ref_pt_align->update_flags, NOT_UPDATED, DA_SIZE(ref_pt_align->reference_pts) * sizeof(*ref_pt_align->update_flags));

    struct tvec_sum curr_step_tvec = { 0 };

    #pragma omp parallel for
    for (size_t tri_idx = 0; tri_idx < SKRY_get_num_triangles(ref_pt_align->triangulation); tri_idx++)
    {
        // Update positions of reference points belonging to triangle [i] iff the sum
        // of their quality est. areas is at least the specified threshold
        // (relative to the min and max sum).

        const struct SKRY_triangle *tri = &SKRY_get_triangles(ref_pt_align->triangulation)[tri_idx];

        SKRY_quality_t qsum = 0;

        struct
        {
            size_t p_idx;
            struct reference_point *ref_pt;
        } tri_pts[3] =
            { { .p_idx = tri->v0, .ref_pt = &ref_pt_align->reference_pts.data[tri->v0] },
              { .p_idx = tri->v1, .ref_pt = &ref_pt_align->reference_pts.data[tri->v1] },
              { .p_idx = tri->v2, .ref_pt = &ref_pt_align->reference_pts.data[tri->v2] } };

        for (int i = 0; i < 3; i++)
            if (tri_pts[i].ref_pt->qual_est_area != SKRY_EMPTY)
                qsum += SKRY_get_area_quality(ref_pt_align->qual_est, tri_pts[i].ref_pt->qual_est_area, img_idx);

        const struct triangle_quality *tri_q = &ref_pt_align->tri_quality[tri_idx];

        int is_quality_sufficient = 0;
        switch (quality_criterion)
        {
            case SKRY_PERCENTAGE_BEST:
                is_quality_sufficient = (tri_q->sorted_idx[img_idx] >= 0.01f * (100 - quality_threshold) * num_active_imgs);
                break;

            case SKRY_MIN_REL_QUALITY:
                is_quality_sufficient = (qsum >= tri_q->qmin + 0.01f * quality_threshold * (tri_q->qmax - tri_q->qmin));
                break;

            case SKRY_NUMBER_BEST:
                is_quality_sufficient = quality_threshold > num_active_imgs ? 1 :
                                            (tri_q->sorted_idx[img_idx] >= num_active_imgs - quality_threshold);
                break;
        }

        for (int i = 0; i < 3; i++)
        {
            struct reference_point *ref_pt = tri_pts[i].ref_pt;
            if (SKRY_EMPTY == ref_pt->qual_est_area || UPDATED == ref_pt_align->update_flags[tri_pts[i].p_idx])
                continue;

            int found_new_valid_pos = 0;

            if (is_quality_sufficient)
            {
                int is_first_update = 0;

                if (0 == ref_pt->ref_block)
                {
                    // This is the first time this point meets the quality criteria.
                    // Initialize its reference block.
                    if (img_idx > 0)
                    {
                        // Point's position in the current image has not been filled in yet, do it now
                        ref_pt->positions[img_idx].pos = ref_pt->positions[img_idx-1].pos;
                    }

                    ref_pt->ref_block = SKRY_create_reference_block(ref_pt_align->qual_est,
                                                                    ref_pt->positions[img_idx].pos,
                                                                    ref_pt_align->ref_block_size);


                    is_first_update = 1;
                }

                struct SKRY_point new_pos_in_img;
                struct SKRY_point current_ref_pos;
                current_ref_pos = ref_pt->positions[(0 == img_idx) ? 0 : (img_idx-1)].pos;

                find_matching_position(
                    (struct SKRY_point) { .x = current_ref_pos.x + intersection.x + img_alignment_ofs.x,
                                          .y = current_ref_pos.y + intersection.y + img_alignment_ofs.y },
                    ref_pt->ref_block, img,
                    ref_pt_align->spacing/2,
                    BLOCK_MATCHING_INITIAL_SEARCH_STEP, &new_pos_in_img);

                struct SKRY_point new_pos = { .x = new_pos_in_img.x - intersection.x - img_alignment_ofs.x,
                                              .y = new_pos_in_img.y - intersection.y - img_alignment_ofs.y };

                // Additional rejection criterion: ignore the first position update if the new pos. is too distant.
                // Otherwise the point would be moved too far at the very start of the ref. point alignment
                // phase and might not recover, i.e. its subsequent position updates might be getting rejected
                // by the additional check after the current outermost 'for' loop.
                if (!is_first_update
                    || SKRY_SQR(new_pos.x - current_ref_pos.x) + SKRY_SQR(new_pos.y - current_ref_pos.y) <= SKRY_SQR((int)ref_pt_align->spacing/6 /*TODO: make it adaptive somehow? or use the current avg. deviation*/))
                {
                    ref_pt->positions[img_idx].pos =
                        (struct SKRY_point) { .x = new_pos_in_img.x - intersection.x - img_alignment_ofs.x,
                                              .y = new_pos_in_img.y - intersection.y - img_alignment_ofs.y };

                    ref_pt->positions[img_idx].is_valid = 1;

                    if (ref_pt->last_valid_pos_idx != SKRY_EMPTY)
                    {
                        ref_pt->last_transl_vec.sq_len = SKRY_SQR(ref_pt->positions[img_idx].pos.x - ref_pt->positions[ref_pt->last_valid_pos_idx].pos.x) +
                                                         SKRY_SQR(ref_pt->positions[img_idx].pos.y - ref_pt->positions[ref_pt->last_valid_pos_idx].pos.y);
                        ref_pt->last_transl_vec.len = sqrt(ref_pt->last_transl_vec.sq_len);

                        curr_step_tvec.sum_len    += ref_pt->last_transl_vec.len;
                        curr_step_tvec.sum_sq_len += ref_pt->last_transl_vec.sq_len;
                        curr_step_tvec.num_terms++;
                    }

                    found_new_valid_pos = 1;
                }
            }

            if (!found_new_valid_pos)
            {
                ref_pt->positions[img_idx].is_valid = 0;
                if (img_idx > 0)
                    ref_pt->positions[img_idx].pos = ref_pt->positions[img_idx-1].pos;
            }

            ref_pt_align->update_flags[tri_pts[i].p_idx] = UPDATED;
        }
    }

    size_t prev_num_terms  = 0;
    double prev_sum_len    = 0;
    double prev_sum_sq_len = 0;
    for (size_t i = 0; i < TVEC_SUM_NUM_IMAGES; i++)
    {
        prev_num_terms  += ref_pt_align->t_vectors.tvec_img_sum[i].num_terms;
        prev_sum_len    += ref_pt_align->t_vectors.tvec_img_sum[i].sum_len;
        prev_sum_sq_len += ref_pt_align->t_vectors.tvec_img_sum[i].sum_sq_len;
    }

    if (curr_step_tvec.num_terms > 0)
    {
        double sum_len_avg    = (prev_sum_len + curr_step_tvec.sum_len)       / (prev_num_terms + curr_step_tvec.num_terms);
        double sum_sq_len_avg = (prev_sum_sq_len + curr_step_tvec.sum_sq_len) / (prev_num_terms + curr_step_tvec.num_terms);

        double std_deviation = (sum_sq_len_avg - SKRY_SQR(sum_len_avg) >= 0) ?
                               sqrt(sum_sq_len_avg - SKRY_SQR(sum_len_avg))
                               : 0;

        // Iterate over the points found "valid" in the current step and if their current translation
        // lies too far from the "sliding window" translation average, clear their "valid" flag.
        for (size_t i = 0; i < DA_SIZE(ref_pt_align->reference_pts); i++)
        {
            struct reference_point *ref_pt = &ref_pt_align->reference_pts.data[i];
            if (ref_pt->qual_est_area != SKRY_EMPTY && ref_pt->positions[img_idx].is_valid
                && ref_pt->last_transl_vec.len > sum_len_avg + 1.5* std_deviation)
            {
                assert(img_idx); // Cannot happen for img_idx==0, because then the point's last translation vector must be zero
                ref_pt->positions[img_idx].is_valid = 0;
                ref_pt->positions[img_idx].pos = ref_pt->positions[img_idx-1].pos;

                curr_step_tvec.sum_len -= ref_pt->last_transl_vec.len;
                curr_step_tvec.num_terms--;

                LOG_MSG(SKRY_LOG_REF_PT_ALIGNMENT,
                            "Rejecting point %zu: translation by %.2f "
                            "too far from current mean: %.2f (std. dev.: %.2f)",
                            i, ref_pt->last_transl_vec.len, sum_len_avg, std_deviation);

                ref_pt_align->statistics.num_rejected_positions++;
            }
            else
            {
                ref_pt->last_valid_pos_idx = img_idx;
                ref_pt_align->statistics.num_valid_positions++;
            }
        }

        struct tvec_sum *dest_tvsum = &ref_pt_align->t_vectors.tvec_img_sum[ref_pt_align->t_vectors.next_entry];
        *dest_tvsum = curr_step_tvec;
        ref_pt_align->t_vectors.next_entry = (ref_pt_align->t_vectors.next_entry + 1) % TVEC_SUM_NUM_IMAGES;
    }
    else
        for (size_t i = 0; i < DA_SIZE(ref_pt_align->reference_pts); i++)
        {
            struct reference_point *ref_pt = &ref_pt_align->reference_pts.data[i];
            if (ref_pt->positions[img_idx].is_valid)
            {
                ref_pt->last_valid_pos_idx = img_idx;
                ref_pt_align->statistics.num_valid_positions++;
            }
        }

}

/// Returns pointer to the added point or null if out of memory
static
void *append_fixed_point(SKRY_RefPtAlignment *ref_pt_align, struct SKRY_point pos, const SKRY_ImgSequence *img_seq)
{
    DA_APPEND(ref_pt_align->reference_pts,
        ((struct reference_point) { .qual_est_area = SKRY_EMPTY,
                                    .ref_block = 0,
                                    .last_valid_pos_idx = 0,
                                    .last_transl_vec = { 0 }
                                  }));

    if (0 == (DA_LAST(ref_pt_align->reference_pts).positions =
                 malloc(SKRY_get_active_img_count(img_seq) * sizeof(*DA_LAST(ref_pt_align->reference_pts).positions))))
    {
        return 0;
    }

    for (unsigned j = 0; j < SKRY_get_active_img_count(img_seq); j++)
    {
        struct reference_point *ref_pt = &DA_LAST(ref_pt_align->reference_pts);
        ref_pt->positions[j].pos = pos;
        ref_pt->positions[j].is_valid = 1;
    }

    return &DA_LAST(ref_pt_align->reference_pts);
}

#define ADDITIONAL_FIXED_PTS_PER_BORDER   4
#define ADDITIONAL_FIXED_PT_OFFSET_DIV    4

/// Returns 'ref_pt_align' or null if out of memory
static
void *create_surrounding_fixed_points(SKRY_RefPtAlignment *ref_pt_align,
                                      struct SKRY_rect intersection,
                                      const SKRY_ImgSequence *img_seq)
{
    /* Add a few fixed points along and just outside intersection's borders. This way after triangulation
       the near-border points will not generate skinny triangles, which would result in locally degraded
       stack quality.

       Example of triangulation without the additional points:

        o                                                o

                        +--------------+
                        |  *   *   *   |
                        |              |<--images' intersection
                        |              |
                        +--------------+



                               o


             o = external fixed points added by SKRY_find_delaunay_triangulation()

       The internal near-border points (*) would generate skinny triangles with the upper (o) points.
       With additional fixed points:

        o                                                o

                           o   o   o

                        +--------------+
                      o |  *   *   *   |   o
                        |              |
                      o |              |   o

    */

    for (size_t i = 1; i <= ADDITIONAL_FIXED_PTS_PER_BORDER; i++)
    {
        // along top border
        if (!append_fixed_point(ref_pt_align,
                           (struct SKRY_point)
                               { .x = i * intersection.width / (ADDITIONAL_FIXED_PTS_PER_BORDER+1),
                                 .y = -(int)intersection.height / ADDITIONAL_FIXED_PT_OFFSET_DIV },
                           img_seq))
        {
            return 0;
        }

        // along bottom border
        if (!append_fixed_point(ref_pt_align,
                           (struct SKRY_point)
                               { .x = i * intersection.width / (ADDITIONAL_FIXED_PTS_PER_BORDER+1),
                                 .y = intersection.height + intersection.height / ADDITIONAL_FIXED_PT_OFFSET_DIV },
                           img_seq))
        {
            return 0;
        }

        // along left border
        if (!append_fixed_point(ref_pt_align,
                           (struct SKRY_point)
                               { .x = -(int)intersection.width / ADDITIONAL_FIXED_PT_OFFSET_DIV,
                                 .y = i * intersection.height / (ADDITIONAL_FIXED_PTS_PER_BORDER+1) },
                           img_seq))
        {
            return 0;
        }

        // along right border
        if (!append_fixed_point(ref_pt_align,
                           (struct SKRY_point)
                               { .x = intersection.width + intersection.width / ADDITIONAL_FIXED_PT_OFFSET_DIV,
                                 .y = i * intersection.height / (ADDITIONAL_FIXED_PTS_PER_BORDER+1) },
                           img_seq))
        {
            return 0;
        }
    }

    return ref_pt_align;
}

#define FAIL_ON_NULL(ptr)                          \
    if (!(ptr))                                    \
    {                                              \
        SKRY_free_ref_pt_alignment(ref_pt_align);  \
        SKRY_free_image(first_img);                \
        if (result) *result = SKRY_OUT_OF_MEMORY;  \
        return 0;                                  \
    }

SKRY_RefPtAlignment *SKRY_init_ref_pt_alignment(
    const SKRY_QualityEstimation *qual_est,
    /// Number of elements in 'points'; if zero, points will be placed automatically
    size_t num_points,
    /// Reference point positions; if null, points will be placed automatically
    /** Positions are specified within the images' intersection.
        The points must not lie outside it. */
    const struct SKRY_point *points,
    /// Min. image brightness that a ref. point can be placed at (values: [0; 1])
    /** Value is relative to the image's darkest (0.0) and brightest (1.0) pixels.
        Used only during automatic placement. */
    float placement_brightness_threshold,
    /// Criterion for updating ref. point position (and later for stacking)
    enum SKRY_quality_criterion quality_criterion,
    /// Interpreted according to 'quality_criterion'
    unsigned quality_threshold,
    /// Spacing in pixels between reference points (used only during automatic placement)
    unsigned spacing,
    /// If not null, receives operation result
    enum SKRY_result *result)
{
    SKRY_Image *first_img = 0; // Will be freed by FAIL_ON_NULL() in case of error
    SKRY_ImgSequence *img_seq = SKRY_get_img_seq(SKRY_get_img_align(qual_est));
    ///! FIXME: detect if image size < reference_block size and return an error; THE SAME goes for image alignment phase

    SKRY_seek_start(img_seq);

    SKRY_RefPtAlignment *ref_pt_align = malloc(sizeof(*ref_pt_align));
    FAIL_ON_NULL(ref_pt_align);

    // Sets all pointer fields to null, so it is safe to call SKRY_free_ref_pt_alignment()
    // via FAIL_ON_NULL() in case one of allocation fails.
    *ref_pt_align = (SKRY_RefPtAlignment) { 0 };

    ref_pt_align->statistics.time.start = SKRY_clock_sec();

    int automatic_points = (0 == num_points || 0 == points);
    if (automatic_points)
        ref_pt_align->spacing = spacing;
    else
        ref_pt_align->spacing = DEFAULT_SPACING;

    ref_pt_align->ref_block_size = 2*spacing/3; //TODO: make it adjustable?
    ref_pt_align->qual_est = qual_est;
    ref_pt_align->quality_criterion = quality_criterion;
    ref_pt_align->quality_threshold = quality_threshold;
    DA_ALLOC(ref_pt_align->reference_pts, 0);

    struct SKRY_rect intersection = SKRY_get_intersection(SKRY_get_img_align(qual_est));

    enum SKRY_result loc_result;
    first_img = SKRY_get_curr_img(img_seq, &loc_result);
    if (SKRY_SUCCESS != loc_result)
    {
        LOG_MSG(SKRY_LOG_REF_PT_ALIGNMENT, "Failed to load first active image from image sequence %p (error: %s).",
                (void *)img_seq, SKRY_get_error_message(loc_result));
        if (result) *result = loc_result;
        SKRY_free_ref_pt_alignment(ref_pt_align);
        return 0;
    }

    struct SKRY_point first_img_offset = SKRY_get_image_ofs(SKRY_get_img_align(qual_est), 0);

    SKRY_Image *img8 = SKRY_convert_pix_fmt_of_subimage(
        first_img, SKRY_PIX_MONO8,
        intersection.x + first_img_offset.x,
        intersection.y + first_img_offset.y,
        intersection.width, intersection.height,
        SKRY_DEMOSAIC_SIMPLE);

    SKRY_free_image(first_img);
    first_img = img8;

    if (automatic_points)
    {
        points = SKRY_suggest_ref_point_positions(
                        qual_est, &num_points,
                        placement_brightness_threshold,
                        spacing);
        FAIL_ON_NULL(points);
    }

    for (size_t i = 0; i < num_points; i++)
    {
        assert(points[i].x >= 0 && points[i].x < (int)intersection.width &&
               points[i].y >= 0 && points[i].y < (int)intersection.height);

        // Not initializing the reference block yet, as we do not know if the current area
        // meets the quality criteria in the current (first) image.
        DA_APPEND(ref_pt_align->reference_pts,
            ((struct reference_point)
                { .qual_est_area = SKRY_get_area_idx_at_pos(qual_est, (struct SKRY_point) { .x = points[i].x, .y = points[i].y }),
                  .ref_block = 0,
                  .last_valid_pos_idx = SKRY_EMPTY,
                  .last_transl_vec = { 0 } }));

        struct reference_point *new_ref_pt = &DA_LAST(ref_pt_align->reference_pts);
        new_ref_pt->positions = malloc(SKRY_get_active_img_count(img_seq) * sizeof(*new_ref_pt->positions));
        FAIL_ON_NULL(new_ref_pt->positions);
        new_ref_pt->positions[0].pos = (struct SKRY_point) { .x = points[i].x,
                                                             .y = points[i].y };

        LOG_MSG(SKRY_LOG_REF_PT_ALIGNMENT, "Added reference point at (%d, %d).",
                new_ref_pt->positions[0].pos.x,
                new_ref_pt->positions[0].pos.y);
    }
    if (automatic_points)
        free((struct SKRY_point *)points);

    FAIL_ON_NULL(create_surrounding_fixed_points(ref_pt_align,
                 intersection, img_seq));

    // Envelope of all reference points (including the fixed ones)
    struct SKRY_rect envelope =
        { .x = -(int)intersection.width/ADDITIONAL_FIXED_PT_OFFSET_DIV,
          .y = -(int)intersection.height/ADDITIONAL_FIXED_PT_OFFSET_DIV,
          .width = intersection.width + 2*intersection.width/ADDITIONAL_FIXED_PT_OFFSET_DIV,
          .height = intersection.height + 2*intersection.height/ADDITIONAL_FIXED_PT_OFFSET_DIV };

    // Find the Delaunay triangulation of the reference points

    struct SKRY_point *initial_positions = malloc(DA_SIZE(ref_pt_align->reference_pts) * sizeof(*initial_positions));
    FAIL_ON_NULL(initial_positions);
    for (size_t i = 0; i < DA_SIZE(ref_pt_align->reference_pts); i++)
        initial_positions[i] = ref_pt_align->reference_pts.data[i].positions[0].pos;

    ref_pt_align->triangulation = SKRY_find_delaunay_triangulation(DA_SIZE(ref_pt_align->reference_pts),
        initial_positions, envelope);
    free(initial_positions);
    FAIL_ON_NULL(ref_pt_align->triangulation);

    // The triangulation object contains 3 additional points comprising a triangle that covers all the other points.
    // These 3 points shall have fixed position and are not associated with any quality estimation area. Add them
    // to the list now and fill their position for all images.
    for (size_t i = SKRY_get_num_vertices(ref_pt_align->triangulation) - 3; i < SKRY_get_num_vertices(ref_pt_align->triangulation); i++)
    {
        append_fixed_point(ref_pt_align, SKRY_get_vertices(ref_pt_align->triangulation)[i], img_seq);
    }

    ref_pt_align->update_flags = malloc(DA_SIZE(ref_pt_align->reference_pts) * sizeof(*ref_pt_align->update_flags));
    FAIL_ON_NULL(ref_pt_align->update_flags);
    ref_pt_align->tri_quality = malloc(SKRY_get_num_triangles(ref_pt_align->triangulation) * sizeof(*ref_pt_align->tri_quality));
    FAIL_ON_NULL(ref_pt_align->tri_quality);
    for (size_t i = 0; i < SKRY_get_num_triangles(ref_pt_align->triangulation); i++)
        ref_pt_align->tri_quality[i].sorted_idx = 0;

    FAIL_ON_NULL(calc_triangle_quality(ref_pt_align));

    update_ref_pt_positions(ref_pt_align, first_img, 0,
                            SKRY_get_active_img_count(img_seq),
                            ref_pt_align->quality_criterion,
                            ref_pt_align->quality_threshold,
                            // 'img' is already just an intersection-sized fragment of the first image,
                            // so pass a full-image "intersection" and a zero offset
                            SKRY_get_img_rect(first_img), (struct SKRY_point) { 0, 0 });

    SKRY_free_image(first_img);

    if (result) *result = SKRY_SUCCESS;
    return ref_pt_align;
}

/// Returns null
SKRY_RefPtAlignment *SKRY_free_ref_pt_alignment(SKRY_RefPtAlignment *ref_pt_align)
{
    if (ref_pt_align)
    {
        for (size_t i = 0; i < DA_SIZE(ref_pt_align->reference_pts); i++)
        {
            struct reference_point *ref_pt = &ref_pt_align->reference_pts.data[i];
            free(ref_pt->positions);
            SKRY_free_image(ref_pt->ref_block);
        }

        DA_FREE(ref_pt_align->reference_pts);

        for (size_t i = 0; i < SKRY_get_num_triangles(ref_pt_align->triangulation); i++)
            free(ref_pt_align->tri_quality[i].sorted_idx);

        free(ref_pt_align->tri_quality);

        SKRY_free_triangulation(ref_pt_align->triangulation);
        free(ref_pt_align->update_flags);
        free(ref_pt_align);
    }
    return 0;
}

size_t SKRY_get_num_ref_pts(const SKRY_RefPtAlignment *ref_pt_align)
{
    return DA_SIZE(ref_pt_align->reference_pts);
}

struct SKRY_point SKRY_get_ref_pt_pos(const SKRY_RefPtAlignment *ref_pt_align,
                                      size_t point_idx, size_t img_idx,
                                      /// If not null, receives 1 if the point is valid in specified image
                                      int *is_valid)
{
    const struct reference_point *ref_pt = &ref_pt_align->reference_pts.data[point_idx];
    if (is_valid) *is_valid = ref_pt->positions[img_idx].is_valid;
    return ref_pt->positions[img_idx].pos;
}

/// Returns SKRY_SUCCESS (i.e. more steps left to do), SKRY_LAST_STEP (no more steps) or an error
enum SKRY_result SKRY_ref_pt_alignment_step(SKRY_RefPtAlignment *ref_pt_align)
{
    SKRY_ImgSequence *img_seq = SKRY_get_img_seq(SKRY_get_img_align(ref_pt_align->qual_est));

    enum SKRY_result result = SKRY_seek_next(img_seq);
    if (SKRY_NO_MORE_IMAGES == result)
    {
        ensure_tris_are_valid(ref_pt_align);
        ref_pt_align->is_complete = 1;
        ref_pt_align->statistics.time.total_sec = SKRY_clock_sec() - ref_pt_align->statistics.time.start;

        LOG_MSG(SKRY_LOG_REF_PT_ALIGNMENT, "Valid reference point positions: %"PRId64", rejected: %"PRId64" (%.2f%%)",
                ref_pt_align->statistics.num_valid_positions,
                ref_pt_align->statistics.num_rejected_positions,
                100.0 * (double)ref_pt_align->statistics.num_rejected_positions
                               / (ref_pt_align->statistics.num_valid_positions
                                + ref_pt_align->statistics.num_rejected_positions));

        LOG_MSG(SKRY_LOG_REF_PT_ALIGNMENT, "Processing time: %.3f s", ref_pt_align->statistics.time.total_sec);

        return SKRY_LAST_STEP;
    }
    else if (SKRY_SUCCESS != result)
    {
        LOG_MSG(SKRY_LOG_REF_PT_ALIGNMENT, "Could not seek to the next image of image sequence %p (error: %d).",
                (void *)img_seq, (int)result);
        return result;
    }

    size_t img_idx = SKRY_get_curr_img_idx_within_active_subset(img_seq);

    SKRY_Image *img = SKRY_get_curr_img(img_seq, &result);
    if (SKRY_SUCCESS != result)
    {
        LOG_MSG(SKRY_LOG_REF_PT_ALIGNMENT, "Could not load image %zu from image sequence %p (error: %d).",
                SKRY_get_curr_img_idx(img_seq), (void *)img_seq, (int)result);
        return result;
    }

    if (SKRY_get_img_pix_fmt(img) != SKRY_PIX_MONO8)
    {
        SKRY_Image *img8 = SKRY_convert_pix_fmt(img, SKRY_PIX_MONO8, SKRY_DEMOSAIC_SIMPLE);
        SKRY_free_image(img);
        img = img8;
    }

    update_ref_pt_positions(ref_pt_align, img, img_idx,
                            SKRY_get_active_img_count(img_seq),
                            ref_pt_align->quality_criterion,
                            ref_pt_align->quality_threshold,
                            SKRY_get_intersection(SKRY_get_img_align(ref_pt_align->qual_est)),
                            SKRY_get_image_ofs(SKRY_get_img_align(ref_pt_align->qual_est), img_idx));

    img = SKRY_free_image(img);

    return SKRY_SUCCESS;
}

int SKRY_is_ref_pt_alignment_complete(const SKRY_RefPtAlignment *ref_pt_align)
{
    return ref_pt_align->is_complete;
}

/// Returns the quality estimation object associated with 'ref_pt_align'
const SKRY_QualityEstimation *SKRY_get_qual_est(const SKRY_RefPtAlignment *ref_pt_align)
{
    return ref_pt_align->qual_est;
}

/** Returns an array of final (i.e. averaged over all images) positions of reference points.
    Returns null if out of memory or if alignment is not complete. */
struct SKRY_point_flt *SKRY_get_final_positions(const SKRY_RefPtAlignment *ref_pt_align,
                                                /// Receives the number of points
                                                size_t *num_points)
{
    if (!ref_pt_align->is_complete)
        return 0;

    *num_points = DA_SIZE(ref_pt_align->reference_pts);

    struct SKRY_point_flt *result = malloc(*num_points * sizeof(*result));
    if (!result)
        return 0;

    for (size_t pt_idx = 0; pt_idx < DA_SIZE(ref_pt_align->reference_pts); pt_idx++)
    {
        const struct reference_point *ref_pt = &ref_pt_align->reference_pts.data[pt_idx];
        size_t valid_pos_count = 0;
        result[pt_idx] = (struct SKRY_point_flt) { .x = 0, .y = 0 };
        for (unsigned img_idx = 0; img_idx < SKRY_get_active_img_count(SKRY_get_img_seq(SKRY_get_img_align(ref_pt_align->qual_est))); img_idx++)
        {
            // Due to how 'update_ref_pt_positions()' works, it is guaranteed that at least one element "is valid"
            if (ref_pt->positions[img_idx].is_valid)
            {
                result[pt_idx].x += ref_pt->positions[img_idx].pos.x;
                result[pt_idx].y += ref_pt->positions[img_idx].pos.y;
                valid_pos_count++;
            }
        }

        result[pt_idx].x /= valid_pos_count;
        result[pt_idx].y /= valid_pos_count;
    }

    return result;
}

int SKRY_is_ref_pt_valid(const SKRY_RefPtAlignment *ref_pt_align, size_t pt_idx, size_t img_idx)
{
    return ref_pt_align->reference_pts.data[pt_idx].positions[img_idx].is_valid;
}

/// Triangulation contains 3 additional points at the end of vertex list: a triangle that covers all the other points
const struct SKRY_triangulation *SKRY_get_ref_pts_triangulation(const SKRY_RefPtAlignment *ref_pt_align)
{
    return ref_pt_align->triangulation;
}
