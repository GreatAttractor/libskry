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
    BMP-related functions header.
*/

#ifndef LIBSKRY_BMP_HEADER
#define LIBSKRY_BMP_HEADER

#include <skry/image.h>
#include <skry/defs.h>


#define BI_RGB       0
#define BI_BITFIELDS 3

#pragma pack(push)

/// Size of BMP palette in bytes
#define BMP_PALETTE_SIZE 256*4

/// BMP-style palette (B, G, R, pad)
#pragma pack(1)
struct BMP_palette
{
    uint8_t pal[BMP_PALETTE_SIZE];
};

#pragma pack(1)
struct bitmap_info_header
{
   uint32_t size;
   int32_t  width;
   int32_t  height;
   uint16_t planes;
   uint16_t bit_count;
   uint32_t compression;
   uint32_t size_image;
   int32_t  x_pels_per_meter;
   int32_t  y_pels_per_meter;
   uint32_t clr_used;
   uint32_t clr_important;
};
#pragma pack(pop)

/// Returns null on error
SKRY_Image *load_BMP(const char *file_name,
                     enum SKRY_result *result ///< If not null, receives operation result
                     );

enum SKRY_result save_BMP(const SKRY_Image *img, const char *file_name);

/// Returns metadata without reading the pixel data
enum SKRY_result get_BMP_metadata(const char *file_name,
                                  unsigned *width,  ///< If not null, receives image width
                                  unsigned *height, ///< If not null, receives image height
                                  enum SKRY_pixel_format *pix_fmt ///< If not null, receives pixel format
                                 );

#endif // LIBSKRY_BMP_HEADER
