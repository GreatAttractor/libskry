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
    Image alignment header.
*/

#ifndef LIB_STACKISTRY_IMAGE_ALIGNMENT_HEADER
#define LIB_STACKISTRY_IMAGE_ALIGNMENT_HEADER

#include <stddef.h>

#include "defs.h"
#include "imgseq.h"


typedef struct SKRY_img_alignment SKRY_ImgAlignment;

/// Returns null
SKRY_ImgAlignment *SKRY_free_img_alignment(SKRY_ImgAlignment *img_algn);

SKRY_ImgAlignment *SKRY_init_img_alignment(
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
);

/// Returns SKRY_SUCCESS (i.e. more steps left to do), SKRY_LAST_STEP (no more steps) or an error
enum SKRY_result SKRY_img_alignment_step(SKRY_ImgAlignment *img_algn);

int SKRY_is_img_alignment_complete(const SKRY_ImgAlignment *img_algn);

/** The return value may increase during processing (when all existing
    anchors became invalid and a new one(s) had to be automatically created). */
size_t SKRY_get_anchor_count(const SKRY_ImgAlignment *img_algn);

/// Returns current positions of anchor points
void SKRY_get_anchors(const SKRY_ImgAlignment *img_algn,
                      /// Has to have room for at least SKRY_get_anchor_count() elements
                      struct SKRY_point points[]);

int SKRY_is_anchor_valid(const SKRY_ImgAlignment *img_algn, size_t anchor_idx);

/// Returns offset of images' intersection relative to the first image's origin
struct SKRY_point SKRY_get_intersection_ofs(const SKRY_ImgAlignment *img_algn);

void SKRY_get_intersection_size(const SKRY_ImgAlignment *img_algn,
                                unsigned *width, ///< If not null, receives width of images' intersection
                                unsigned *height ///< If not null, receives height of images' intersection
                                );

/// Returns the images' intersection (position relative to the first image's origin)
struct SKRY_rect SKRY_get_intersection(const SKRY_ImgAlignment *img_algn);

struct SKRY_point SKRY_get_image_ofs(const SKRY_ImgAlignment *img_algn, size_t img_idx);

/// Returns the associated image sequence
SKRY_ImgSequence *SKRY_get_img_seq(const SKRY_ImgAlignment *img_algn);

/// Returns optimal position of a video stabilization anchor in 'image'
struct SKRY_point SKRY_suggest_anchor_pos(
    const SKRY_Image *image,
    /// Min. image brightness that an anchor point can be placed at (values: [0; 1])
    /** Value is relative to the image's darkest (0.0) and brightest (1.0) pixels. */
    float placement_brightness_threshold,
    unsigned ref_block_size);

enum SKRY_img_alignment_method SKRY_get_alignment_method(const SKRY_ImgAlignment *img_algn);

/// Returns current centroid position
struct SKRY_point SKRY_get_current_centroid_pos(const SKRY_ImgAlignment *img_algn);


#endif // LIB_STACKISTRY_IMAGE_ALIGNMENT_HEADER
