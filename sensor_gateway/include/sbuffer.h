#ifndef SBUFFER_H
#define SBUFFER_H

#include <pthread.h>
#include <stdbool.h>
#include "sensor_types.h"

typedef struct sbuffer_node sbuffer_node_t;

struct sbuffer_node
{
    sensor_data_t data;

    bool read_by_datamgr;
    bool read_by_storagemgr;

    sbuffer_node_t *next;
};

typedef struct
{
    sbuffer_node_t *head;
    sbuffer_node_t *tail;

    pthread_mutex_t mutex;
    pthread_cond_t cond;

    bool shutdown;
} sbuffer_t;

typedef enum
{
    SBUFFER_SUCCESS = 0,
    SBUFFER_FAILURE = -1,
    SBUFFER_NO_DATA = 1
} sbuffer_state_t;

int sbuffer_init(sbuffer_t **buffer);

int sbuffer_insert(
        sbuffer_t *buffer,
        const sensor_data_t *data);

int sbuffer_read_datamgr(
        sbuffer_t *buffer,
        sensor_data_t *data);

int sbuffer_read_storagemgr(
        sbuffer_t *buffer,
        sensor_data_t *data);

void sbuffer_shutdown(
        sbuffer_t *buffer);

void sbuffer_free(
        sbuffer_t **buffer);

#endif