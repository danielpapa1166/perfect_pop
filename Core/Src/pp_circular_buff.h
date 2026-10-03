/*
 * pp_circular_buff.h
 *
 *      Author: daniel_papa
 */

#ifndef SRC_PP_CIRCULAR_BUFF_H_
#define SRC_PP_CIRCULAR_BUFF_H_

#include <stddef.h>

#ifdef FILTER_SIMULATION
#include <pthread.h>
#else
#include "cmsis_os2.h"
#endif

typedef enum {
    CB_OK = 0,
    CB_ERROR = -1,
    CB_FULL = -2,
    CB_EMPTY = -3
} cb_status_t;



#ifdef FILTER_SIMULATION
typedef struct {
    void *buffer;
    size_t head;
    size_t tail;
    size_t buffer_size;
    size_t item_size;
    pthread_mutex_t mutex;
} circular_buffer_t;

#else

typedef struct {
    void *buffer;
    size_t head;
    size_t tail;
    size_t buffer_size;
    size_t item_size;
    osMutexId_t mutex_id;
} circular_buffer_t;

#endif

cb_status_t cb_init(circular_buffer_t *cb, void *buffer, size_t buffer_size, size_t item_size); 
cb_status_t cb_free(circular_buffer_t *cb);
cb_status_t cb_push(circular_buffer_t * const cb, const void * const item);
cb_status_t cb_pop(circular_buffer_t * const cb, void * const item);



#endif /* SRC_PP_CIRCULAR_BUFF_H_ */
