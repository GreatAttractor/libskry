#include <stdlib.h>
#include "list.h"


void list_add(struct list_node **list, void *data)
{
    struct list_node *new_node = malloc(sizeof(*new_node));
    new_node->prev = 0;
    new_node->next = *list;
    new_node->data = data;
    if (*list)
        (*list)->prev = new_node;

    *list = new_node;
}

void list_remove(struct list_node **list, struct list_node *node)
{
    if (!node)
        return;

    if (*list == node)
        *list = node->next;

    struct list_node *prev = node->prev;
    struct list_node *next = node->next;
    if (node->prev)
        node->prev->next = next;

    if (node->next)
        node->next->prev = prev;
}

/** Frees all nodes (calling 'fn_free_node_data' on each node's 'data' field)
    and sets 'list' to null.
    'fn_free_node_data' must not free the 'list_node' structs. */
void list_free(struct list_node **list, void (*fn_free_node_data)(void *))
{
    struct list_node *curr = *list;
    while (curr)
    {
        fn_free_node_data(curr->data);
        struct list_node *next = curr->next;
        free(curr);
        curr = next;
    }
    *list = 0;
}
