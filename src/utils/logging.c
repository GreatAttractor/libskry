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
    Logging implementation.
*/

#include <assert.h>
#include <skry/skry.h>
#include "logging.h"


const char* pix_fmt_str[SKRY_NUM_PIX_FORMATS] =
{
    [SKRY_PIX_INVALID] = "PIX_INVALID",

    [SKRY_PIX_PAL8]    = "PIX_PAL8",
    [SKRY_PIX_MONO8]   = "PIX_MONO8",
    [SKRY_PIX_RGB8]    = "PIX_RGB8",
    [SKRY_PIX_BGRA8]   = "PIX_BGRA8",

    [SKRY_PIX_MONO16]  = "PIX_MONO16",
    [SKRY_PIX_RGB16]   = "PIX_RGB16",
    [SKRY_PIX_RGBA16]  = "PIX_RGBA16",

    [SKRY_PIX_MONO32F] = "PIX_MONO32F",
    [SKRY_PIX_RGB32F]  = "PIX_RGB32F",

    [SKRY_PIX_MONO64F] = "PIX_MONO64F",
    [SKRY_PIX_RGB64F]  = "PIX_RGB64F"
};

const char *error_messages[SKRY_RESULT_LAST] =
{
    [SKRY_SUCCESS]                      = "Success",
    [SKRY_INVALID_PARAMETERS]           = "Invalid parameters",
    [SKRY_LAST_STEP]                    = "Last step",
    [SKRY_NO_MORE_IMAGES]               = "No more images",
    [SKRY_NO_PALETTE]                   = "No palette",
    [SKRY_CANNOT_OPEN_FILE]             = "Cannot open file",
    [SKRY_BMP_MALFORMED_FILE]           = "Malformed BMP file",
    [SKRY_UNSUPPORTED_BMP_FILE]         = "Unsupported BMP file",
    [SKRY_UNSUPPORTED_FILE_FORMAT]      = "Unsupported file format",
    [SKRY_OUT_OF_MEMORY]                = "Out of memory",
    [SKRY_CANNOT_CREATE_FILE]           = "Cannot create file",
    [SKRY_TIFF_INCOMPLETE_HEADER]       = "Incomplete TIFF header",
    [SKRY_TIFF_UNKNOWN_VERSION]         = "Unknown TIFF version",
    [SKRY_TIFF_NUM_DIR_ENTR_TAG_INCOMPLETE] = "Incomplete TIFF tag: number of directory entries",
    [SKRY_TIFF_INCOMPLETE_FIELD]        = "Incomplete TIFF field",
    [SKRY_TIFF_DIFF_CHANNEL_BIT_DEPTHS] = "Channels have different bit depths",
    [SKRY_TIFF_COMPRESSED]              = "TIFF compression is not supported",
    [SKRY_TIFF_UNSUPPORTED_PLANAR_CONFIG] = "Unsupported TIFF planar configuration",
    [SKRY_UNSUPPORTED_PIXEL_FORMAT]     = "Unsupported pixel format",
    [SKRY_TIFF_INCOMPLETE_PIXEL_DATA]   = "Incomplete TIFF pixel data",
    [SKRY_AVI_MALFORMED_FILE]           = "Malformed AVI file",
    [SKRY_AVI_UNSUPPORTED_FORMAT]       = "Unsupported AVI DIB format",
    [SKRY_INVALID_IMG_DIMENSIONS]       = "Invalid image dimensions",
    [SKRY_SER_MALFORMED_FILE]           = "Malformed SER file",
    [SKRY_SER_UNSUPPORTED_FORMAT]       = "Unsupported SER format"
};

SKRY_log_callback_fn *g_log_msg_callback;

unsigned g_log_event_type_mask = 0U;

char g_log_msg_buf[LOG_MSG_MAX_LEN];

void SKRY_set_logging(unsigned log_event_type_mask,
                      SKRY_log_callback_fn callback_func)
{
    g_log_msg_callback = callback_func;
    g_log_event_type_mask = log_event_type_mask;
}

void log_msg(unsigned log_event_type, const char *msg)
{
    if ((g_log_event_type_mask & log_event_type) && g_log_msg_callback)
        g_log_msg_callback(log_event_type, msg);
}

const char *SKRY_get_error_message(enum SKRY_result error)
{
    assert(error >= SKRY_SUCCESS && error <= SKRY_RESULT_LAST);
    return error_messages[error];
}
