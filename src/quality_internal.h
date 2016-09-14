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
    Quality estimation non-public header.
*/


/// Returns a square image to be used as reference block; returns null if out of memory
SKRY_Image *SKRY_create_reference_block(
    const SKRY_QualityEstimation *qual_est,
    /// Center of the reference block (within images' intersection)
    struct SKRY_point pos,
    /// Desired width & height; the result may be smaller than this (but always a square)
    unsigned blk_size);
