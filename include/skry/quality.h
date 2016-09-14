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
    Quality estimation header.
*/

#ifndef LIB_STACKISTRY_QUALITY_HEADER
#define LIB_STACKISTRY_QUALITY_HEADER


#include "defs.h"
#include "image.h"
#include "img_align.h"


typedef struct SKRY_quality_estimation SKRY_QualityEstimation;

/// Returns null if out of memory
SKRY_QualityEstimation *SKRY_init_quality_est(
        const SKRY_ImgAlignment *img_algn,
        /// Aligned image sequence will be divided into areas of this size for quality estimation
        unsigned estimation_area_size,
        /// Corresponds with box blur radius used for quality estimation
        unsigned detail_scale);

/// Returns null
SKRY_QualityEstimation *SKRY_free_quality_est(SKRY_QualityEstimation *qual_est);

int SKRY_is_qual_est_complete(const SKRY_QualityEstimation *qual_est);

/// Returns SKRY_SUCCESS (i.e. more steps left to do), SKRY_LAST_STEP (no more steps) or an error
enum SKRY_result SKRY_quality_est_step(SKRY_QualityEstimation *qual_est);

size_t SKRY_get_qual_est_num_areas(const SKRY_QualityEstimation *qual_est);

/// Fills 'qual_array' with overall quality values of subsequent images
void SKRY_get_images_quality(const SKRY_QualityEstimation *qual_est,
    /// Element count = number of active images in img. sequence associated with 'qual_est'
    SKRY_quality_t qual_array[]
);

SKRY_quality_t SKRY_get_avg_area_quality(const SKRY_QualityEstimation *qual_est, size_t area_idx);

SKRY_quality_t SKRY_get_area_quality(const SKRY_QualityEstimation *qual_est, size_t area_idx, size_t img_idx);

SKRY_quality_t SKRY_get_best_avg_area_quality(const SKRY_QualityEstimation *qual_est);

SKRY_quality_t SKRY_get_overall_avg_area_quality(const SKRY_QualityEstimation *qual_est);

/// Returns the image alignment object associated with 'qual_est'
const SKRY_ImgAlignment *SKRY_get_img_align(const SKRY_QualityEstimation *qual_est);

struct SKRY_point SKRY_get_qual_est_area_center(const SKRY_QualityEstimation *qual_est, size_t area_idx);

size_t SKRY_get_area_idx_at_pos(const SKRY_QualityEstimation *qual_est,
                                /// Position within images' intersection
                                struct SKRY_point pos);

SKRY_quality_t SKRY_get_min_nonzero_avg_area_quality(const SKRY_QualityEstimation *qual_est);

size_t SKRY_get_best_img_idx(const SKRY_QualityEstimation *qual_est);

/// Returns a composite image consisting of best fragments of all frames
/** Returns null if out of memory. */
SKRY_Image *SKRY_get_best_fragments_img(const SKRY_QualityEstimation *qual_est);

/// Returns an array of suggested reference point positions; return null if out of memory
struct SKRY_point *SKRY_suggest_ref_point_positions(
    const SKRY_QualityEstimation *qual_est,
    size_t *num_points, ///< Receives number of elements in the result

    /// Min. image brightness that a ref. point can be placed at (values: [0; 1])
    /** Value is relative to the darkest (0.0) and brightest (1.0) pixels. */
    float brightness_threshold,

    /// Structure detection threshold; value of 1.2 is recommended
    /** The greater the value, the more local contrast is required to place
        a ref. point. */
    float structure_threshold,

    /** Corresponds with pixel size of smallest structures. Should equal 1
        for optimally-sampled or undersampled images. Use higher values
        for oversampled (blurry) material. */
    unsigned structure_scale,

    /// Spacing in pixels between reference points
    unsigned spacing,

    /// Size of reference blocks used for block matching
    unsigned ref_block_size
);


#endif // LIB_STACKISTRY_QUALITY_HEADER
