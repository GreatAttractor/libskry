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
    Delaunay triangulation implementation.
*/

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <skry/triangulation.h>

#include "dnarray.h"
#include "logging.h"


static
void replace_opposing_vertex(struct SKRY_edge *edge, size_t wold, size_t wnew)
{
    if (edge->w0 == wold) edge->w0 = wnew;
    else if (edge->w1 == wold) edge->w1 = wnew;
    else if (edge->w0 == SKRY_EMPTY) edge->w0 = wnew;
    else if (edge->w1 == SKRY_EMPTY) edge->w1 = wnew;
}

static
void replace_adjacent_triangle(struct SKRY_edge *edge, size_t told, size_t tnew)
{
    if (edge->t0 == told) edge->t0 = tnew;
    else if (edge->t1 == told) edge->t1 = tnew;
    else if (edge->t0 == SKRY_EMPTY) edge->t0 = tnew;
    else if (edge->t1 == SKRY_EMPTY) edge->t1 = tnew;
}

#define TRI_CONTAINS(triangle, vertex) \
    ((vertex) == (triangle)->v0 || (vertex) == (triangle)->v1 || (vertex) == (triangle)->v2)

static
size_t next_vertex(struct SKRY_triangle *t, size_t vertex)
{
    if (vertex == t->v0) return t->v1;
    else if (vertex == t->v1) return t->v2;
    else if (vertex == t->v2) return t->v0;
    else return SKRY_EMPTY; // this should never happen
}

// Each vertex has a 'leading' and a 'trailing' edge (corresponding to CCW order).
// The 'leading' edge is the one which contains the vertex and a vertex which succeeds it in CCW order.
static
size_t get_leading_edge_containing_vertex(struct SKRY_triangle *t, size_t vertex)
{
    if (vertex == t->v0) return t->e0;
    else if (vertex == t->v1) return t->e1;
    else if (vertex == t->v2) return t->e2;
    else return SKRY_EMPTY; // this should never happen
}

struct SKRY_triangulation
{
    size_t num_vertices;
    struct SKRY_point *vertices;
    DA_DECLARE(struct SKRY_edge) edges;
    DA_DECLARE(struct SKRY_triangle) triangles;
};

static
int is_inside_circumcircle(size_t pidx, size_t tidx, SKRY_Triangulation *tri)
{
    struct SKRY_point *p = &tri->vertices[pidx];
    struct SKRY_triangle *t = &tri->triangles.data[tidx];

    struct SKRY_point *A = &tri->vertices[t->v0],
                      *B = &tri->vertices[t->v1],
                      *C = &tri->vertices[t->v2];


    // Coordinates of the circumcenter
    float ux, uy;
    // Squared radius of the circumcircle
    float radiusq;

    // Note: the formulas below work correctly regardless of the handedness of the coordinate system used
    //       (for triangulation of points in an image a left-handed system is used here, i.e. X grows to the right, Y downwards)

    float d = 2.0f * ((float)A->x * (B->y - C->y) + (float)B->x * (C->y - A->y) + (float)C->x * (A->y - B->y));

    if (fabs(d) > 1e-8)
    {

        ux = ((SKRY_SQR((float)A->x) + SKRY_SQR((float)A->y)) * (B->y - C->y) +
              (SKRY_SQR((float)B->x) + SKRY_SQR((float)B->y)) * (C->y - A->y) +
              (SKRY_SQR((float)C->x) + SKRY_SQR((float)C->y)) * (A->y - B->y)) / d;

        uy = ((SKRY_SQR((float)A->x) + SKRY_SQR((float)A->y)) * (C->x - B->x) +
              (SKRY_SQR((float)B->x) + SKRY_SQR((float)B->y)) * (A->x - C->x) +
              (SKRY_SQR((float)C->x) + SKRY_SQR((float)C->y)) * (B->x - A->x)) / d;

        radiusq = SKRY_SQR(ux - A->x) + SKRY_SQR(uy - A->y);
    }
    else // degenerated triangle (co-linear vertices)
    {
        float distABsq = SKRY_SQR(A->x - B->x) + SKRY_SQR(A->y - B->y),
              distACsq = SKRY_SQR(A->x - C->x) + SKRY_SQR(A->y - C->y),
              distBCsq = SKRY_SQR(B->x - C->x) + SKRY_SQR(B->y - C->y);

        // Extreme vertices of the degenerated triangle
        struct SKRY_point *ext1, *ext2;
        if (distABsq >= distACsq && distABsq >= distBCsq)
        {
            ext1 = A; ext2 = B;
        }
        else if (distACsq >= distABsq && distACsq >= distBCsq)
        {
            ext1 = A; ext2 = C;
        }
        else
        {
            ext1 = B; ext2 = C;
        }

        ux = (ext1->x + ext2->x) * 0.5f;
        uy = (ext1->y + ext2->y) * 0.5f;

        radiusq = 0.25f * (SKRY_SQR(ext1->x - ext2->x) + SKRY_SQR(ext1->y - ext2->y));
    }

    return SKRY_SQR(p->x - ux) + SKRY_SQR(p->y - uy) < radiusq;
}

//TODO: use the other "calc. bary..." function
//FIXME: detect degenerate triangles
static
int is_inside_triangle(size_t pidx, size_t tidx, SKRY_Triangulation *tri)
{
    struct SKRY_point *p = &tri->vertices[pidx];

    struct SKRY_triangle *t = &tri->triangles.data[tidx];

    struct SKRY_point *A = &tri->vertices[t->v0],
                      *B = &tri->vertices[t->v1],
                      *C = &tri->vertices[t->v2];

    //Calculate barycentric coordinates
    float a = ((float)(B->y - C->y) * (p->x - C->x) + (float)(C->x - B->x) * (p->y - C->y)) / ((float)(B->y - C->y) * (A->x - C->x) + (float)(C->x - B->x) * (A->y - C->y));
    float b = ((float)(C->y - A->y) * (p->x - C->x) + (float)(A->x - C->x) * (p->y - C->y)) / ((float)(B->y - C->y) * (A->x - C->x) + (float)(C->x - B->x) * (A->y - C->y));
    float c = 1.0f - a - b;

    return (a >= 0 && a <= 1 && b >= 0 && b <= 1 && c >= 0 && c <= 1);
}

/*
    If the specified edge 'e' violates the Delaunay condition, swaps it
    and recursively continues to test the 4 neighboring edges.

    Before:

        v3--e2---v2
       / t1 ___/ /
     e3  __e4   e1
     / _/   t0 /
    v0---e0--v1

    After swapping e4:

        v3--e2---v2
       / \   t0 /
     e3   e4   e1
     /  t1 \  /
    v0--e0--v1

    How to decide which of the new triangles is now t0 and which t1?
    For each of the triangles adjacent to 'e4' before the swap, take their vertex opposite to 'e4' and the next vertex.
    After edge swap, the new triangle still contains the same 2 vertices. From the example above:

    1) For t0 (v0-v1-v2), use v1, v2. After swap, the new t0 is the triangle which still contains v1, v2.
    2) For t1 (v0-v2-v3), use v3, v0. After swap, the new t1 is the triangle which still contains v3, v0.

    After the swap is complete, recursively test e0, e1, e2 and e3.
*/
static
void test_and_swap_edge(SKRY_Triangulation *tri, size_t e,
                        // edges to skip when checking what needs swapping
                        size_t eskip1, size_t eskip2)
{
    struct SKRY_edge *tri_edges = tri->edges.data;

    // edge 'e' before the swap
    struct SKRY_edge eprev = tri->edges.data[e];

    // 0) Check the Delaunay condition for 'e's adjacent triangles

    if (tri_edges[e].t0 == SKRY_EMPTY || tri_edges[e].t1 == SKRY_EMPTY)
    {
        LOG_MSG(SKRY_LOG_TRIANGULATION, "Testing edge %zu: external edge, not swapping.", e);
        return;
    }

    // triangles which share edge 'e' before the swap
    struct SKRY_triangle *t0prev = &tri->triangles.data[eprev.t0],
                           *t1prev = &tri->triangles.data[eprev.t1];



    //TODO: guarantee that always 'w0' belongs to 't0' and 'w1' to 't1' - then we can get rid of all the "contains" checks below

    int swap_needed = 0;

    if (!swap_needed && TRI_CONTAINS(t0prev, eprev.w0) && is_inside_circumcircle(eprev.w1, eprev.t0, tri))
    {
        swap_needed = 1;
        LOG_MSG(SKRY_LOG_TRIANGULATION, "Testing edge %zu: needs swapping, because tri %zu contains vertex %zu and vertex %zu "
                                        "is inside the tri's c-circle.",
                                                      e,                         eprev.t0,           eprev.w0,      eprev.w1);
    }

    if (!swap_needed && TRI_CONTAINS(t0prev, eprev.w1) && is_inside_circumcircle(eprev.w0, eprev.t0, tri))
    {
        swap_needed = 1;
        LOG_MSG(SKRY_LOG_TRIANGULATION, "Testing edge %zu: needs swapping, because tri %zu contains vertex %zu and vertex %zu "
                                        "is inside the tri's c-circle.",
                                                      e,                         eprev.t0,            eprev.w1,     eprev.w0);
    }

    if (!swap_needed && TRI_CONTAINS(t1prev, eprev.w0) && is_inside_circumcircle(eprev.w1, eprev.t1, tri))
    {
        swap_needed = 1;
        LOG_MSG(SKRY_LOG_TRIANGULATION, "Testing edge %zu: needs swapping, because tri %zu contains vertex %zu and vertex %zu "
                                        "is inside the tri's c-circle.",
                                                      e,                         eprev.t1,           eprev.w0,      eprev.w1);
    }

    if (!swap_needed && TRI_CONTAINS(t1prev, eprev.w1) && is_inside_circumcircle(eprev.w0, eprev.t1, tri))
    {
        swap_needed = 1;
        LOG_MSG(SKRY_LOG_TRIANGULATION, "Testing edge %zu: needs swapping, because tri %zu contains vertex %zu and vertex %zu "
                                        "is inside the tri's c-circle.",
                                                      e,                         eprev.t1,           eprev.w1,      eprev.w0);
    }

    if (!swap_needed)
    {
        LOG_MSG(SKRY_LOG_TRIANGULATION, "Testing edge %zu: not swapping.", e);
        return;
    }

    // List of at most 4 edges that have to be checked recursively after 'e' is swapped
    // FIXME: do we have to check all the 4 neighboring edges?
    size_t num_edges_to_check = 0;
    size_t edges_to_check[4];

    if (t0prev->e0 != e && t0prev->e0 != eskip1 && t0prev->e0 != eskip2)
        edges_to_check[num_edges_to_check++] = t0prev->e0;
    if (t0prev->e1 != e && t0prev->e1 != eskip1 && t0prev->e1 != eskip2)
        edges_to_check[num_edges_to_check++] = t0prev->e1;
    if (t0prev->e2 != e && t0prev->e2 != eskip1 && t0prev->e2 != eskip2)
        edges_to_check[num_edges_to_check++] = t0prev->e2;

    if (t1prev->e0 != e && t1prev->e0 != eskip1 && t1prev->e0 != eskip2)
        edges_to_check[num_edges_to_check++] = t1prev->e0;
    if (t1prev->e1 != e && t1prev->e1 != eskip1 && t1prev->e1 != eskip2)
        edges_to_check[num_edges_to_check++] = t1prev->e1;
    if (t1prev->e2 != e && t1prev->e2 != eskip1 && t1prev->e2 != eskip2)
        edges_to_check[num_edges_to_check++] = t1prev->e2;

    // 1) Determine the reference vertices for each triangle

    /*

        D---------C
       / t1 ___/ /
      /  __e    /
     / _/   t0 /
    A---------B

    B becomes t0refV
    D becomes t1refV

    */

    size_t t0refv; // The only vertex in 't0' which does not belong to 'e'
    size_t t1refv; // The only vertex in 't1' which does not belong to 'e'

    if (TRI_CONTAINS(t0prev, eprev.w0))
    {
        t0refv = eprev.w0;
        t1refv = eprev.w1;
    }
    else
    {
        t0refv = eprev.w1;
        t1refv = eprev.w0;
    }


    // 2) Update the triangles

    struct SKRY_triangle t0new, t1new;

    // For each of the new triangles, the reference vertex from step 1) and the next vertex stay the same as before the swap.
    // The third vertex (i.e. the one "previous" to the reference vertex) becomes the vertex opposite 'e', i.e. the other triangle's reference vertex.
    // Additionally, reorder the vertices such that the reference vertex becomes v0.

    /*

    Before:                    After:

        D---------C           D-------C
       / t1 ___/ /           / \  t0 /
      /  __e    /           /   e   /
     / _/   t0 /           / t1  \ /
    A---------B           A-------B

    t0: v0=A, v1=B, v2=C    t0: v0=B, v1=C, v2=D
    t1: v0=D, v1=A, v2=C    t1: v0=D, v1=A, v2=B

    */

    t0new.v0 = t0refv;
    t0new.v1 = next_vertex(t0prev, t0refv);
    t0new.v2 = t1refv;

    t1new.v0 = t1refv;
    t1new.v1 = next_vertex(t1prev, t1refv);
    t1new.v2 = t0refv;

    // For each new triangle, update their edges. The 'leading' edge of the reference vertex (now: e0) stays the same. The second edge comes
    // from the other triangle. The third edge is the new 'e' (after the swap).

    t0new.e0 = get_leading_edge_containing_vertex(t0prev, t0new.v0);
    t0new.e1 = get_leading_edge_containing_vertex(t1prev, t0new.v1);
    t0new.e2 = e;

    t1new.e0 = get_leading_edge_containing_vertex(t1prev, t1new.v0);
    t1new.e1 = get_leading_edge_containing_vertex(t0prev, t1new.v1);
    t1new.e2 = e;

    // For each of the 5 edges involved, update their adjacent triangles and opposing vertices information.
    // For 'e' after swap update also the end vertices.

    replace_opposing_vertex(&tri_edges[t0new.e0], t1new.v1, t0new.v2);
    replace_opposing_vertex(&tri_edges[t0new.e1], t1new.v1, t0new.v0);
    replace_opposing_vertex(&tri_edges[t1new.e0], t0new.v1, t1new.v2);
    replace_opposing_vertex(&tri_edges[t1new.e1], t0new.v1, t1new.v0);

    replace_adjacent_triangle(&tri_edges[t0new.e1], eprev.t1, eprev.t0);
    replace_adjacent_triangle(&tri_edges[t1new.e1], eprev.t0, eprev.t1);

    // update edge 'e' after swap
    tri_edges[e].w0 = t0new.v1;
    tri_edges[e].w1 = t1new.v1;

    tri_edges[e].v0 = t0new.v0;
    tri_edges[e].v1 = t1new.v0;

    // overwrite the old triangles
    *t0prev = t0new;
    *t1prev = t1new;

    for (size_t i = 0; i < num_edges_to_check; i++)
        LOG_MSG(SKRY_LOG_TRIANGULATION, "Edge %zu needs to be checked.", edges_to_check[i]);

    // Recursively check the affected edges
    for (size_t i = 0; i < num_edges_to_check; i++)
        test_and_swap_edge(tri, edges_to_check[i], e, SKRY_EMPTY);
}

/** Finds Delaunay triangulation for the specified point set; also adds (at the end
    of points' list) three additional points for the initial triangle which covers
    the whole set and 'envelope'. Returns null if out of memory. */
SKRY_Triangulation *SKRY_find_delaunay_triangulation(
                size_t num_points, struct SKRY_point points[],
                /// Must be big enough to cover the whole set of 'points'
                struct SKRY_rect envelope)
{
    SKRY_Triangulation *tri = malloc(sizeof(*tri));
    if (!tri)
        return 0;
    tri->num_vertices = num_points + 3;
    tri->vertices = malloc(tri->num_vertices * sizeof(struct SKRY_point));
    if (!tri->vertices)
    {
        free(tri);
        return 0;
    }
    memcpy(tri->vertices, points, num_points * sizeof(struct SKRY_point));

    DA_ALLOC(tri->edges, 0);
    DA_ALLOC(tri->triangles, 0);

    // Create the initial triangle which covers 'envelope' (which in turn must contain all of 'points[]');
    // append its vertices 'all_' at the end of the array
    struct SKRY_point *all0 = &tri->vertices[num_points + 0],
                      *all1 = &tri->vertices[num_points + 1],
                      *all2 = &tri->vertices[num_points + 2];

    all0->x = envelope.x - 15*envelope.height/10 - 16;
    all0->y = envelope.y - envelope.height/10 - 16;

    all1->x = envelope.x + envelope.width/2;
    all1->y = envelope.y + envelope.height + 15*envelope.width/10 + 16;

    all2->x = envelope.x + envelope.width + 15*envelope.height/10 + 16;
    all2->y = all0->y;

    // Initial triangle's edges
    LOG_MSG(SKRY_LOG_TRIANGULATION, "Added external vertex %zu = (%d, %d).", num_points + 0, all0->x, all0->y);
    LOG_MSG(SKRY_LOG_TRIANGULATION, "Added external vertex %zu = (%d, %d).", num_points + 1, all1->x, all1->y);
    LOG_MSG(SKRY_LOG_TRIANGULATION, "Added external vertex %zu = (%d, %d).", num_points + 2, all2->x, all2->y);

    DA_APPEND(tri->edges,
        ((struct SKRY_edge) { .v0 = num_points + 0, .v1 = num_points + 1,
                              .t0 = 0, .t1 = SKRY_EMPTY,
                              .w0 = num_points + 2, .w1 = SKRY_EMPTY }));

    DA_APPEND(tri->edges,
        ((struct SKRY_edge) { .v0 = num_points + 1, .v1 = num_points + 2,
                              .t0 = 0, .t1 = SKRY_EMPTY,
                              .w0 = num_points + 0, .w1 = SKRY_EMPTY }));

    DA_APPEND(tri->edges,
        ((struct SKRY_edge) { .v0 = num_points + 2, .v1 = num_points + 0,
                              .t0 = 0, .t1 = SKRY_EMPTY,
                              .w0 = num_points + 1, .w1 = SKRY_EMPTY }));

    DA_APPEND(tri->triangles,
        ((struct SKRY_triangle) { .v0 = num_points + 0, .v1 = num_points + 1, .v2 = num_points + 2,
                                  .e0 = 0, .e1 = 1, .e2 = 2 }));

    // Process subsequent points and incrementally refresh the triangulation

    for (size_t pidx = 0; pidx < num_points; pidx++)
    {
        // 1) Find an existing triangle 't' with index 'tidx' to which 'pidx' belongs
        size_t tidx = SKRY_EMPTY;
        for (size_t j = 0; j < DA_SIZE(tri->triangles); j++)
            if (is_inside_triangle(pidx, j, tri))
            {
                tidx = j;
                break;
            }

        assert(tidx != SKRY_EMPTY); // Will never happen, unless 'envelope' does not contain all 'points'

        // 2) Subdivide 't' into 3 sub-triangles 'tsub0', 'tsub1', 'tsub3' using 'pidx'
        //
        //    The order of existing triangles has to be preserved (they are referenced by the existing edges),
        //    so replace 't' by 'tsub0' and add 'tsub1' and 'tsub2' at the triangle array's end.

        struct SKRY_triangle tsub0;
        size_t tsub0idx = tidx;

        // Add 2 new triangles

        DA_SET_SIZE(tri->triangles, DA_SIZE(tri->triangles) + 2);
        size_t tsub1idx = DA_SIZE(tri->triangles) - 2;
        size_t tsub2idx = DA_SIZE(tri->triangles) - 1;

        struct SKRY_triangle *tsub1 = &tri->triangles.data[tsub1idx],
                               *tsub2 = &tri->triangles.data[tsub2idx];

        struct SKRY_triangle *t = &tri->triangles.data[tidx];

        // Add 3 new edges 'enew0', 'enew1', 'enew2' which connect 't.v0', 't.v1', 't.v2' with 'pidx'

        DA_APPEND(tri->edges, ((struct SKRY_edge) { .v0 = t->v0, .v1 = pidx, .t0 = tsub0idx, .t1 = tsub2idx, .w0 = t->v1, .w1 = t->v2 }));
        size_t enew0 = DA_SIZE(tri->edges) - 1;

        DA_APPEND(tri->edges, ((struct SKRY_edge) { .v0 = t->v1, .v1 = pidx, .t0 = tsub0idx, .t1 = tsub1idx, .w0 = t->v0, .w1 = t->v2 }));
        size_t enew1 = DA_SIZE(tri->edges) - 1;

        DA_APPEND(tri->edges, ((struct SKRY_edge) { .v0 = t->v2, .v1 = pidx, .t0 = tsub1idx, .t1 = tsub2idx, .w0 = t->v1, .w1 = t->v0 }));
        size_t enew2 = DA_SIZE(tri->edges) - 1;

        // Fill the new triangles' data

        tsub0.v0 = pidx;
        tsub0.v1 = t->v0;
        tsub0.v2 = t->v1;
        tsub0.e0 = enew0;
        tsub0.e1 = t->e0;
        tsub0.e2 = enew1;

        tsub1->v0 = pidx;
        tsub1->v1 = t->v1;
        tsub1->v2 = t->v2;
        tsub1->e0 = enew1;
        tsub1->e1 = t->e1;
        tsub1->e2 = enew2;

        tsub2->v0 = pidx;
        tsub2->v1 = t->v2;
        tsub2->v2 = t->v0;
        tsub2->e0 = enew2;
        tsub2->e1 = t->e2;
        tsub2->e2 = enew0;

        // Update adjacent triangle and opposing vertex data for 't's edges

        struct SKRY_edge *tri_edges = tri->edges.data;

        replace_opposing_vertex(&tri_edges[t->e0], t->v2, pidx);
        replace_adjacent_triangle(&tri_edges[t->e0], tidx, tsub0idx);

        replace_opposing_vertex(&tri_edges[t->e1], t->v0, pidx);
        replace_adjacent_triangle(&tri_edges[t->e1], tidx, tsub1idx);

        replace_opposing_vertex(&tri_edges[t->e2], t->v1, pidx);
        replace_adjacent_triangle(&tri_edges[t->e2], tidx, tsub2idx);

        // Keep note of the 't's edges for the subsequent Delaunay check
        size_t te0 = t->e0, te1 = t->e1, te2 = t->e2;

        // Original triangle 't' is no longer needed, replace it with 'tsub0'
        *t = tsub0;

        // 3) Check Delaunay condition for the old 't's edges and swap them if necessary.
        //    Also recursively check any edges affected by the swap.

        LOG_MSG(SKRY_LOG_TRIANGULATION, "Inserted point %zu (%d, %d) into triangle %zu.",
                pidx, tri->vertices[pidx].x, tri->vertices[pidx].y, tidx);

        test_and_swap_edge(tri, te0, enew0, enew1);
        test_and_swap_edge(tri, te1, enew1, enew2);
        test_and_swap_edge(tri, te2, enew2, enew0);
    }

    return tri;
}

/// Returns null
SKRY_Triangulation *SKRY_free_triangulation(SKRY_Triangulation *tri)
{
    if (tri)
    {
        free(tri->vertices);
        DA_FREE(tri->edges);
        DA_FREE(tri->triangles);
        free(tri);
    }
    return 0;
}

size_t SKRY_get_num_vertices(const SKRY_Triangulation *tri)
{
    return tri->num_vertices;
}

const struct SKRY_point *SKRY_get_vertices(const SKRY_Triangulation *tri)
{
    return tri->vertices;
}

const struct SKRY_edge *SKRY_get_edges(const SKRY_Triangulation *tri)
{
    return tri->edges.data;
}

const struct SKRY_triangle *SKRY_get_triangles(const SKRY_Triangulation *tri)
{
    return tri->triangles.data;
}

size_t SKRY_get_num_edges(const SKRY_Triangulation *tri)
{
    return DA_SIZE(tri->edges);
}

size_t SKRY_get_num_triangles(const SKRY_Triangulation *tri)
{
    return DA_SIZE(tri->triangles);
}

/// Finds barycentric coordinates of point 'p' in the triangle (v0, v1, v2) ('p' can be outside triangle)
void SKRY_calc_barycentric_coords(struct SKRY_point p,
                                  struct SKRY_point v0,
                                  struct SKRY_point v1,
                                  struct SKRY_point v2,
                                  float *u,
                                  float *v
)
{
    //FIXME: detect degenerate triangles
    *u = ((float)(v1.y - v2.y) * (p.x - v2.x) + (float)(v2.x - v1.x) * (p.y - v2.y)) / ((float)(v1.y - v2.y) * (v0.x - v2.x) + (float)(v2.x - v1.x) * (v0.y - v2.y));
    *v = ((float)(v2.y - v0.y) * (p.x - v2.x) + (float)(v0.x - v2.x) * (p.y - v2.y)) / ((float)(v1.y - v2.y) * (v0.x - v2.x) + (float)(v2.x - v1.x) * (v0.y - v2.y));
}

/// Finds barycentric coordinates of point 'p' in the triangle (v0, v1, v2) ('p' can be outside triangle)
void SKRY_calc_barycentric_coords_flt(struct SKRY_point p,
                                      struct SKRY_point_flt v0,
                                      struct SKRY_point_flt v1,
                                      struct SKRY_point_flt v2,
                                      float *u,
                                      float *v
)
{
    *u = ((v1.y - v2.y) * (p.x - v2.x) + (v2.x - v1.x) * (p.y - v2.y)) / ((v1.y - v2.y) * (v0.x - v2.x) + (v2.x - v1.x) * (v0.y - v2.y));
    *v = ((v2.y - v0.y) * (p.x - v2.x) + (v0.x - v2.x) * (p.y - v2.y)) / ((v1.y - v2.y) * (v0.x - v2.x) + (v2.x - v1.x) * (v0.y - v2.y));
}
