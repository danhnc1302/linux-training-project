#include <stdlib.h>

#include "sbuffer.h"

/* Remove node from the list if both readers are done.
 * Caller holds buffer->mutex. */
static void remove_if_done(sbuffer_t *b, sbuffer_node_t *prev,
                           sbuffer_node_t *node)
{
    if (!node->read_by_datamgr || !node->read_by_storagemgr)
    {
        return;
    }

    if (prev == NULL)
    {
        b->head = node->next;
    }
    else
    {
        prev->next = node->next;
    }

    if (b->tail == node)
    {
        b->tail = prev;
    }

    b->size--;
    free(node);
}

/* Find first node not yet read by the given reader. Caller holds mutex. */
static sbuffer_node_t *find_unread(sbuffer_t *b, bool storage,
                                   sbuffer_node_t **prev_out)
{
    sbuffer_node_t *prev = NULL;
    sbuffer_node_t *curr = b->head;

    while (curr != NULL)
    {
        bool done = storage ? curr->read_by_storagemgr : curr->read_by_datamgr;

        if (!done)
        {
            *prev_out = prev;
            return curr;
        }
        prev = curr;
        curr = curr->next;
    }

    *prev_out = NULL;
    return NULL;
}

/* Block until an unread node exists. Return it, or NULL after shutdown.
 * Caller holds mutex. */
static sbuffer_node_t *wait_unread(sbuffer_t *b, bool storage,
                                   sbuffer_node_t **prev_out)
{
    for (;;)
    {
        sbuffer_node_t *node = find_unread(b, storage, prev_out);

        if (node != NULL)
        {
            return node;
        }
        if (b->shutdown)
        {
            return NULL;
        }
        pthread_cond_wait(&b->cond, &b->mutex);
    }
}

int sbuffer_init(sbuffer_t **buffer)
{
    sbuffer_t *b;

    if (buffer == NULL)
    {
        return SBUFFER_FAILURE;
    }

    b = calloc(1, sizeof(*b));
    if (b == NULL)
    {
        return SBUFFER_FAILURE;
    }

    pthread_mutex_init(&b->mutex, NULL);
    pthread_cond_init(&b->cond, NULL);

    *buffer = b;
    return SBUFFER_SUCCESS;
}

void sbuffer_free(sbuffer_t **buffer)
{
    sbuffer_node_t *curr;

    if (buffer == NULL || *buffer == NULL)
    {
        return;
    }

    curr = (*buffer)->head;
    while (curr != NULL)
    {
        sbuffer_node_t *next = curr->next;

        free(curr);
        curr = next;
    }

    pthread_mutex_destroy(&(*buffer)->mutex);
    pthread_cond_destroy(&(*buffer)->cond);
    free(*buffer);
    *buffer = NULL;
}

int sbuffer_insert(sbuffer_t *buffer, const sensor_data_t *data)
{
    sbuffer_node_t *node;

    if (buffer == NULL || data == NULL)
    {
        return SBUFFER_FAILURE;
    }

    node = calloc(1, sizeof(*node));      /* malloc outside the lock */
    if (node == NULL)
    {
        return SBUFFER_FAILURE;
    }
    node->data = *data;

    pthread_mutex_lock(&buffer->mutex);

    if (buffer->tail == NULL)
    {
        buffer->head = node;
    }
    else
    {
        buffer->tail->next = node;
    }
    buffer->tail = node;
    buffer->size++;

    pthread_cond_broadcast(&buffer->cond);
    pthread_mutex_unlock(&buffer->mutex);

    return SBUFFER_SUCCESS;
}

int sbuffer_read_datamgr(sbuffer_t *buffer, sensor_data_t *data)
{
    sbuffer_node_t *prev;
    sbuffer_node_t *node;

    pthread_mutex_lock(&buffer->mutex);

    node = wait_unread(buffer, false, &prev);
    if (node == NULL)
    {
        pthread_mutex_unlock(&buffer->mutex);
        return SBUFFER_NO_DATA;
    }

    *data = node->data;
    node->read_by_datamgr = true;
    remove_if_done(buffer, prev, node);

    pthread_mutex_unlock(&buffer->mutex);
    return SBUFFER_SUCCESS;
}

int sbuffer_peek_storagemgr(sbuffer_t *buffer, sensor_data_t *data)
{
    sbuffer_node_t *prev;
    sbuffer_node_t *node;

    pthread_mutex_lock(&buffer->mutex);

    node = wait_unread(buffer, true, &prev);
    if (node == NULL)
    {
        pthread_mutex_unlock(&buffer->mutex);
        return SBUFFER_NO_DATA;
    }

    *data = node->data;                    /* flag NOT set yet */

    pthread_mutex_unlock(&buffer->mutex);
    return SBUFFER_SUCCESS;
}

/* Only the Storage manager sets read_by_storagemgr, and nodes are only
 * appended at the tail, so the first unread node is still the one that
 * was peeked. It cannot have been freed: its storage flag was false. */
int sbuffer_commit_storagemgr(sbuffer_t *buffer)
{
    sbuffer_node_t *prev;
    sbuffer_node_t *node;

    pthread_mutex_lock(&buffer->mutex);

    node = find_unread(buffer, true, &prev);
    if (node == NULL)
    {
        pthread_mutex_unlock(&buffer->mutex);
        return SBUFFER_FAILURE;
    }

    node->read_by_storagemgr = true;
    remove_if_done(buffer, prev, node);

    pthread_mutex_unlock(&buffer->mutex);
    return SBUFFER_SUCCESS;
}

int sbuffer_read_storagemgr(sbuffer_t *buffer, sensor_data_t *data)
{
    int ret = sbuffer_peek_storagemgr(buffer, data);

    if (ret == SBUFFER_SUCCESS)
    {
        ret = sbuffer_commit_storagemgr(buffer);
    }
    return ret;
}

void sbuffer_shutdown(sbuffer_t *buffer)
{
    pthread_mutex_lock(&buffer->mutex);
    buffer->shutdown = true;
    pthread_cond_broadcast(&buffer->cond);
    pthread_mutex_unlock(&buffer->mutex);
}

size_t sbuffer_size(sbuffer_t *buffer)
{
    size_t size;

    pthread_mutex_lock(&buffer->mutex);
    size = buffer->size;
    pthread_mutex_unlock(&buffer->mutex);

    return size;
}
