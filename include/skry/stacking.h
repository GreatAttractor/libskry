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
    Image stacking header.
*/

#ifndef LIB_STACKISTRY_STACKING_HEADER
#define LIB_STACKISTRY_STACKING_HEADER

#include "image.h"
#include "ref_pt_align.h"


typedef struct SKRY_stacking SKRY_Stacking;

SKRY_Stacking *SKRY_init_stacking(const SKRY_RefPtAlignment *ref_pt_align,
                                  /// May be null; no longer used after the function returns
                                  const SKRY_Image *flatfield,
                                  /// If not null, receives operation result
                                  enum SKRY_result *result);

/// Returns null
SKRY_Stacking *SKRY_free_stacking(SKRY_Stacking *stacking);

/// Returns SKRY_SUCCESS (i.e. more steps left to do), SKRY_LAST_STEP (no more steps) or an error
enum SKRY_result SKRY_stacking_step(SKRY_Stacking *stacking);

/// Can be used only after stacking completes
const SKRY_Image *SKRY_get_image_stack(const SKRY_Stacking *stacking);

/// Returns an incomplete image stack, updated after every stacking step
SKRY_Image *SKRY_get_partial_image_stack(const SKRY_Stacking *stacking);

int SKRY_is_stacking_complete(const SKRY_Stacking *stacking);

/// Returns an array of triangle indices stacked in current step
/** Meant to be called right after SKRY_stacking_step(). Values are indices into triangle array
    of the triangulation returned by SKRY_get_ref_pts_triangulation(). Vertex coordinates do not
    correspond with the triangulation, but with the array returned by 'SKRY_get_ref_pt_stacking_pos'. */
const size_t *SKRY_get_curr_step_stacked_triangles(
                const SKRY_Stacking *stacking,
                /// Receives length of the returned array
                size_t *num_triangles);

/// Returns reference point positions as used during stacking
const struct SKRY_point_flt *SKRY_get_ref_pt_stacking_pos(const SKRY_Stacking *stacking);

#endif // LIB_STACKISTRY_STACKING_HEADER
