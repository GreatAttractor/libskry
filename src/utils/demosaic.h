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
    Color filter array images demosaicing header.
*/

#ifndef LIB_STACKISTRY_DEMOSAIC_HEADER
#define LIB_STACKISTRY_DEMOSAIC_HEADER


/* NOTE: 2-pixel borders of the image at the top and on the left and 3-pixel borders at the bottom
         and on the right are not processed, just copied from their neighbors */

/// Performs demosaicing of 8-bit 'input' and saves as 8-bit RGB in 'output'
/** 'width' and 'height' must be >= 6 */
void demosaic_8_as_RGB(uint8_t *input, unsigned width, unsigned height,
                       ptrdiff_t input_stride, ///< line stride in bytes
                       uint8_t *output,
                       ptrdiff_t output_stride, ///< line stride in bytes
                       /// Color filter array pattern in 'input'
                       enum SKRY_CFA_pattern CFA_pattern,
                       enum SKRY_demosaic_method method
                       );

/// Performs demosaicing of 8-bit 'input' and saves as 8-bit mono in 'output'
/** 'width' and 'height' must be >= 6 */
void demosaic_8_as_mono8(uint8_t *input, unsigned width, unsigned height,
                         ptrdiff_t input_stride, ///< line stride in bytes
                         uint8_t *output,
                         ptrdiff_t output_stride, ///< line stride in bytes
                         /// Color filter array pattern in 'input'
                         enum SKRY_CFA_pattern CFA_pattern,
                         enum SKRY_demosaic_method method);

/// Performs demosaicing of 16-bit 'input' and saves as 16-bit RGB in 'output'
/** 'width' and 'height' must be >= 6 */
void demosaic_16_as_RGB(uint16_t *input, unsigned width, unsigned height,
                        ptrdiff_t input_stride, ///< line stride in bytes
                        uint16_t *output,
                        ptrdiff_t output_stride, ///< line stride in bytes
                        /// Color filter array pattern in 'input'
                        enum SKRY_CFA_pattern CFA_pattern,
                        enum SKRY_demosaic_method method);

/// Performs demosaicing of 16-bit 'input' and saves as 8-bit mono in 'output'
/** 'width' and 'height' must be >= 6 */
void demosaic_16_as_mono8(uint16_t *input, unsigned width, unsigned height,
                          ptrdiff_t input_stride, ///< line stride in bytes
                          uint8_t *output,
                          ptrdiff_t output_stride, ///< line stride in bytes
                          /// Color filter array pattern in 'input'
                          enum SKRY_CFA_pattern CFA_pattern,
                          enum SKRY_demosaic_method method);

#endif // LIB_STACKISTRY_DEMOSAIC_HEADER
