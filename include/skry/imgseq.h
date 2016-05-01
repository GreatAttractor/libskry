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
    Image sequence header.
*/

#ifndef LIB_STACKISTRY_IMG_SEQ_HEADER
#define LIB_STACKISTRY_IMG_SEQ_HEADER

#include <stddef.h>
#include <stdint.h>

#include "defs.h"
#include "image.h"


typedef struct SKRY_img_sequence SKRY_ImgSequence;

SKRY_ImgSequence *SKRY_init_image_list(
        size_t num_images,
        /// If null, SKRY_image_list_add_img() must be later used to add 'num_images' file names
        const char *file_names[]);

SKRY_ImgSequence *SKRY_init_video_file(const char *file_name,
                                       /// If not null, receives operation result
                                       enum SKRY_result *result);

enum SKRY_result SKRY_image_list_add_img(SKRY_ImgSequence *img_seq,
                                         const char *file_name);

/// Returns absolute index (refers to the whole set, including non-active images)
size_t SKRY_get_curr_img_idx(const SKRY_ImgSequence *img_seq);

size_t SKRY_get_curr_img_idx_within_active_subset(const SKRY_ImgSequence *img_seq);

size_t SKRY_get_img_count(const SKRY_ImgSequence *img_seq);

/// Returns null
SKRY_ImgSequence *SKRY_free_img_sequence(SKRY_ImgSequence *img_seq);

/// Seeks to the first active image
void SKRY_seek_start(SKRY_ImgSequence *img_seq);

/// Seeks forward to the next active image; returns SKRY_SUCCESS or SKRY_NO_MORE_IMAGES
enum SKRY_result SKRY_seek_next(SKRY_ImgSequence *img_seq);

SKRY_Image *SKRY_get_curr_img(const SKRY_ImgSequence *img_seq,
                              enum SKRY_result *result ///< If not null, receives operation result
                              );

enum SKRY_result SKRY_get_curr_img_metadata(const SKRY_ImgSequence *img_seq,
                                        unsigned *width,  ///< If not null, receives current image's width
                                        unsigned *height, ///< If not null, receives current image's height
                                        enum SKRY_pixel_format *pix_fmt ///< If not null, receives current image's pixel format
                                        );

SKRY_Image *SKRY_get_img_by_index(const SKRY_ImgSequence *img_seq, size_t index,
                                  enum SKRY_result *result ///< If not null, receives operation result
                                  );

/// Should be called when 'img_seq' will not be read for some time
/** In case of image lists, the function does nothing. For video files, it closes them.
    Video files are opened automatically (and kept open) every time a frame is loaded. */
void SKRY_deactivate_img_seq(SKRY_ImgSequence *img_seq);

void SKRY_set_active_imgs(SKRY_ImgSequence *img_seq,
                          /// Element count = number of images in 'img_seq'
                          const uint8_t *active_imgs);

int SKRY_is_img_active(const SKRY_ImgSequence *img_seq, size_t img_idx);

/// Element count of result = number of images in 'img_seq'
const uint8_t *SKRY_get_img_active_flags(const SKRY_ImgSequence *img_seq);

size_t SKRY_get_active_img_count(const SKRY_ImgSequence *img_seq);

enum SKRY_img_sequence_type SKRY_get_img_seq_type(const SKRY_ImgSequence *img_seq);

SKRY_Image *SKRY_create_flatfield(
    /// All images must have the same size
    SKRY_ImgSequence *img_seq,
    /// If not null, receives operation result
    enum SKRY_result *result
);

#endif // LIB_STACKISTRY_IMG_SEQ_HEADER
