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
    Logging header.
*/

#ifndef LIBSKRY_LOGGING_HEADER
#define LIBSKRY_LOGGING_HEADER

#include <stdio.h>
#include <skry/defs.h>


/// String representations of values from 'enum SKRY_pixel_format'
extern const char* pix_fmt_str[SKRY_NUM_PIX_FORMATS];

extern SKRY_log_callback_fn *g_log_msg_callback;
#define LOG_MSG_MAX_LEN 1024 /// Includes the NUL terminator
extern char g_log_msg_buf[LOG_MSG_MAX_LEN];
extern unsigned g_log_event_type_mask;

/// Message formatting wrapper
#define LOG_MSG(log_event_type, ...)                           \
do {                                                           \
    if (g_log_msg_callback                                     \
        && (g_log_event_type_mask & log_event_type))           \
    {                                                          \
        snprintf(g_log_msg_buf, LOG_MSG_MAX_LEN, __VA_ARGS__); \
        g_log_msg_callback(log_event_type, g_log_msg_buf);     \
    }                                                          \
} while (0)

#endif // LIBSKRY_LOGGING_HEADER
