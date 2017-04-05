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
    Delaunay triangulation header.
*/

#ifndef LIBSKRY_DELAUNAY_HEADER
#define LIBSKRY_DELAUNAY_HEADER

#include <skry/defs.h>


typedef struct SKRY_triangulation SKRY_Triangulation;

struct SKRY_edge
{
    size_t v0, v1; // contained vertices (ends)
    size_t t0, t1; // adjacent triangles (if there is only one, t0 or t1 will be SKRY_EMPTY)
    size_t w0, w1; // opposing vertices (if there is only one, w0 or w1 will be SKRY_EMPTY)
    // Note: w0 may belong to either t0 or t1; the same goes for w1.
};

struct SKRY_triangle
{
    size_t v0, v1, v2; // vertices in CCW order

    // Edges in CCW order: e0 contains v0,v1; e1 contains v1,v2; e2 contains v2,v0.
    // Note: in triangulation's 'edges' array the edges' vertices may not be specified in this order
    size_t e0, e1, e2;
};

/** Finds Delaunay triangulation for the specified point set; also adds (at the end
    of points' list) three additional points for the initial triangle which covers
    the whole set and 'envelope'. Returns null if out of memory. */
SKRY_Triangulation *SKRY_find_delaunay_triangulation(
        size_t num_points,
        /// All points have to be different
        const struct SKRY_point points[],
        /// Must be big enough to cover the whole set of 'points'
        struct SKRY_rect envelope);

/// Returns null
SKRY_Triangulation *SKRY_free_triangulation(SKRY_Triangulation *tri);

size_t SKRY_get_num_vertices(const SKRY_Triangulation *tri);
const struct SKRY_point *SKRY_get_vertices(const SKRY_Triangulation *tri);

size_t SKRY_get_num_edges(const SKRY_Triangulation *tri);
const struct SKRY_edge *SKRY_get_edges(const SKRY_Triangulation *tri);

size_t SKRY_get_num_triangles(const SKRY_Triangulation *tri);
const struct SKRY_triangle *SKRY_get_triangles(const SKRY_Triangulation *tri);

/// Finds barycentric coordinates of point 'p' in the triangle (v0, v1, v2) ('p' can be outside triangle)
void SKRY_calc_barycentric_coords(struct SKRY_point p,
                                  struct SKRY_point v0,
                                  struct SKRY_point v1,
                                  struct SKRY_point v2,
                                  float *u, ///< Receives the coordinate relative to the triangle's first vertex
                                  float *v  ///< Receives the coordinate relative to the triangle's second vertex
);

/// Finds barycentric coordinates of point 'p' in the triangle (v0, v1, v2) ('p' can be outside triangle)
void SKRY_calc_barycentric_coords_flt(struct SKRY_point p,
                                      struct SKRY_point_flt v0,
                                      struct SKRY_point_flt v1,
                                      struct SKRY_point_flt v2,
                                      float *u, ///< Receives the coordinate relative to the triangle's first vertex
                                      float *v  ///< Receives the coordinate relative to the triangle's second vertex
);

#endif // LIBSKRY_DELAUNAY_HEADER
