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
    Block-matching header.
*/

#ifndef LIBSKRY_IMG_MATCHING_HEADER
#define LIBSKRY_IMG_MATCHING_HEADER

#include <skry/defs.h>
#include <skry/image.h>


void foo(void);

//TODO: add comments
void find_matching_position(
    struct SKRY_point ref_pos,
    const SKRY_Image *ref_block,
    const SKRY_Image *image,
    unsigned search_radius,
    unsigned initial_search_step,
    struct SKRY_point *new_pos);

#endif // LIBSKRY_IMG_MATCHING_HEADER
