#ifndef SBUFFER_H
#define SBUFFER_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#include "sensor_types.h"

typedef struct sbuffer_node
{
    sensor_data_t data;

    bool read_by_datamgr;       /* Data manager has read this element    */
    bool read_by_storagemgr;    /* Storage manager has stored it         */

    struct sbuffer_node *next;
} sbuffer_node_t;

typedef struct
{
    sbuffer_node_t *head;
    sbuffer_node_t *tail;
    size_t size;

    pthread_mutex_t mutex;
    pthread_cond_t cond;

    bool shutdown;
} sbuffer_t;

typedef enum
{
    SBUFFER_SUCCESS = 0,
    SBUFFER_FAILURE = -1,
    SBUFFER_NO_DATA = 1         /* shutdown and nothing left to read */
} sbuffer_state_t;

int sbuffer_init(sbuffer_t **buffer);
void sbuffer_free(sbuffer_t **buffer);

/* Connection manager */
int sbuffer_insert(sbuffer_t *buffer, const sensor_data_t *data);

/* Data manager: copy the next element and set its flag */
int sbuffer_read_datamgr(sbuffer_t *buffer, sensor_data_t *data);

/* Storage manager (Step 6): two-phase read so that an element stays in
 * sbuffer until it is really stored in the database.
 *   peek:   block until an element is available, copy it, flag NOT set
 *   commit: set the storage flag of that element (call after INSERT OK)  */
int sbuffer_peek_storagemgr(sbuffer_t *buffer, sensor_data_t *data);
int sbuffer_commit_storagemgr(sbuffer_t *buffer);

/* Kept for compatibility: peek + commit in one call */
int sbuffer_read_storagemgr(sbuffer_t *buffer, sensor_data_t *data);

/* Wake up blocked readers, they return SBUFFER_NO_DATA when empty */
void sbuffer_shutdown(sbuffer_t *buffer);

/* Number of elements (for debug / shared status later) */
size_t sbuffer_size(sbuffer_t *buffer);

#endif
