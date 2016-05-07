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
    Image list implementation.
*/

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <skry/defs.h>
#include <skry/imgseq.h>

#include "../utils/logging.h"
#include "imgseq_internal.h"


struct image_list_data
{
    char **file_names; ///< Array of 'num_images' string pointers

    //TODO: move this mechanism to base class (?)
    SKRY_Image *last_loaded_img;
    size_t last_loaded_img_idx; // May be SKRY_EMPTY

    size_t next_file_name_idx_to_add;
};

/// 'img_seq' is a pointer to 'struct SKRY_img_sequence'
#define IMG_LIST_DATA(img_seq) ((struct image_list_data *)img_seq->data)

static
void image_list_free(struct SKRY_img_sequence *img_seq)
{
    if (img_seq)
    {
        if (img_seq->data)
        {
            for (size_t i = 0; i < img_seq->num_images; i++)
                free(IMG_LIST_DATA(img_seq)->file_names[i]);

            free(IMG_LIST_DATA(img_seq)->file_names);
            free(IMG_LIST_DATA(img_seq)->last_loaded_img);
            free(img_seq->data);
        }
        free(img_seq);
    }
}

static
SKRY_Image *image_list_get_img(
    const struct SKRY_img_sequence *img_seq,
    size_t img_idx,
    enum SKRY_result *result ///< If not null, receives operation result
)
{
    struct image_list_data *data = IMG_LIST_DATA(img_seq);

    if (data->last_loaded_img_idx != SKRY_EMPTY
        && (size_t)data->last_loaded_img_idx == img_idx
        && data->last_loaded_img)
    {
        if (result)
            *result = SKRY_SUCCESS;
        return SKRY_get_img_copy(data->last_loaded_img);
    }
    else
    {
        const char *file_name = data->file_names[img_idx];

        SKRY_Image *loaded_img = SKRY_load_image(file_name, result);
        if (loaded_img)
        {
            SKRY_free_image(data->last_loaded_img);
            data->last_loaded_img = loaded_img;
            return SKRY_get_img_copy(data->last_loaded_img);
        }
        else
            return 0;
    }
}

static
SKRY_Image *image_list_get_curr_img(
    const struct SKRY_img_sequence *img_seq,
    enum SKRY_result *result ///< If not null, receives operation result
)
{
    return image_list_get_img(img_seq, SKRY_get_curr_img_idx(img_seq), result);
}

static
enum SKRY_result image_list_get_curr_img_metadata(
    const struct SKRY_img_sequence *img_seq,
    unsigned *width,
    unsigned *height,
    enum SKRY_pixel_format *pix_fmt)
{
    struct image_list_data *data = IMG_LIST_DATA(img_seq);

    if (data->last_loaded_img_idx != SKRY_EMPTY
        && (size_t)data->last_loaded_img_idx == img_seq->curr_image_idx
        && data->last_loaded_img)
    {
        if (width)
            *width = SKRY_get_img_width(data->last_loaded_img);
        if (height)
            *height = SKRY_get_img_height(data->last_loaded_img);
        if (pix_fmt)
            *pix_fmt = SKRY_get_img_pix_fmt(data->last_loaded_img);

        return SKRY_SUCCESS;
    }
    else
    {
        return SKRY_get_image_metadata(data->file_names[img_seq->curr_image_idx], width, height, pix_fmt);
    }
}

static
void image_list_deactivate(struct SKRY_img_sequence *img_seq)
{
    // Do nothing
    (void)img_seq; // suppress the "unused parameter" warning
}

#define FAIL_ON_NULL(ptr)                \
    if (!(ptr))                          \
    {                                    \
        SKRY_free_img_sequence(img_seq); \
        return 0;                        \
    }

struct SKRY_img_sequence *SKRY_init_image_list(
        size_t num_images,
        const char *file_names[],
        SKRY_ImagePool *img_pool)
{
    struct SKRY_img_sequence *img_seq = malloc(sizeof(*img_seq));
    if (!img_seq)
        return 0;

    *img_seq = (struct SKRY_img_sequence) { 0 };

    img_seq->type = SKRY_IMG_SEQ_IMAGE_FILES;
    img_seq->num_images =             num_images;
    img_seq->free =                   image_list_free;
    img_seq->get_curr_img =           image_list_get_curr_img;
    img_seq->get_curr_img_metadata =  image_list_get_curr_img_metadata;
    img_seq->get_img_by_index =       image_list_get_img;
    img_seq->deactivate_img_seq =     image_list_deactivate;

    base_init(img_seq, img_pool);

    struct image_list_data *data = malloc(sizeof(*data));
    FAIL_ON_NULL(data);

    img_seq->data = data;

    *data = (struct image_list_data) { 0 };
    data->last_loaded_img_idx = SKRY_EMPTY;
    data->file_names = malloc(num_images * sizeof(char *));
    FAIL_ON_NULL(data->file_names);
    for (size_t i = 0; i < num_images; i++)
    {
        if (file_names)
        {
            data->file_names[i] = malloc(strlen(file_names[i]) + 1);
            FAIL_ON_NULL(data->file_names[i]);
            strcpy(data->file_names[i], file_names[i]);

            data->next_file_name_idx_to_add++;
        }
        else
            data->file_names[i] = 0;
    }

    return img_seq;
}

enum SKRY_result SKRY_image_list_add_img(struct SKRY_img_sequence *img_seq,
                             const char *file_name)
{
    struct image_list_data *data = IMG_LIST_DATA(img_seq);
    assert(data->next_file_name_idx_to_add < img_seq->num_images);

    char **fn = &data->file_names[data->next_file_name_idx_to_add];
    *fn = malloc(strlen(file_name) + 1);
    if (!(*fn))
        return SKRY_OUT_OF_MEMORY;
    else
    {
        data->next_file_name_idx_to_add += 1;
        strcpy(*fn, file_name);
        return SKRY_SUCCESS;
    }
}
