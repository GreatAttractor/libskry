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
    TIFF-related functions header.
*/

#ifndef LIBSKRY_TIFF_HEADER
#define LIBSKRY_TIFF_HEADER

#include <skry/image.h>
#include <skry/defs.h>


/// Returns null on error
SKRY_Image *load_TIFF(const char *file_name,
                      enum SKRY_result *result ///< If not null, receives operation result
                     );

enum SKRY_result save_TIFF(const SKRY_Image *img, const char *file_name);

/// Returns metadata without reading the pixel data
enum SKRY_result get_TIFF_metadata(const char *file_name,
                                   unsigned *width,  ///< If not null, receives image width
                                   unsigned *height, ///< If not null, receives image height
                                   enum SKRY_pixel_format *pix_fmt ///< If not null, receives pixel format
                                  );

#endif // LIBSKRY_TIFF_HEADER
