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
    Image sequence internal header.
*/

#ifndef LIB_STACKISTRY_IMG_SEQ_INTERNAL_HEADER
#define LIB_STACKISTRY_IMG_SEQ_INTERNAL_HEADER
#include <skry/imgseq.h>


typedef                               void fn_free(struct SKRY_img_sequence *);
typedef         struct SKRY_image *fn_get_curr_img(const struct SKRY_img_sequence *, enum SKRY_result *);
typedef  enum SKRY_result fn_get_curr_img_metadata(const struct SKRY_img_sequence *, unsigned *, unsigned *, enum SKRY_pixel_format *);
typedef     struct SKRY_image *fn_get_img_by_index(const struct SKRY_img_sequence *, size_t, enum SKRY_result *);
typedef                 void fn_deactivate_img_seq(struct SKRY_img_sequence *);

struct SKRY_img_sequence
{
    enum SKRY_img_sequence_type type;
    size_t num_images;
    size_t curr_image_idx;
    size_t curr_img_idx_within_active_subset;
    uint8_t *is_img_active; ///< Contains 'num_images' elements
    size_t last_active_idx; ///< Index of last non-zero element in 'is_img_active'
    size_t num_active_images;
    void *data;

    fn_free                  *free;
    fn_get_curr_img          *get_curr_img;
    fn_get_curr_img_metadata *get_curr_img_metadata;
    fn_get_img_by_index      *get_img_by_index;
    fn_deactivate_img_seq    *deactivate_img_seq;
};

/// Must be called after img_seq->num_images has been set
void base_init(struct SKRY_img_sequence *img_seq);

#endif // LIB_STACKISTRY_IMG_SEQ_INTERNAL_HEADER
