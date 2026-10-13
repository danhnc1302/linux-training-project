#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "sbuffer.h"

static void remove_node_if_done(
        sbuffer_t *buffer,
        sbuffer_node_t *prev,
        sbuffer_node_t *node)
{
    printf("[CHECK DELETE] seq=%lu data=%d store=%d\n",
       node->data.seq,
       node->read_by_datamgr,
       node->read_by_storagemgr);

    if (!node->read_by_datamgr ||
        !node->read_by_storagemgr)
    {
        return;
    }

    printf("[DELETE] seq=%lu\n",
           node->data.seq);

    if (prev == NULL)
    {
        buffer->head = node->next;
    }
    else
    {
        prev->next = node->next;
    }

    if (buffer->tail == node)
    {
        buffer->tail = prev;
    }

    free(node);
}

int sbuffer_init(sbuffer_t **buffer)
{
    *buffer = malloc(sizeof(sbuffer_t));

    if (*buffer == NULL)
    {
        return SBUFFER_FAILURE;
    }

    (*buffer)->head = NULL;
    (*buffer)->tail = NULL;
    (*buffer)->shutdown = false;

    pthread_mutex_init(&(*buffer)->mutex, NULL);
    pthread_cond_init(&(*buffer)->cond, NULL);

    return SBUFFER_SUCCESS;
}

int sbuffer_insert(
        sbuffer_t *buffer,
        const sensor_data_t *data)
{

    printf("[INSERT] seq=%lu id=%u temp=%.2f ts=%ld\n",
       data->seq,
       data->sensor_id,
       data->value,
       data->timestamp);

    if (buffer == NULL || data == NULL)
    {
        return SBUFFER_FAILURE;
    }

    sbuffer_node_t *node =
        malloc(sizeof(sbuffer_node_t));

    if (node == NULL)
    {
        return SBUFFER_FAILURE;
    }

    node->data = *data;
    node->read_by_datamgr = false;
    node->read_by_storagemgr = false;
    node->next = NULL;

    pthread_mutex_lock(&buffer->mutex);

    if (buffer->tail == NULL)
    {
        buffer->head = node;
        buffer->tail = node;
    }
    else
    {
        buffer->tail->next = node;
        buffer->tail = node;
    }

    pthread_cond_broadcast(&buffer->cond);

    pthread_mutex_unlock(&buffer->mutex);

    return SBUFFER_SUCCESS;
}

int sbuffer_read_datamgr(
        sbuffer_t *buffer,
        sensor_data_t *data)
{
    pthread_mutex_lock(&buffer->mutex);

    while (1)
    {
        sbuffer_node_t *curr = buffer->head;
        sbuffer_node_t *prev = NULL;

        while (curr)
        {
            if (!curr->read_by_datamgr)
            {
                *data = curr->data;

                printf("[DM READ ] seq=%lu d=%d s=%d\n",
       curr->data.seq,
       curr->read_by_datamgr,
       curr->read_by_storagemgr);

                curr->read_by_datamgr = true;

                remove_node_if_done(
                        buffer,
                        prev,
                        curr);

                pthread_mutex_unlock(
                        &buffer->mutex);

                return SBUFFER_SUCCESS;
            }

            prev = curr;
            curr = curr->next;
        }

        if (buffer->shutdown)
        {
            pthread_mutex_unlock(
                    &buffer->mutex);

            return SBUFFER_NO_DATA;
        }

        pthread_cond_wait(
                &buffer->cond,
                &buffer->mutex);
    }
}

int sbuffer_read_storagemgr(
        sbuffer_t *buffer,
        sensor_data_t *data)
{
    pthread_mutex_lock(&buffer->mutex);

    while (1)
    {
        sbuffer_node_t *curr = buffer->head;
        sbuffer_node_t *prev = NULL;

        while (curr)
        {
            if (!curr->read_by_storagemgr)
            {
                *data = curr->data;

                curr->read_by_storagemgr = true;

                remove_node_if_done(
                        buffer,
                        prev,
                        curr);

                pthread_mutex_unlock(
                        &buffer->mutex);

                return SBUFFER_SUCCESS;
            }

            prev = curr;
            curr = curr->next;
        }

        if (buffer->shutdown)
        {
            pthread_mutex_unlock(
                    &buffer->mutex);

            return SBUFFER_NO_DATA;
        }

        pthread_cond_wait(
                &buffer->cond,
                &buffer->mutex);
    }
}

void sbuffer_shutdown(
        sbuffer_t *buffer)
{
    pthread_mutex_lock(&buffer->mutex);

    buffer->shutdown = true;

    pthread_cond_broadcast(
            &buffer->cond);

    pthread_mutex_unlock(
            &buffer->mutex);
}

void sbuffer_free(
        sbuffer_t **buffer)
{
    if (buffer == NULL ||
        *buffer == NULL)
    {
        return;
    }

    sbuffer_node_t *curr =
        (*buffer)->head;

    while (curr)
    {
        sbuffer_node_t *next =
            curr->next;

        free(curr);

        curr = next;
    }

    pthread_mutex_destroy(
            &(*buffer)->mutex);

    pthread_cond_destroy(
            &(*buffer)->cond);

    free(*buffer);

    *buffer = NULL;
}