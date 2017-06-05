/*
libskry - astronomical image stacking
Copyright (C) 2016, 2017 Filip Szczerek <ga.software@yahoo.com>

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
    Library initialization/deinitialization functions.
*/

#if USE_LIBAV
#include <libavformat/avformat.h>
#endif
#include <skry/skry.h>

/// Must be called before using libskry
enum SKRY_result SKRY_initialize(void)
{
#if USE_LIBAV
    av_register_all();
#endif
    return SKRY_SUCCESS;
}

/// Must be called after finished using libskry
void SKRY_deinitialize(void)
{
#if USE_LIBAV
    //
#endif
}
