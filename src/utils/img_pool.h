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
    Image pool header.
*/

#ifndef LIB_STACKISTRY_IMAGE_POOL_HEADER
#define LIB_STACKISTRY_IMAGE_POOL_HEADER

#include <stddef.h>

#include <skry/image.h>
#include <skry/imgseq.h>

#include "list.h"


/** Returns a pointer to be later passed to all functions that expect 'img_seq_node'.
    Returns null if out of memory. */
struct list_node *connect_img_sequence(SKRY_ImagePool *img_pool, SKRY_ImgSequence *img_seq);

void disconnect_img_sequence(SKRY_ImagePool *img_pool,
                             /// Pointer returned by connect_img_sequence()
                             struct list_node *img_seq_node);

/** If an image has been already added for this 'img_seq' and 'img_index', it will be freed
    and replaced by 'img'. Otherwise, if adding 'img' would exceed the pool's memory size limit,
    images of the least recently used img. sequence in 'pool' will be removed and freed,
    until there is sufficient room. If all images other than those of 'img_seq_node'
    have been removed and there is still no room, the image will not be added to 'img_pool'. */
void put_image_in_pool(SKRY_ImagePool *img_pool,
                       /// Pointer returned by connect_img_sequence()
                       struct list_node *img_seq_node,
                       size_t img_index, SKRY_Image *img);

/// May return null; the caller must not attempt to free the returned image
SKRY_Image *get_image_from_pool(SKRY_ImagePool *img_pool,
                                /// Pointer returned by connect_img_sequence()
                                struct list_node *img_seq_node,
                                size_t img_idx);


#endif // LIB_STACKISTRY_IMAGE_POOL_HEADER
