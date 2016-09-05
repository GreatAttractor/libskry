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
    Image sequence implementation.
*/

#include <float.h>
#include <stdlib.h>
#include <string.h>

#include <skry/imgseq.h>

#include "imgseq_internal.h"
#include "video.h"
#include "../utils/misc.h"


/// Returns null
SKRY_ImgSequence *SKRY_free_img_sequence(SKRY_ImgSequence *img_seq)
{
    if (img_seq)
    {
        SKRY_disconnect_from_img_pool(img_seq);
        free(img_seq->is_img_active);
        img_seq->free(img_seq);
    }
    return 0;
}

/// Returns absolute index (refers to the whole set, including non-active images)
size_t SKRY_get_curr_img_idx(const SKRY_ImgSequence *img_seq)
{
    return img_seq->curr_image_idx;
}

size_t SKRY_get_img_count(const SKRY_ImgSequence *img_seq)
{
    return img_seq->num_images;
}

/// Seeks to the first active image
void SKRY_seek_start(SKRY_ImgSequence *img_seq)
{
    img_seq->curr_image_idx = 0;
    while (!img_seq->is_img_active[img_seq->curr_image_idx])
        img_seq->curr_image_idx++;

    img_seq->curr_img_idx_within_active_subset = 0;
}

SKRY_Image *SKRY_get_curr_img(const SKRY_ImgSequence *img_seq,
                              enum SKRY_result *result ///< If not null, receives operation result
)
{
    SKRY_Image *img = img_seq->get_curr_img(img_seq, result);
    if (img)
    {
        enum SKRY_pixel_format pix_fmt = SKRY_get_img_pix_fmt(img);
        if (img_seq->CFA_pattern != SKRY_CFA_NONE &&
            (pix_fmt == SKRY_PIX_MONO8 ||
             pix_fmt == SKRY_PIX_MONO16 ||
             pix_fmt > SKRY_PIX_CFA_MIN && pix_fmt < SKRY_PIX_CFA_MAX))
        {
            SKRY_reinterpret_as_CFA(img, img_seq->CFA_pattern);
        }
    }
    return img;
}

enum SKRY_result SKRY_get_curr_img_metadata(const SKRY_ImgSequence *img_seq,
                                        unsigned *width,  ///< If not null, receives current image's width
                                        unsigned *height, ///< If not null, receives current image's height
                                        enum SKRY_pixel_format *pix_fmt ///< If not null, receives current image's pixel format
)
{
    enum SKRY_result result = img_seq->get_curr_img_metadata(img_seq, width, height, pix_fmt);
    if (SKRY_SUCCESS == result)
    {
        if (img_seq->CFA_pattern != SKRY_CFA_NONE && pix_fmt)
        {
            if (*pix_fmt == SKRY_PIX_MONO8 ||
                *pix_fmt > SKRY_PIX_CFA_MIN && *pix_fmt < SKRY_PIX_CFA_MAX && BITS_PER_CHANNEL[*pix_fmt] == 8)
                switch (img_seq->CFA_pattern)
                {
                    case SKRY_CFA_BGGR: *pix_fmt = SKRY_PIX_CFA_BGGR8; break;
                    case SKRY_CFA_GBRG: *pix_fmt = SKRY_PIX_CFA_GBRG8; break;
                    case SKRY_CFA_GRBG: *pix_fmt = SKRY_PIX_CFA_GRBG8; break;
                    case SKRY_CFA_RGGB: *pix_fmt = SKRY_PIX_CFA_RGGB8; break;
                    default: break;
                }
            else if (*pix_fmt == SKRY_PIX_MONO16 ||
                     *pix_fmt > SKRY_PIX_CFA_MIN && *pix_fmt < SKRY_PIX_CFA_MAX && BITS_PER_CHANNEL[*pix_fmt] == 16)
                switch (img_seq->CFA_pattern)
                {
                    case SKRY_CFA_BGGR: *pix_fmt = SKRY_PIX_CFA_BGGR16; break;
                    case SKRY_CFA_GBRG: *pix_fmt = SKRY_PIX_CFA_GBRG16; break;
                    case SKRY_CFA_GRBG: *pix_fmt = SKRY_PIX_CFA_GRBG16; break;
                    case SKRY_CFA_RGGB: *pix_fmt = SKRY_PIX_CFA_RGGB16; break;
                    default: break;
                }
        }

    }
    return result;
}

/// Seeks forward to the next active image; returns SKRY_SUCCESS or SKRY_NO_MORE_IMAGES
enum SKRY_result SKRY_seek_next(SKRY_ImgSequence *img_seq)
{
    if (img_seq->curr_image_idx < img_seq->last_active_idx)
    {
        do
        {
            img_seq->curr_image_idx++;
        }
        while (!img_seq->is_img_active[img_seq->curr_image_idx]);

        img_seq->curr_img_idx_within_active_subset++;

        return SKRY_SUCCESS;
    }
    else
        return SKRY_NO_MORE_IMAGES;
}

SKRY_Image *SKRY_get_img_by_index(const SKRY_ImgSequence *img_seq, size_t index,
                                  enum SKRY_result *result ///< If not null, receives operation result
)
{
    SKRY_Image *img = img_seq->get_img_by_index(img_seq, index, result);
    if (img)
    {
        enum SKRY_pixel_format pix_fmt = SKRY_get_img_pix_fmt(img);
        if (img_seq->CFA_pattern != SKRY_CFA_NONE &&
            (pix_fmt == SKRY_PIX_MONO8 || pix_fmt == SKRY_PIX_MONO16 ||
             pix_fmt > SKRY_PIX_CFA_MIN && pix_fmt < SKRY_PIX_CFA_MAX))
        {
            SKRY_reinterpret_as_CFA(img, img_seq->CFA_pattern);
        }
    }
    return img;
}

/// Should be called when 'img_seq' will not be read for some time
/** In case of image lists, the function does nothing. For video files, it closes them.
    Video files are opened automatically (and kept open) every time a frame is loaded. */
void SKRY_deactivate_img_seq(SKRY_ImgSequence *img_seq)
{
    img_seq->deactivate_img_seq(img_seq);
}

SKRY_ImgSequence *SKRY_init_video_file(
    const char *file_name,
    /** If not null, will be used to keep converted images in memory for use
        by subsequent processing phases. */
    SKRY_ImagePool *img_pool,
    /// If not null, receives operation result
    enum SKRY_result *result)
{
    if (compare_extension(file_name, "avi"))
        return init_AVI(file_name, img_pool, result);
    else if (compare_extension(file_name, "ser"))
        return init_SER(file_name, img_pool, result);
    else
    {
        if (result) *result = SKRY_UNSUPPORTED_FILE_FORMAT;
        return 0;
    }
}

/// Must be called after img_seq->num_images has been set
void base_init(SKRY_ImgSequence *img_seq,
               /// May be null
               SKRY_ImagePool *img_pool)
{
    img_seq->is_img_active = malloc(img_seq->num_images * sizeof(*img_seq->is_img_active));
    memset(img_seq->is_img_active, 1, img_seq->num_images);
    img_seq->last_active_idx = img_seq->num_images-1;
    img_seq->num_active_images = img_seq->num_images;
    SKRY_seek_start(img_seq);
    if (img_pool)
    {
        img_seq->img_pool = img_pool;
        img_seq->pool_node = connect_img_sequence(img_pool, img_seq);
    }
    img_seq->CFA_pattern = SKRY_CFA_NONE;
}

void SKRY_set_active_imgs(SKRY_ImgSequence *img_seq,
                          /** Element count = number of images in 'img_seq'.
                              Non-zero values indicate active images. */
                          const uint8_t *active_imgs)
{
    memcpy(img_seq->is_img_active, active_imgs, img_seq->num_images);
    img_seq->num_active_images = 0;
    for (size_t i = 0; i < img_seq->num_images; i++)
    {
        if (img_seq->is_img_active[i])
        {
            img_seq->last_active_idx = i;
            img_seq->num_active_images++;
        }
    }
}

int SKRY_is_img_active(const SKRY_ImgSequence *img_seq, size_t img_idx)
{
    return img_seq->is_img_active[img_idx];
}

/// Element count of result = number of images in 'img_seq'
const uint8_t *SKRY_get_img_active_flags(const SKRY_ImgSequence *img_seq)
{
    return img_seq->is_img_active;
}

size_t SKRY_get_active_img_count(const SKRY_ImgSequence *img_seq)
{
    return img_seq->num_active_images;
}

size_t SKRY_get_curr_img_idx_within_active_subset(const SKRY_ImgSequence *img_seq)
{
    return img_seq->curr_img_idx_within_active_subset;
}

enum SKRY_img_sequence_type SKRY_get_img_seq_type(const SKRY_ImgSequence *img_seq)
{
    return img_seq->type;
}

/// Disconnects 'img_seq' from the image pool that was specified during 'img_seq's creation (if any)
void SKRY_disconnect_from_img_pool(SKRY_ImgSequence *img_seq)
{
    if (img_seq->img_pool)
    {
        disconnect_img_sequence(img_seq->img_pool, img_seq->pool_node);
        img_seq->img_pool = 0;
        img_seq->pool_node = 0;
    }
}

#define FAIL(error)                  \
    do {                             \
        SKRY_free_image(flatfield);  \
        if (result) *result = error; \
        return 0;                    \
    } while (0)

SKRY_Image *SKRY_create_flatfield(
    /// All images must have the same size
    SKRY_ImgSequence *img_seq,
    /// If not null, receives operation result
    enum SKRY_result *result)
{
    SKRY_Image *flatfield = 0;
    unsigned width, height;

    SKRY_seek_start(img_seq);
    do
    {
        enum SKRY_result loc_result;
        SKRY_Image *img = SKRY_get_curr_img(img_seq, &loc_result);
        if (!img)
            FAIL(loc_result);

        if (flatfield && (SKRY_get_img_width(img) != width ||
                          SKRY_get_img_height(img) != height))
        {
            FAIL(SKRY_INVALID_IMG_DIMENSIONS);
        }
        else if (!flatfield)
        {
            width = SKRY_get_img_width(img);
            height = SKRY_get_img_height(img);
            flatfield = SKRY_new_image(width, height, SKRY_PIX_MONO32F, 0, 1);
        }

        SKRY_Image *float_img = img;
        if (SKRY_get_img_pix_fmt(img) != SKRY_PIX_MONO32F)
        {
            float_img = SKRY_convert_pix_fmt(img, SKRY_PIX_MONO32F, SKRY_DEMOSAIC_HQLINEAR);
            SKRY_free_image(img);
        }

        for (unsigned y = 0; y < height; y++)
        {
            float *l_flat = SKRY_get_line(flatfield, y);
            float *l_img = SKRY_get_line(float_img, y);
            for (unsigned x = 0; x < width; x++)
                l_flat[x] += l_img[x];
        }
        SKRY_free_image(float_img);
    } while (SKRY_SUCCESS == SKRY_seek_next(img_seq));

    float max_val = FLT_MIN;
    for (unsigned y = 0; y < height; y++)
    {
        float *line = SKRY_get_line(flatfield, y);
        for (unsigned x = 0; x < width; x++)
            if (line[x] > max_val)
                max_val = line[x];
    }

    for (unsigned y = 0; y < height; y++)
    {
        float *line = SKRY_get_line(flatfield, y);
        for (unsigned x = 0; x < width; x++)
            // Store an inverted value, so that during stacking
            // we can multiply instead of dividing
            line[x] *= 1.0f/max_val;
    }

    if (result) *result = SKRY_SUCCESS;
    return flatfield;
}

#undef FAIL

/// Returns the current image in specified format
/** If 'img_seq' is connected to an image pool, the image is taken from the pool
    if it exists there (and has 'pix_fmt'). If the image is not yet in the pool,
    it will be converted to 'pix_fmt' and stored there, if the pool's capacity
    is not yet exhausted. Otherwise, the image is read as usual and converted
    without being added to the pool.
    In any case, once the caller is done with the image, it *has to* call
    'SKRY_release_img_to_pool' and must not attempt to free the returned image. */
SKRY_Image *SKRY_get_curr_img_from_pool(
              const SKRY_ImgSequence *img_seq,
              enum SKRY_pixel_format pix_fmt,
              /// Used if source image contains raw color data
              enum SKRY_demosaic_method demosaic_method,
              enum SKRY_result *result ///< If not null, receives operation result
)
{
    if (result) *result = SKRY_SUCCESS;

    if (img_seq->img_pool)
    {
        SKRY_Image *img = get_image_from_pool(img_seq->img_pool,
                                              img_seq->pool_node,
                                              img_seq->curr_image_idx);

        if (!img)
        {
            img = SKRY_get_curr_img(img_seq, result);
            if (!img)
                return 0;

            if (SKRY_get_img_pix_fmt(img) != pix_fmt)
            {
                SKRY_Image *img_conv = SKRY_convert_pix_fmt(img, pix_fmt, demosaic_method);
                SKRY_free_image(img);
                if (!img_conv)
                {
                    if (result) *result = SKRY_OUT_OF_MEMORY;
                    return 0;
                }
                else
                    img = img_conv;
            }

            put_image_in_pool(img_seq->img_pool, img_seq->pool_node,
                              img_seq->curr_image_idx, img);

            return img;

        }
        else
        {
            if (SKRY_get_img_pix_fmt(img) != pix_fmt)
            {
                img = SKRY_convert_pix_fmt(img, pix_fmt, demosaic_method);
                if (!img)
                {
                    if (result) *result = SKRY_OUT_OF_MEMORY;
                    return 0;
                }
                else
                    put_image_in_pool(img_seq->img_pool, img_seq->pool_node,
                                      img_seq->curr_image_idx, img);
            }

            return img;
        }
    }
    else
    {
        SKRY_Image *img = SKRY_get_curr_img(img_seq, result);
        if (SKRY_get_img_pix_fmt(img) != pix_fmt)
        {
            SKRY_Image *img_conv = SKRY_convert_pix_fmt(img, pix_fmt, demosaic_method);
            SKRY_free_image(img);
            if (!img_conv)
            {
                if (result) *result = SKRY_OUT_OF_MEMORY;
                return 0;
            }
            else
                img = img_conv;
        }

        return img;
    }
}

/** Has to be called for the image returned by 'SKRY_get_curr_img_from_pool',
    when the image is no longer needed by the caller. */
void SKRY_release_img_to_pool(const SKRY_ImgSequence *img_seq, size_t img_idx, SKRY_Image *image)
{
    if (img_seq->img_pool &&
        get_image_from_pool(img_seq->img_pool, img_seq->pool_node, img_idx) == image)
    {
        // do nothing
    }
    else
        SKRY_free_image(image);
}

/// Treat mono images in 'img_seq' as containing raw color data
/** Applies only to 8/16-bit mono and raw color images. Only pixel format is updated,
    the pixel data is unchanged. To demosaic, use one of the pixel format
    conversion functions.
    For SER videos marked as raw color, there is no need to call this function.
    However, it can be used to override the CFA pattern indicated in the SER header. */
void SKRY_reinterpret_img_seq_as_CFA(
         SKRY_ImgSequence *img_seq,
         /// Specify SKRY_CFA_NONE to disable pixel format overriding
         enum SKRY_CFA_pattern CFA_pattern)
{
    img_seq->CFA_pattern = CFA_pattern;
}
