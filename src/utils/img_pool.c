#include <assert.h>
#include <stdlib.h>

#include "img_pool.h"
#include "logging.h"


struct img_seq_entry
{
    SKRY_ImgSequence *img_seq;
    size_t num_images;
    SKRY_Image **images;
};

/**
    Contains a list of image sequences and an array of stored images for each of them.

    Each element in 'img_seq_nodes' corresponds with an image sequence registered
    in the pool. For each sequence there is an array of registered image pointers
    (img_seq_entry::images). The 'img_seq_nodes' list's linkage changes during pool's
    lifetime to reflect the order of most recent access. (Note that pointers to
    the list's elements remain unchanged).
*/
struct SKRY_image_pool
{
    size_t capacity; ///< Max allowed total size of registered images
    size_t num_bytes; ///< Number of bytes occupied by all registered images

    /** List of image sequences connected to the pool ('data' fields point
        to 'struct img_seq_entry'. The first node is the most recently used (MRU)
        one, the last is the least recently used (LRU). */
    struct list_node *img_seq_nodes;

    /// The least recently used (connected/read/modified) node in 'img_seq_nodes'
    /** This is also the last node in the list. */
    struct list_node *LRU;
};

struct SKRY_image_pool *SKRY_create_image_pool(size_t capacity)
{
    struct SKRY_image_pool *img_pool = malloc(sizeof(*img_pool));
    if (!img_pool)
        return 0;

    *img_pool = (struct SKRY_image_pool) { 0 };
    img_pool->capacity = capacity;

    LOG_MSG(SKRY_LOG_IMG_POOL, "Created image pool %p (%.1f MiB capacity).",
            (void *)img_pool, (double)capacity/(1U << 20));

    return img_pool;
}

struct list_node *connect_img_sequence(struct SKRY_image_pool *img_pool, SKRY_ImgSequence *img_seq)
{
    struct img_seq_entry *data = malloc(sizeof(*data));
    if (!data)
        return 0;

    data->img_seq = img_seq;
    data->num_images = SKRY_get_img_count(img_seq);
    data->images = malloc(data->num_images * sizeof(*data->images));
    if (!data->images)
    {
        free(data);
        return 0;
    }
    for (size_t i = 0; i < data->num_images; i++)
        data->images[i] = 0;

    list_add(&img_pool->img_seq_nodes, data);

    LOG_MSG(SKRY_LOG_IMG_POOL, "Connected img. seq. %p to img. pool %p (node: %p).",
            (void *)img_seq, (void *)img_pool, (void *)img_pool->img_seq_nodes);

    if (!img_pool->LRU)
        img_pool->LRU = img_pool->img_seq_nodes; // currently there is only one element

    return img_pool->img_seq_nodes;
}

void disconnect_img_sequence(struct SKRY_image_pool *img_pool, struct list_node *img_seq_node)
{
    struct img_seq_entry *entry = (struct img_seq_entry *)img_seq_node->data;
    for (size_t i = 0; i < entry->num_images; i++)
        SKRY_free_image(entry->images[i]);

    free(entry->images);

    LOG_MSG(SKRY_LOG_IMG_POOL, "Disconnected img. seq. %p from img. pool %p (node: %p).",
        (void *)entry->img_seq, (void *)img_pool, (void *)img_seq_node);

    free(entry);
    list_remove(&img_pool->img_seq_nodes, img_seq_node);
    free(img_seq_node);
}

static
void mark_as_MRU(struct SKRY_image_pool *img_pool, struct list_node *img_seq_node)
{
    struct list_node *MRU = img_pool->img_seq_nodes;

    if (MRU == img_seq_node)
        return;

    if (img_seq_node->prev)
        img_seq_node->prev->next = img_seq_node->next;

    if (img_seq_node->next)
        img_seq_node->next->prev = img_seq_node->prev;

    if (img_pool->LRU == img_seq_node)
        img_pool->LRU = img_seq_node->prev;

    img_seq_node->prev = 0;
    img_seq_node->next = MRU;
    MRU->prev = img_seq_node;
    img_pool->img_seq_nodes = img_seq_node;

    LOG_MSG(SKRY_LOG_IMG_POOL, "Node %p (img. seq. %p) set as MRU in img. pool %p.",
            (void *)img_seq_node, (void *)((struct img_seq_entry *)img_seq_node->data)->img_seq,
            (void *)img_pool);
}

void put_image_in_pool(SKRY_ImagePool *img_pool, struct list_node *img_seq_node,
                       size_t img_index, SKRY_Image *img)
{
    assert(img_seq_node);
    struct img_seq_entry *data = img_seq_node->data;
    assert(img_index < data->num_images);

    size_t img_bytes = SKRY_get_img_byte_count(img);

    struct list_node *less_recently_used_node = img_pool->LRU;

    // If the image cannot fit in the pool, try freeing images of
    // less recently used image sequences until there is room.
    while (img_pool->num_bytes + img_bytes > img_pool->capacity
           && less_recently_used_node
           && less_recently_used_node != img_seq_node)
    {
        size_t curr_lru_img_index_to_free = 0; // img index in 'less_recently_used_node'
        struct img_seq_entry *data = less_recently_used_node->data;
        while (img_pool->num_bytes + img_bytes > img_pool->capacity
               && curr_lru_img_index_to_free < data->num_images)
        {
            SKRY_Image *img_to_free = data->images[curr_lru_img_index_to_free];
            if (img_to_free)
            {
                img_pool->num_bytes -= SKRY_get_img_byte_count(img_to_free);
                data->images[curr_lru_img_index_to_free] = SKRY_free_image(img_to_free);

                LOG_MSG(SKRY_LOG_IMG_POOL, "Freed image %p (index %zu in img. seq. %p) from img. pool %p.",
                        (void *)img_to_free, curr_lru_img_index_to_free,
                        (void *)data->img_seq, (void *)img_pool);
            }
            curr_lru_img_index_to_free++;
        }
        less_recently_used_node = less_recently_used_node->prev;
    }

    if (img_pool->num_bytes + img_bytes <= img_pool->capacity)
    {
        if (data->images[img_index])
        {
            LOG_MSG(SKRY_LOG_IMG_POOL, "Replacing and freeing image %p (index %zu in img. seq. %p) in img. pool %p.",
                    (void *)data->images[img_index], img_index, (void *)data->img_seq, (void *)img_pool);
            img_pool->num_bytes -= SKRY_get_img_byte_count(data->images[img_index]);
            SKRY_free_image(data->images[img_index]);
        }
        data->images[img_index] = img;
        img_pool->num_bytes += img_bytes;
        mark_as_MRU(img_pool, img_seq_node);

        LOG_MSG(SKRY_LOG_IMG_POOL, "Image %p (index %zu in img. seq. %p) put in img. pool %p (pool size is now %zu).",
        (void *)img, img_index, (void *)data->img_seq, (void *)img_pool, img_pool->num_bytes);
    }
    else
    {
        LOG_MSG(SKRY_LOG_IMG_POOL, "Image %p (%zu bytes, index %zu in img. seq. %p) could not be put in img. pool %p (size: %zu, capacity: %zu).",
        (void *)img, img_bytes, img_index, (void *)data->img_seq, (void *)img_pool, img_pool->num_bytes, img_pool->capacity);

        // report something to caller?
    }
}

SKRY_Image *get_image_from_pool(struct SKRY_image_pool *img_pool, struct list_node *img_seq_node, size_t img_idx)
{
    struct img_seq_entry *data = img_seq_node->data;
    assert(img_idx < data->num_images);

    mark_as_MRU(img_pool, img_seq_node);

    if (data->images[img_idx])
    {
        mark_as_MRU(img_pool, img_seq_node);

        LOG_MSG(SKRY_LOG_IMG_POOL, "Got image %p (index %zu in img. seq. %p) from img. pool %p.",
                (void *)data->images[img_idx], img_idx, (void *)data->img_seq, (void *)img_pool);

        return data->images[img_idx];
    }
    else
        return 0;
}

struct SKRY_image_pool *SKRY_free_image_pool(struct SKRY_image_pool *img_pool)
{
    if (img_pool)
    {
        struct list_node *node = img_pool->img_seq_nodes;
        while (node)
        {
            struct list_node *next = node->next;
            SKRY_disconnect_from_img_pool(((struct img_seq_entry *)node->data)->img_seq);
            node = next;
        }
        free(img_pool);
    }
    return 0;
}
