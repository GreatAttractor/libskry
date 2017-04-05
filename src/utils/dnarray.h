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
    Dynamic array macros.
*/

#ifndef LIB_STACKISTRY_DYNAMIC_ARRAY_HEADER
#define LIB_STACKISTRY_DYNAMIC_ARRAY_HEADER

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>


// After a call to DA_GROW(), DA_SET_SIZE(), DA_SET_CAPACITY() or DA_APPEND(), pointers to dynamic array's elements may be invalidated.

#define DA_GROWTH_FACTOR 2

#define DA_DECLARE(Type) struct { Type *data; Type *data_end; Type *storage_end; }

#define DA_ALLOC(darray, capacity) \
do {                               \
    (darray).data = 0;             \
    (darray).data_end = 0;         \
    (darray).storage_end = 0;      \
                                   \
    if (capacity > 0)              \
    {                              \
        (darray).data = malloc(capacity * sizeof(*(darray).data)); \
        assert((darray).data);                                     \
        (darray).data_end = (darray).data;                         \
        (darray).storage_end = (darray).data + capacity;           \
    }                                                              \
} while (0)

#define DA_CAPACITY(darray) ((size_t)((darray).storage_end - (darray).data))
#define DA_SIZE(darray)     ((size_t)((darray).data_end - (darray).data))

#define DA_GROW(darray)                            \
do {                                               \
    size_t storage_size = DA_CAPACITY(darray);     \
    size_t data_size = DA_SIZE(darray);            \
    storage_size *= DA_GROWTH_FACTOR;              \
    if (0 == storage_size) storage_size = 1;       \
    (darray).data = realloc((darray).data, storage_size * sizeof(*(darray).data)); \
    assert((darray).data);                         \
    (darray).data_end = (darray).data + data_size; \
    (darray).storage_end = (darray).data + storage_size; \
} while (0)

#define DA_APPEND(darray, value)               \
do {                                           \
    if ((darray).data_end == (darray).storage_end) \
        DA_GROW(darray);                       \
                                               \
    *(darray).data_end++ = value;              \
} while (0)

#define DA_FREE(darray) { free((darray).data); (darray).data = (darray).data_end = (darray).storage_end = 0; }

#define DA_SET_CAPACITY(darray, new_capacity_expr)         \
do {                                                       \
    /* Can't use 'new_capacity_exp' elsewhere in the macro  */          \
    /* directly, because it could be passed as e.g. DA_SIZE(darray); */ \
    /* substituting this below would be disastrous. */     \
    size_t new_capacity = (size_t)(new_capacity_expr);     \
    (darray).data = realloc((darray).data, new_capacity * sizeof(*(darray).data)); \
    assert((darray).data);                                 \
    (darray).storage_end = (darray).data + new_capacity;   \
    if (DA_CAPACITY(darray) < DA_SIZE(darray))             \
        (darray).data_end = (darray).storage_end;          \
} while (0)

/** Changes the size (element count). If 'new_size' is greater than
    the current count, new elements will be uninitialized.
    If 'new_size' exceeds current capacity, the storage will
    be re-allocated. */
#define DA_SET_SIZE(darray, new_size_expr)                        \
do {                                                              \
    size_t new_size = (size_t)new_size_expr;                      \
    if ((new_size) > DA_CAPACITY(darray))                         \
        DA_SET_CAPACITY((darray), (new_size) * DA_GROWTH_FACTOR); \
                                                                  \
    (darray).data_end = (darray).data + new_size;                 \
} while (0)

#define DA_LAST(darray) (*((darray).data_end - 1))

#endif // LIB_STACKISTRY_DYNAMIC_ARRAY_HEADER
