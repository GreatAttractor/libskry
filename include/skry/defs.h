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
    Enums, constants and types.
*/

#ifndef LIB_STACKISTRY_DEFS_HEADER
#define LIB_STACKISTRY_DEFS_HEADER

#include <limits.h>
#include <stdint.h>


#define SKRY_MIN(x, y) ((x) < (y) ? (x) : (y))
#define SKRY_MAX(x, y) ((x) > (y) ? (x) : (y))
#define SKRY_SQR(x) ((x)*(x))

/// Indicates a lack of relationship
#define SKRY_EMPTY SIZE_MAX

/// Log event masks
enum
{
    SKRY_LOG_QUIET            = 0U,
    SKRY_LOG_IMAGE            = 1U << 1,
    SKRY_LOG_REF_PT_ALIGNMENT = 1U << 2,
    SKRY_LOG_STACKING         = 1U << 3,
    SKRY_LOG_TRIANGULATION    = 1U << 4,
    SKRY_LOG_QUALITY          = 1U << 5,
    SKRY_LOG_AVI              = 1U << 6,
    SKRY_LOG_IMG_ALIGNMENT    = 1U << 7,
    SKRY_LOG_SER              = 1U << 8,
    SKRY_LOG_IMG_POOL         = 1U << 9,

    SKRY_LOG_ALL              = UINT_MAX
};

enum SKRY_pixel_format
{
    SKRY_PIX_INVALID,

    SKRY_PIX_PAL8,  ///< 8 bits per pixel, values from a 256-entry palette
    SKRY_PIX_MONO8,
    SKRY_PIX_RGB8,  ///< LSB = R, MSB = B
    SKRY_PIX_BGRA8, ///< LSB = B, MSB = A or unused

    SKRY_PIX_MONO16,
    SKRY_PIX_RGB16,
    SKRY_PIX_RGBA16,

    SKRY_PIX_MONO32F,
    SKRY_PIX_RGB32F,

    SKRY_PIX_MONO64F,
    SKRY_PIX_RGB64F,

    SKRY_PIX_CFA_MIN, ///< All color filter array formats have to be above

    SKRY_PIX_CFA_RGGB8,
    SKRY_PIX_CFA_GRBG8,
    SKRY_PIX_CFA_GBRG8,
    SKRY_PIX_CFA_BGGR8,

    SKRY_PIX_CFA_RGGB16,
    SKRY_PIX_CFA_GRBG16,
    SKRY_PIX_CFA_GBRG16,
    SKRY_PIX_CFA_BGGR16,

    SKRY_PIX_CFA_MAX, ///< All color filter array formats have to be below

    SKRY_NUM_PIX_FORMATS ///< This has to be the last element
};

enum SKRY_CFA_pattern
{
    SKRY_CFA_RGGB = 0,
    SKRY_CFA_BGGR,
    SKRY_CFA_GRBG,
    SKRY_CFA_GBRG,

    SKRY_CFA_MAX,
    SKRY_CFA_NONE = SKRY_CFA_MAX,
    SKRY_NUM_CFA_PATTERNS ///< This has to be the last element
};

extern const char *SKRY_CFA_pattern_str[SKRY_NUM_CFA_PATTERNS];

/// Indexed by 'SKRY_pixel_format'
extern const enum SKRY_CFA_pattern SKRY_PIX_CFA_PATTERN[SKRY_NUM_PIX_FORMATS];

enum SKRY_demosaic_method
{
    /** Fast, but low-quality; used internally during
        image alignment, quality estimation and
        ref. point alignment */
    SKRY_DEMOSAIC_SIMPLE,

    /** High-quality and slower; used internally
        during stacking phase */
    SKRY_DEMOSAIC_HQLINEAR,

    /** Mainly for calling pixel format conversion functions
        on non-raw color images. */
    SKRY_DEMOSAIC_DONT_CARE = SKRY_DEMOSAIC_SIMPLE
};

enum SKRY_result
{
    SKRY_SUCCESS,
    SKRY_INVALID_PARAMETERS,
    SKRY_LAST_STEP,
    SKRY_NO_MORE_IMAGES,
    SKRY_NO_PALETTE,
    SKRY_CANNOT_OPEN_FILE,
    SKRY_BMP_MALFORMED_FILE,
    SKRY_UNSUPPORTED_BMP_FILE,
    SKRY_UNSUPPORTED_FILE_FORMAT,
    SKRY_OUT_OF_MEMORY,
    SKRY_CANNOT_CREATE_FILE,
    SKRY_FILE_IO_ERROR,
    SKRY_TIFF_INCOMPLETE_HEADER,
    SKRY_TIFF_UNKNOWN_VERSION,
    SKRY_TIFF_NUM_DIR_ENTR_TAG_INCOMPLETE,
    SKRY_TIFF_INCOMPLETE_FIELD,
    SKRY_TIFF_DIFF_CHANNEL_BIT_DEPTHS,
    SKRY_TIFF_COMPRESSED,
    SKRY_TIFF_UNSUPPORTED_PLANAR_CONFIG,
    SKRY_UNSUPPORTED_PIXEL_FORMAT,
    SKRY_TIFF_INCOMPLETE_PIXEL_DATA,
    SKRY_AVI_MALFORMED_FILE,
    SKRY_AVI_UNSUPPORTED_FORMAT,
    SKRY_INVALID_IMG_DIMENSIONS,
    SKRY_SER_MALFORMED_FILE,
    SKRY_SER_UNSUPPORTED_FORMAT,

    SKRY_RESULT_LAST
};

/** Some of the formats listed here may be not supported;
    use SKRY_get_supported_output_formats() to find out. */
enum SKRY_output_format
{
    SKRY_INVALID_OUTP_FMT = 0,

    SKRY_BMP_8,
    SKRY_PNG_8,
    SKRY_TIFF_16,

    SKRY_OUTP_FMT_LAST
};

enum SKRY_img_sequence_type
{
    SKRY_IMG_SEQ_IMAGE_FILES,
    SKRY_IMG_SEQ_AVI,
    SKRY_IMG_SEQ_SER
};

/// Selection criterion used for reference point alignment and stacking
/** "fragment" = triangular patch */
enum SKRY_quality_criterion
{
    /// Percentage of best-quality fragments
    SKRY_PERCENTAGE_BEST,

    /// Minimum relative quality (%)
    /** Only fragments with quality above specified threshold
        (% relative to [min,max] of the corresponding quality
        estimation area) will be used. */
    SKRY_MIN_REL_QUALITY,

    /// Number of best-quality fragments
    SKRY_NUMBER_BEST
};

typedef float SKRY_quality_t;

struct SKRY_point
{
    int x, y;
};

struct SKRY_point_flt
{
    float x, y;
};

struct SKRY_rect
{
    int x, y;
    unsigned width, height;
};

#define SKRY_RECT_CONTAINS(r, p) \
    ((p).x >= (r).x && (p).x < (r).x + (int)(r).width && (p).y >= (r).y && (p).y < (r).y + (int)(r).height)

#define SKRY_ADD_POINT_TO(dest, src) \
    ((dest) = (struct SKRY_point) { .x = (dest).x + (src).x, .y = (dest).y + (src).y })

#define SKRY_ADD_POINTS(p1, p2) \
    ((struct SKRY_point) { .x = (p1).x + (p2).x, .y = (p1).y + (p2).y })

#define ROUND_TO_NEAREST(x) ((int)((x) >= 0 ? (x) + 0.5 : (x) - 0.5))

typedef void SKRY_log_callback_fn(unsigned log_event_type, const char *msg);

typedef double SKRY_clock_sec_fn(void);

#endif // LIB_STACKISTRY_DEFS_HEADER
