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
    Reference point alignment header.
*/

#ifndef LIB_STACKISTRY_REFERENCE_POINT_ALIGNMENT_HEADER
#define LIB_STACKISTRY_REFERENCE_POINT_ALIGNMENT_HEADER

#include <stddef.h>

#include "quality.h"
#include "triangulation.h"


typedef struct SKRY_ref_pt_alignment SKRY_RefPtAlignment;

SKRY_RefPtAlignment *SKRY_init_ref_pt_alignment(
    const SKRY_QualityEstimation *qual_est,
    /// Number of elements in 'points'; if zero, points will be placed automatically
    size_t num_points,
    /// Reference point positions; if null, points will be placed automatically
    /** Positions are specified within the images' intersection.
        The points must not lie outside it. */
    const struct SKRY_point *points,

    /// Criterion for updating ref. point position (and later for stacking)
    enum SKRY_quality_criterion quality_criterion,

    /// Interpreted according to 'quality_criterion'
    unsigned quality_threshold,

    /// Size (in pixels) of reference blocks used for block matching
    unsigned ref_block_size,

    /// Search radius (in pixels) used during block matching
    unsigned search_radius,

    /// If not null, receives operation result
    enum SKRY_result *result,

    // Parameters used if num_points==0 (automatic placement of ref. points) -----------------

    /// Min. image brightness that a ref. point can be placed at (values: [0; 1])
    /** Value is relative to the image's darkest (0.0) and brightest (1.0) pixels. */
    float placement_brightness_threshold,

    /// Structure detection threshold; value of 1.2 is recommended
    /** The greater the value, the more local contrast is required to place
        a ref. point. */
    float structure_threshold,

    /** Corresponds with pixel size of smallest structures. Should equal 1
        for optimally-sampled or undersampled images. Use higher values
        for oversampled (blurry) material. */
    unsigned structure_scale,

    /// Spacing in pixels between reference points
    unsigned spacing
);

/// Returns null
SKRY_RefPtAlignment *SKRY_free_ref_pt_alignment(SKRY_RefPtAlignment *ref_pt_align);

size_t SKRY_get_num_ref_pts(const SKRY_RefPtAlignment *ref_pt_align);

struct SKRY_point SKRY_get_ref_pt_pos(const SKRY_RefPtAlignment *ref_pt_align, size_t point_idx, size_t img_idx,
                                      /// If not null, receives 1 if the point is valid in specified image
                                      int *is_valid);

/// Returns SKRY_SUCCESS (i.e. more steps left to do), SKRY_LAST_STEP (no more steps) or an error
enum SKRY_result SKRY_ref_pt_alignment_step(SKRY_RefPtAlignment *ref_pt_align);

int SKRY_is_ref_pt_alignment_complete(const SKRY_RefPtAlignment *ref_pt_align);

/// Returns the quality estimation object associated with 'ref_pt_align'
const SKRY_QualityEstimation *SKRY_get_qual_est(const SKRY_RefPtAlignment *ref_pt_align);

/** Returns an array of final (i.e. averaged over all images) positions of reference points.
    Returns null if out of memory or if alignment is not complete. */
struct SKRY_point_flt *SKRY_get_final_positions(const SKRY_RefPtAlignment *ref_pt_align,
                                                /// Receives the number of points
                                                size_t *num_points);

int SKRY_is_ref_pt_valid(const SKRY_RefPtAlignment *ref_pt_align, size_t pt_idx, size_t img_idx);

/// Triangulation contains 3 additional points at the end of vertex list: a triangle that covers all the other points
const struct SKRY_triangulation *SKRY_get_ref_pts_triangulation(const SKRY_RefPtAlignment *ref_pt_align);

#endif
