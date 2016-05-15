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
typedef struct SKRY_image_pool SKRY_ImagePool;

SKRY_ImgSequence *SKRY_init_image_list(
        size_t num_images,
        /// If null, SKRY_image_list_add_img() must be later used to add 'num_images' file names
        const char *file_names[],
        /** If not null, will be used to keep converted images in memory for use
            by subsequent processing phases. */
        SKRY_ImagePool *img_pool);

SKRY_ImgSequence *SKRY_init_video_file(
    const char *file_name,
    /** If not null, will be used to keep converted images in memory for use
        by subsequent processing phases. */
    SKRY_ImagePool *img_pool,
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

/// Returns the current image in specified format
/** If 'img_seq' is connected to an image pool, the image is taken from the pool
    if it exists there (and has 'pix_fmt'). If the image is not yet in the pool,
    it will be converted to 'pix_fmt' and stored there, if the pool's capacity
    is not yet exhausted. Otherwise, the image is read as usual and converted
    without being added to the pool.
    In any case, once the caller is done with the image, it *has to* call
    'SKRY_release_img' and must not attempt to free the returned image. */
SKRY_Image *SKRY_get_curr_img_from_pool(
              const SKRY_ImgSequence *img_seq,
              enum SKRY_pixel_format pix_fmt,
              /// Used if source image contains raw color data
              enum SKRY_demosaic_method demosaic_method,
              enum SKRY_result *result ///< If not null, receives operation result
              );

/** Has to be called for the image returned by 'SKRY_get_curr_img_from_pool',
    when the image is no longer needed by the caller. */
void SKRY_release_img_to_pool(const SKRY_ImgSequence *img_seq, size_t img_idx, SKRY_Image *image);

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

/// Disconnects 'img_seq' from the image pool that was specified during 'img_seq's creation (if any)
void SKRY_disconnect_from_img_pool(SKRY_ImgSequence *img_seq);

/// Returns null if out of memory
SKRY_ImagePool *SKRY_create_image_pool(
    /// Value in bytes
    /** Concerns only the size of stored images. The pool's internal data
        structures occupy additional memory (e.g. for 1000 registered image
        sequences with 1000 images each, on a system with 64-bit pointers
        there will be additional 20+ megabytes used. */
    size_t capacity
);

/// Returns null; also disconnects all image sequences that were using 'img_pool'
SKRY_ImagePool *SKRY_free_image_pool(SKRY_ImagePool *img_pool);

/// Treat mono images in 'img_seq' as containing raw color data
/** Applies only to 8- and 16-bit mono images. Only pixel format is updated,
    the pixel data is unchanged. To demosaic, use one of the pixel format
    conversion functions.
    For SER videos marked as raw color, there is no need to call this function.
    It can be used to override the CFA pattern indicated in the SER header. */
void SKRY_reinterpret_img_seq_as_CFA(
         SKRY_ImgSequence *img_seq,
         /// Specify SKRY_CFA_NONE to disable pixel format overriding
         enum SKRY_CFA_pattern CFA_pattern);


#endif // LIB_STACKISTRY_IMG_SEQ_HEADER
