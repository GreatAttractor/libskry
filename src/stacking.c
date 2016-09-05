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
    Image stacking implementation.
*/

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <skry/defs.h>
#include <skry/image.h>
#include <skry/img_align.h>
#include <skry/imgseq.h>
#include <skry/ref_pt_align.h>
#include <skry/stacking.h>
#include <skry/triangulation.h>

#include "utils/dnarray.h"
#include "utils/logging.h"
#include "utils/misc.h"


struct stack_triangle_point
{
    int x, y;
    float u, v; // Barycentric coordinates in the parent triangle
};

typedef DA_DECLARE(struct stack_triangle_point) triangle_point_list_t;

struct SKRY_stacking
{
    const SKRY_RefPtAlignment *ref_pt_align;

    int is_complete;

    /// Number of triangles in 'ref_pt_align->triangulation'
    size_t num_triangles;

    /** For each triangle in 'ref_pt_align->triangulation', contains a list of
        points comprising it. */
    triangle_point_list_t *rasterized_tris;

    /** Final positions (within the images' intersection) of the reference points,
        i.e. the average over all images where the points are valid.
        Contains (at the end) 3 additional points - vertices of an all-encompassing triangle
        added during triangulation.
        */
    struct SKRY_point_flt *final_ref_pt_pos;

    size_t num_ref_points;

    /** Element [i] = number of images that were stacked to produce
        the i-th pixel in 'image_stack'. */
    unsigned *added_img_count;

    /// Format: SKRY_PIX_MONO32F or SKRY_PIX_RGB32F
    SKRY_Image *image_stack;

    int first_step_complete;

    /// Triangle indices (from 'ref_pt_align->triangulation') stacked in the current step
    DA_DECLARE(size_t) curr_step_stacked_triangles;

    /// Contains inverted flat-field values (1/flat-field)
    SKRY_Image *flatfield;

    struct
    {
        struct
        {
            double start; ///< Stored at the beginning of SKRY_init_stacking()
            double total_sec; ///< Difference between end of the last step and 'start'
        } time;
    } statistics;
};

typedef DA_DECLARE(int) *int_list_array_t;

/// Returns list of pixels belonging to triangle (v0, v1, v2)
static
triangle_point_list_t rasterize_triangle(
    struct SKRY_point_flt v0,
    struct SKRY_point_flt v1,
    struct SKRY_point_flt v2,
    struct SKRY_rect envelope,
    /** Contains as many pixels as 'envelope'; element value equals 1
        if a pixel has been already listed for another triangle
        (the current triangle will not include it). */
    uint8_t *pixel_occupied)
{
    /*
        Test every point of the rectangular axis-aligned bounding box of
        the triangle (v0, v1, v2) and if it is inside triangle, add it
        to the returned list.
    */

    triangle_point_list_t result;
    DA_ALLOC(result, 0);

    int xmin = INT_MAX, xmax = INT_MIN, ymin = INT_MAX, ymax = INT_MIN;

    if (v0.x < xmin) xmin = v0.x;
    if (v0.x > xmax) xmax = v0.x;
    if (v0.y < ymin) ymin = v0.y;
    if (v0.y > ymax) ymax = v0.y;

    if (v1.x < xmin) xmin = v1.x;
    if (v1.x > xmax) xmax = v1.x;
    if (v1.y < ymin) ymin = v1.y;
    if (v1.y > ymax) ymax = v1.y;

    if (v2.x < xmin) xmin = v2.x;
    if (v2.x > xmax) xmax = v2.x;
    if (v2.y < ymin) ymin = v2.y;
    if (v2.y > ymax) ymax = v2.y;

    for (int y = ymin; y <= ymax; y++)
        for (int x = xmin; x <= xmax; x++)
        {
            if (SKRY_RECT_CONTAINS(envelope, ((struct SKRY_point) { .x = x, .y = y })))
            {
                uint8_t *is_pix_occupied = &pixel_occupied[x - envelope.x + (y - envelope.y)*envelope.width];
                if (! *is_pix_occupied)
                {
                    float u, v;
                    SKRY_calc_barycentric_coords_flt((struct SKRY_point) { .x = x, .y = y },
                                                      v0, v1, v2, &u, &v);

                    if (u >= 0.0f && u <= 1.0f &&
                        v >= 0.0f && v <= 1.0f &&
                        u+v >= 0.0f && u+v <= 1.0f)
                    {
                        DA_APPEND(result, ((struct stack_triangle_point) { .x = x, .y = y, .u = u, .v = v }));
                        *is_pix_occupied = 1;
                    }
                }
            }
        }

    return result;
}

#define FAIL_ON_NULL(ptr)                         \
    if (!(ptr))                                   \
    {                                             \
        SKRY_free_stacking(stacking);             \
        if (result) *result = SKRY_OUT_OF_MEMORY; \
        return 0;                                 \
    }

SKRY_Stacking *SKRY_init_stacking(const SKRY_RefPtAlignment *ref_pt_align,
                                  /// May be null; no longer used after the function returns
                                  const SKRY_Image *flatfield,
                                  /// If not null, receives operation result
                                  enum SKRY_result *result)
{
    SKRY_ImgSequence *img_seq = SKRY_get_img_seq(SKRY_get_img_align(SKRY_get_qual_est(ref_pt_align)));
    SKRY_seek_start(img_seq);

    SKRY_Stacking *stacking = malloc(sizeof(*stacking));
    FAIL_ON_NULL(stacking);

    // Sets all pointer fields to null, so it is safe to call SKRY_free_stacking()
    // via FAIL_ON_NULL() in case one of allocation fails.
    *stacking = (SKRY_Stacking) { 0 };

    stacking->statistics.time.start = SKRY_clock_sec();
    stacking->ref_pt_align = ref_pt_align;
    stacking->final_ref_pt_pos = SKRY_get_final_positions(ref_pt_align, &stacking->num_ref_points);

    DA_ALLOC(stacking->curr_step_stacked_triangles, 0);

    const struct SKRY_triangulation *triangulation = SKRY_get_ref_pts_triangulation(stacking->ref_pt_align);
    stacking->num_triangles = SKRY_get_num_triangles(triangulation);
    stacking->rasterized_tris = malloc(stacking->num_triangles * sizeof(*stacking->rasterized_tris));
    FAIL_ON_NULL(stacking->rasterized_tris);

    const struct SKRY_triangle *triangulation_tris = SKRY_get_triangles(triangulation);
    struct SKRY_rect intersection = SKRY_get_intersection(SKRY_get_img_align(SKRY_get_qual_est(stacking->ref_pt_align)));
    uint8_t *pixel_occupied = malloc(intersection.width * intersection.height * sizeof(*pixel_occupied));
    FAIL_ON_NULL(pixel_occupied);
    memset(pixel_occupied, 0, intersection.width * intersection.height);
    for (size_t i = 0; i < stacking->num_triangles; i++)
    {
        stacking->rasterized_tris[i] =
            rasterize_triangle(
                stacking->final_ref_pt_pos[triangulation_tris[i].v0],
                stacking->final_ref_pt_pos[triangulation_tris[i].v1],
                stacking->final_ref_pt_pos[triangulation_tris[i].v2],
                (struct SKRY_rect) { .x = 0, .y = 0, .width = intersection.width, .height = intersection.height },
                pixel_occupied);
    }
    //TODO: see if after rasterization there are any pixels not belonging to any triangle and assign them

    enum SKRY_result loc_result;
    enum SKRY_pixel_format img_seq_pix_fmt;
    if (SKRY_SUCCESS != (loc_result = SKRY_get_curr_img_metadata(img_seq, 0, 0, &img_seq_pix_fmt)))
    {
        SKRY_free_stacking(stacking);
        if (result) *result = loc_result;
        return 0;
    }

    enum SKRY_pixel_format stack_pix_fmt =
        (NUM_CHANNELS[img_seq_pix_fmt] == 1 && (img_seq_pix_fmt < SKRY_PIX_CFA_MIN || img_seq_pix_fmt > SKRY_PIX_CFA_MAX) ?
            SKRY_PIX_MONO32F :
            SKRY_PIX_RGB32F);
    stacking->image_stack = SKRY_new_image(intersection.width, intersection.height, stack_pix_fmt, 0, 1);
    FAIL_ON_NULL(stacking->image_stack);

    stacking->added_img_count = malloc(intersection.width * intersection.height * sizeof(*stacking->added_img_count));
    FAIL_ON_NULL(stacking->added_img_count);
    memset(stacking->added_img_count, 0,  intersection.width * intersection.height * sizeof(*stacking->added_img_count));

    if (flatfield)
    {
        if (SKRY_get_img_pix_fmt(flatfield) == SKRY_PIX_MONO32F)
            stacking->flatfield = SKRY_get_img_copy(flatfield);
        else
            stacking->flatfield = SKRY_convert_pix_fmt(flatfield, SKRY_PIX_MONO32F, SKRY_DEMOSAIC_HQLINEAR);

        FAIL_ON_NULL(stacking->flatfield);

        unsigned width = SKRY_get_img_width(stacking->flatfield),
                 height = SKRY_get_img_height(stacking->flatfield);
        float max_val = FLT_MIN;
        for (unsigned y = 0; y < height; y++)
        {
            float *line = SKRY_get_line(stacking->flatfield, y);
            for (unsigned x = 0; x < width; x++)
                if (line[x] > max_val)
                    max_val = line[x];
        }

        for (unsigned y = 0; y < height; y++)
        {
            float *line = SKRY_get_line(stacking->flatfield, y);
            for (unsigned x = 0; x < width; x++)
                if (line[x] > 0.0f)
                    line[x] = max_val/line[x];
        }
    }

    if (result) *result = SKRY_SUCCESS;
    return stacking;
}

/// Returns null
SKRY_Stacking *SKRY_free_stacking(SKRY_Stacking *stacking)
{
    if (stacking)
    {
        if (stacking->rasterized_tris)
        {
            for (size_t i = 0; i < stacking->num_triangles; i++)
                DA_FREE(stacking->rasterized_tris[i]);

            free(stacking->rasterized_tris);
        }

        free(stacking->final_ref_pt_pos);
        DA_FREE(stacking->curr_step_stacked_triangles);
        SKRY_free_image(stacking->image_stack);
        free(stacking->added_img_count);
        SKRY_free_image(stacking->flatfield);
        free(stacking);
    }
    return 0;
}

/// Performs linear interpolation in 'img' (which has to be 32-bit floating point)
static
float interpolate_pixel_value(const float *pixels, ptrdiff_t line_stride_in_bytes,
                              unsigned img_width, unsigned img_height,
                              float x, float y, size_t channel, size_t bytes_per_pix)
{
    if (x < 0 || x >= img_width-1 || y < 0 || y >= img_height-1)
        return 0.0f;

    double x0d, y0d;
    float tx = modf(x, &x0d);
    float ty = modf(y, &y0d);
    int x0 = (int)x0d, y0 = (int)y0d;

    float * restrict line_lo = (float *)((uint8_t *)pixels + y0*line_stride_in_bytes);
    float * restrict line_hi = (float *)((uint8_t *)line_lo + line_stride_in_bytes);
    float v00 = ((float *)((char *)line_lo + x0    *bytes_per_pix))[channel],
          v10 = ((float *)((char *)line_lo + (x0+1)*bytes_per_pix))[channel],
          v01 = ((float *)((char *)line_hi + x0    *bytes_per_pix))[channel],
          v11 = ((float *)((char *)line_hi + (x0+1)*bytes_per_pix))[channel];

    return (1.0f-ty) * ((1.0f-tx)*v00 + tx*v10) + ty * ((1.0-tx)*v01 + tx*v11);
}

static
void normalize_image_stack(
    // element value = number of triangles stacked for the corresponding pixel in 'img_stack'
    const unsigned added_img_count[],
    SKRY_Image *img_stack, int uses_flatfield)
{
    unsigned width = SKRY_get_img_width(img_stack),
             height = SKRY_get_img_height(img_stack);

    size_t num_channels = NUM_CHANNELS[SKRY_get_img_pix_fmt(img_stack)];

    float max_stack_value = 0.0f;
    for (unsigned y = 0; y < height; y++)
    {
        float *line = SKRY_get_line(img_stack, y);
            for (unsigned x = 0; x < width; x++)
                for (size_t ch = 0; ch < num_channels; ch++)
                {
                    float *val = &line[num_channels*x + ch];
                    *val /= (SKRY_MAX(1, added_img_count[x + y*width]));
                    if (uses_flatfield && *val > max_stack_value)
                        max_stack_value = *val;
                }
    }

    if (uses_flatfield && max_stack_value > 0.0f)
        for (unsigned y = 0; y < height; y++)
        {
            float *line = SKRY_get_line(img_stack, y);
            for (unsigned x = 0; x < width; x++)
                for (size_t ch = 0; ch < num_channels; ch++)
                    line[num_channels*x + ch] /= max_stack_value;
        }
}

/// Returns SKRY_SUCCESS (i.e. more steps left to do), SKRY_LAST_STEP (no more steps) or an error
enum SKRY_result SKRY_stacking_step(SKRY_Stacking *stacking)
{
    enum SKRY_result result;
    SKRY_ImgSequence *img_seq = SKRY_get_img_seq(SKRY_get_img_align(SKRY_get_qual_est(stacking->ref_pt_align)));

    if (stacking->first_step_complete)
    {
        result = SKRY_seek_next(img_seq);
        if (SKRY_NO_MORE_IMAGES == result)
        {
            normalize_image_stack(stacking->added_img_count, stacking->image_stack,
                                  0 != stacking->flatfield);

            stacking->statistics.time.total_sec = SKRY_clock_sec() - stacking->statistics.time.start;
            LOG_MSG(SKRY_LOG_STACKING, "Processing time: %.3f s", stacking->statistics.time.total_sec);

            stacking->is_complete = 1;
            return SKRY_LAST_STEP;
        }
        else if (SKRY_SUCCESS != result)
        {
            LOG_MSG(SKRY_LOG_STACKING, "Could not seek to the next image of image sequence %p (error: %d).",
                    (void *)img_seq, (int)result);
            return result;
        }
    }

    SKRY_Image *img = SKRY_get_curr_img(img_seq, &result);
    size_t curr_img_idx = SKRY_get_curr_img_idx_within_active_subset(img_seq);
    if (SKRY_SUCCESS != result)
    {
        LOG_MSG(SKRY_LOG_STACKING, "Could not load image %zu from image sequence %p (error: %d).",
                                 SKRY_get_curr_img_idx(img_seq),      (void *)img_seq, (int)result);
        return result;
    }

    struct SKRY_rect intersection = SKRY_get_intersection(SKRY_get_img_align(SKRY_get_qual_est(stacking->ref_pt_align)));
    struct SKRY_point alignment_ofs = SKRY_get_image_ofs(SKRY_get_img_align(SKRY_get_qual_est(stacking->ref_pt_align)), curr_img_idx);

    if (SKRY_get_img_pix_fmt(img) != SKRY_get_img_pix_fmt(stacking->image_stack))
    {
        SKRY_Image *img32f = SKRY_convert_pix_fmt(img, SKRY_get_img_pix_fmt(stacking->image_stack), SKRY_DEMOSAIC_HQLINEAR);
        SKRY_free_image(img);
        img = img32f;
    }

    unsigned width = SKRY_get_img_width(img),
             height = SKRY_get_img_height(img);
    size_t num_channels = NUM_CHANNELS[SKRY_get_img_pix_fmt(img)],
           bytes_per_pix = BYTES_PER_PIXEL[SKRY_get_img_pix_fmt(img)];

    unsigned ff_width = 0, ff_height = 0;
    if (stacking->flatfield)
    {
        ff_width = SKRY_get_img_width(stacking->flatfield);
        ff_height = SKRY_get_img_height(stacking->flatfield);
    }

    // For each triangle, check if its vertices are valid in the current image. If they are,
    // add the triangle's contents to the corresponding triangle patch in the stack.
    DA_SET_SIZE(stacking->curr_step_stacked_triangles, 0);
    const struct SKRY_triangulation *triangulation = SKRY_get_ref_pts_triangulation(stacking->ref_pt_align);

    struct SKRY_rect envelope = { .x = 0, .y = 0,
                                  .width = intersection.width,
                                  .height = intersection.height };

    // First, find the list of triangles valid in the current step
    for (size_t tri_idx = 0; tri_idx < SKRY_get_num_triangles(triangulation); tri_idx++)
    {
        const struct SKRY_triangle *tri = &SKRY_get_triangles(triangulation)[tri_idx];

        struct
        {
            struct SKRY_point pos; // position of triangle's vertex in the current image
            int is_valid;
        } p0 = { .pos = SKRY_get_ref_pt_pos(stacking->ref_pt_align, tri->v0, curr_img_idx, &p0.is_valid) },
          p1 = { .pos = SKRY_get_ref_pt_pos(stacking->ref_pt_align, tri->v1, curr_img_idx, &p1.is_valid) },
          p2 = { .pos = SKRY_get_ref_pt_pos(stacking->ref_pt_align, tri->v2, curr_img_idx, &p2.is_valid) };

        if (p0.is_valid && p1.is_valid && p2.is_valid)
        {
            // Due to the way reference point alignment works, it is allowed for a point
            // to be outside the image intersection at some times. Must be careful not to
            // try interpolating pixel values from outside the current image.
            // (Cannot use 'intersection' here directly, because its origin may not be (0,0),
            // and p0, p1, p2 have coordinates relative to intersection's origin).
            int p0_inside = SKRY_RECT_CONTAINS(envelope, p0.pos);
            int p1_inside = SKRY_RECT_CONTAINS(envelope, p1.pos);
            int p2_inside = SKRY_RECT_CONTAINS(envelope, p2.pos);

            if (p0_inside || p1_inside || p2_inside)
                DA_APPEND(stacking->curr_step_stacked_triangles, tri_idx);
        }
    }

    // Second, stack the triangles
    float * restrict src_pixels = SKRY_get_line(img, 0);
    ptrdiff_t src_stride = SKRY_get_line_stride_in_bytes(img);

    float * restrict flatf_pixels = 0;
    ptrdiff_t flatf_stride = 0;
    if (stacking->flatfield)
    {
        flatf_pixels = SKRY_get_line(stacking->flatfield, 0);
        flatf_stride = SKRY_get_line_stride_in_bytes(stacking->flatfield);
    }

    float * restrict stack_pixels = SKRY_get_line(stacking->image_stack, 0);
    ptrdiff_t stack_stride = SKRY_get_line_stride_in_bytes(stacking->image_stack);

    #pragma omp parallel for
    for (size_t i = 0; i < DA_SIZE(stacking->curr_step_stacked_triangles); i++)
    {
        size_t tri_idx = stacking->curr_step_stacked_triangles.data[i];

        const struct SKRY_triangle *tri = &SKRY_get_triangles(triangulation)[tri_idx];

        struct
        {
            struct SKRY_point pos; // position of triangle's vertex in the current image
            int is_valid;
        } p0 = { .pos = SKRY_get_ref_pt_pos(stacking->ref_pt_align, tri->v0, curr_img_idx, &p0.is_valid) },
          p1 = { .pos = SKRY_get_ref_pt_pos(stacking->ref_pt_align, tri->v1, curr_img_idx, &p1.is_valid) },
          p2 = { .pos = SKRY_get_ref_pt_pos(stacking->ref_pt_align, tri->v2, curr_img_idx, &p2.is_valid) };

        int p0_inside = SKRY_RECT_CONTAINS(envelope, p0.pos);
        int p1_inside = SKRY_RECT_CONTAINS(envelope, p1.pos);
        int p2_inside = SKRY_RECT_CONTAINS(envelope, p2.pos);
        int all_inside = p0_inside && p1_inside && p2_inside;

        for (size_t p = 0; p < DA_SIZE(stacking->rasterized_tris[tri_idx]); p++)
        {
            const struct stack_triangle_point *stp = &stacking->rasterized_tris[tri_idx].data[p];

            float srcx = stp->u * p0.pos.x +
                         stp->v * p1.pos.x +
                         (1.0f - stp->u - stp->v) * p2.pos.x;
            float srcy = stp->u * p0.pos.y +
                         stp->v * p1.pos.y +
                         (1.0f - stp->u - stp->v) * p2.pos.y;

            if (all_inside ||
                (srcx >= 0 && srcx <= intersection.width-1 &&
                 srcy >= 0 && srcy <= intersection.height-1))
            {
                unsigned ffx = 0, ffy = 0;
                if (stacking->flatfield)
                {
                    ffx = SKRY_MIN(srcx + intersection.x + alignment_ofs.x, ff_width-1);
                    ffy = SKRY_MIN(srcy + intersection.y + alignment_ofs.y, ff_height-1);
                }

                for (size_t ch = 0; ch < num_channels; ch++)
                {
                    float src_val =
                        interpolate_pixel_value(src_pixels, src_stride, width, height,
                                                srcx + intersection.x + alignment_ofs.x,
                                                srcy + intersection.y + alignment_ofs.y,
                                                ch, bytes_per_pix);

                    if (stacking->flatfield)
                    {
                        // 'stacking->flatfield' contains inverted flat-field values,
                        // so we multiply instead of dividing
                        src_val *= ((float*)((uint8_t *)flatf_pixels + ffy*flatf_stride))[ffx];
                    }

                    ((float *)((uint8_t *)stack_pixels + stack_stride*stp->y))[num_channels*stp->x + ch] += src_val;
                }

                stacking->added_img_count[stp->x + stp->y*intersection.width] += 1;
            }
        }
    }

    SKRY_free_image(img);

    if (!stacking->first_step_complete)
        stacking->first_step_complete = 1;

    return SKRY_SUCCESS;
}

/// Can be used only after stacking completes
const SKRY_Image *SKRY_get_image_stack(const SKRY_Stacking *stacking)
{
    if (stacking->is_complete)
        return stacking->image_stack;
    else
        return 0;
}

/// Returns an incomplete image stack, updated after every stacking step
SKRY_Image *SKRY_get_partial_image_stack(const SKRY_Stacking *stacking)
{
    SKRY_Image *result = SKRY_get_img_copy(stacking->image_stack);
    normalize_image_stack(stacking->added_img_count, result,
                          0 != stacking->flatfield);
    return result;
}

int SKRY_is_stacking_complete(const SKRY_Stacking *stacking)
{
    return stacking->is_complete;
}

/// Returns an array of triangle indices stacked in current step
/** Meant to be called right after SKRY_stacking_step(). Values are indices into triangle array
    of the triangulation returned by SKRY_get_ref_pts_triangulation(). Vertex coordinates do not
    correspond with the triangulation, but with the array returned by 'SKRY_get_ref_pt_stacking_pos'. */
const size_t *SKRY_get_curr_step_stacked_triangles(
                const SKRY_Stacking *stacking,
                /// Receives length of the returned array
                size_t *num_triangles)
{
    *num_triangles = DA_SIZE(stacking->curr_step_stacked_triangles);
    return stacking->curr_step_stacked_triangles.data;
}

/// Returns reference point positions as used during stacking
const struct SKRY_point_flt *SKRY_get_ref_pt_stacking_pos(const SKRY_Stacking *stacking)
{
    return stacking->final_ref_pt_pos;
}
