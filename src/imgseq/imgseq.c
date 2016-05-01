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


struct SKRY_img_sequence *SKRY_free_img_sequence(struct SKRY_img_sequence *img_seq)
{
    if (img_seq)
    {
        free(img_seq->is_img_active);
        img_seq->free(img_seq);
    }
    return 0;
}

size_t SKRY_get_curr_img_idx(const struct SKRY_img_sequence *img_seq)
{
    return img_seq->curr_image_idx;
}

size_t SKRY_get_img_count(const struct SKRY_img_sequence *img_seq)
{
    return img_seq->num_images;
}

void SKRY_seek_start(struct SKRY_img_sequence *img_seq)
{
    img_seq->curr_image_idx = 0;
    while (!img_seq->is_img_active[img_seq->curr_image_idx])
        img_seq->curr_image_idx++;

    img_seq->curr_img_idx_within_active_subset = 0;
}

SKRY_Image *SKRY_get_curr_img(const struct SKRY_img_sequence *img_seq,
                              enum SKRY_result *result)
{
    return img_seq->get_curr_img(img_seq, result);
}

enum SKRY_result SKRY_get_curr_img_metadata(const struct SKRY_img_sequence *img_seq,
                                            unsigned *width,
                                            unsigned *height,
                                            enum SKRY_pixel_format *pix_fmt)
{
    return img_seq->get_curr_img_metadata(img_seq, width, height, pix_fmt);
}

enum SKRY_result SKRY_seek_next(struct SKRY_img_sequence *img_seq)
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

SKRY_Image *SKRY_get_img_by_index(const struct SKRY_img_sequence *img_seq, size_t index,
                                  enum SKRY_result *result)
{
    return img_seq->get_img_by_index(img_seq, index, result);
}

void SKRY_deactivate_img_seq(struct SKRY_img_sequence *img_seq)
{
    img_seq->deactivate_img_seq(img_seq);
}

struct SKRY_img_sequence *SKRY_init_video_file(const char *file_name, enum SKRY_result *result)
{
    if (compare_extension(file_name, "avi"))
        return init_AVI(file_name, result);
    else if (compare_extension(file_name, "ser"))
        return init_SER(file_name, result);
    else
    {
        if (result) *result = SKRY_UNSUPPORTED_FILE_FORMAT;
        return 0;
    }
}

void base_init(struct SKRY_img_sequence *img_seq)
{
    img_seq->is_img_active = malloc(img_seq->num_images * sizeof(*img_seq->is_img_active));
    memset(img_seq->is_img_active, 1, img_seq->num_images);
    img_seq->last_active_idx = img_seq->num_images-1;
    img_seq->num_active_images = img_seq->num_images;
    SKRY_seek_start(img_seq);
}

void SKRY_set_active_imgs(struct SKRY_img_sequence *img_seq,
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

int SKRY_is_img_active(const struct SKRY_img_sequence *img_seq, size_t img_idx)
{
    return img_seq->is_img_active[img_idx];
}

const uint8_t *SKRY_get_img_active_flags(const struct SKRY_img_sequence *img_seq)
{
    return img_seq->is_img_active;
}

size_t SKRY_get_active_img_count(const struct SKRY_img_sequence *img_seq)
{
    return img_seq->num_active_images;
}

size_t SKRY_get_curr_img_idx_within_active_subset(const struct SKRY_img_sequence *img_seq)
{
    return img_seq->curr_img_idx_within_active_subset;
}

enum SKRY_img_sequence_type SKRY_get_img_seq_type(const struct SKRY_img_sequence *img_seq)
{
    return img_seq->type;
}

#define FAIL(error)                  \
    do {                             \
        SKRY_free_image(flatfield);  \
        if (result) *result = error; \
        return 0;                    \
    } while (0)

SKRY_Image *SKRY_create_flatfield(SKRY_ImgSequence *img_seq, enum SKRY_result *result)
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
            float_img = SKRY_convert_pix_fmt(img, SKRY_PIX_MONO32F);
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
