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
    Doubly linked list header.
*/

#ifndef LIB_STACKISTRY_DL_LIST_HEADER
#define LIB_STACKISTRY_DL_LIST_HEADER


struct list_node
{
    struct list_node *prev, *next;
    void *data;
};

void list_add(struct list_node **list, void *data);
void list_remove(struct list_node **list, struct list_node *node);

/** Frees all nodes (calling 'fn_free_node_data' on each node's 'data' field)
    and sets 'list' to null. */
void list_free(struct list_node **list, void (*fn_free_node_data)(void *));

#endif // LIB_STACKISTRY_DL_LIST_HEADER
