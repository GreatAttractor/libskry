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

/// Returns a square image to be used as reference block; returns null if out of memory
//TODO: move it out of public interface
SKRY_Image *SKRY_create_reference_block(
    const SKRY_QualityEstimation *qual_est,
    /// Center of the reference block (within images' intersection)
    struct SKRY_point pos,
    /// Desired width & height; the result may be smaller than this (but always a square)
    unsigned blk_size);


size_t SKRY_get_area_idx_at_pos(const SKRY_QualityEstimation *qual_est,
                                /// Position within images' intersection
                                struct SKRY_point pos);

SKRY_quality_t SKRY_get_min_nonzero_avg_area_quality(const SKRY_QualityEstimation *qual_est);

#endif // LIB_STACKISTRY_QUALITY_HEADER
