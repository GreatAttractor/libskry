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
    Video-loading functions header.
*/

#ifndef LIBSKRY_VIDEO_HEADER
#define LIBSKRY_VIDEO_HEADER

struct SKRY_img_sequence *init_AVI(const char *file_name,
                                   /// If not null, receives operation result
                                   enum SKRY_result *result);

struct SKRY_img_sequence *init_SER(const char *file_name,
                                   /// If not null, receives operation result
                                   enum SKRY_result *result);

#endif // LIBSKRY_VIDEO_HEADER
