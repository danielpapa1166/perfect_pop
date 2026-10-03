/*
 * pp_circular_buff.c
 *
 *      Author: daniel_papa
 */


#include "pp_circular_buff.h"

#include <stdio.h>
#include <string.h>

#ifdef FILTER_SIMULATION
#include <pthread.h>
#else
#include "cmsis_os2.h"
#endif
//#define DEBUG

#ifndef FILTER_SIMULATION

static const osMutexAttr_t m_mutex_attr = {
	  NULL, 				// no name
	  osMutexRobust,   		// the mutex is automatically released when owner thread is terminated
	  NULL,					// NULL to use Automatic Dynamic Allocation for the mutex control block
	  0U,					// 0 as the default is no memory provided with cb_mem
};

#endif


cb_status_t cb_init(circular_buffer_t *cb, void *buffer, size_t buffer_size, size_t item_size) {
    if (!cb || !buffer || buffer_size == 0 || item_size == 0) {
        return CB_ERROR;
    }
    cb->buffer = buffer;
    cb->head = 0;
    cb->tail = 0;
    cb->buffer_size = buffer_size;
    cb->item_size = item_size;
#ifdef FILTER_SIMULATION
    pthread_mutex_init(&cb->mutex, NULL);
#else
    cb->mutex_id = osMutexNew(NULL); // use default or pass m_mutex_attr
#endif
    return CB_OK;
}

cb_status_t cb_free(circular_buffer_t *cb) {
    if (!cb) {
        return CB_ERROR;
    }

#ifdef FILTER_SIMULATION
    pthread_mutex_destroy(&cb->mutex);
#else
    osMutexDelete(cb->mutex_id);
#endif

    cb->buffer = NULL;
    cb->head = 0;
    cb->tail = 0;
    cb->buffer_size = 0;
    cb->item_size = 0;
    return CB_OK;
}

cb_status_t cb_push(circular_buffer_t * const cb, const void * const item) {
    if (!cb || !item) {
        return CB_ERROR;
    }

	#ifdef FILTER_SIMULATION
    pthread_mutex_lock(&cb->mutex);
	#else
    osMutexAcquire(cb->mutex_id, osWaitForever);
	#endif

    size_t next_head = (cb->head + 1) % cb->buffer_size;
    if (next_head == cb->tail) {
		#ifdef FILTER_SIMULATION
        pthread_mutex_unlock(&cb->mutex);
		#else
        osMutexRelease(cb->mutex_id);
		#endif

        #ifdef DEBUG
        printf("Circular buffer is full\n");
        #endif

        return CB_FULL; // Buffer is full
    }
    memcpy(cb->buffer + cb->head * cb->item_size, item, cb->item_size);
    cb->head = next_head;

    #ifdef DEBUG
    printf("Pushed item to circular buffer: "
        "head:%zu tail:%zu\n", cb->head, cb->tail);
    #endif

	#ifdef FILTER_SIMULATION
	pthread_mutex_unlock(&cb->mutex);
	#else
	osMutexRelease(cb->mutex_id);
	#endif
    return CB_OK;
}

cb_status_t cb_pop(circular_buffer_t * const cb, void * const item) {
    if (!cb || !item) {
        return CB_ERROR;
    }
	#ifdef FILTER_SIMULATION
	pthread_mutex_lock(&cb->mutex);
	#else
	osMutexAcquire(cb->mutex_id, osWaitForever);
	#endif
    if (cb->head == cb->tail) {
		#ifdef FILTER_SIMULATION
		pthread_mutex_unlock(&cb->mutex);
		#else
		osMutexRelease(cb->mutex_id);
		#endif

        #ifdef DEBUG
        printf("Circular buffer is empty\n");
        #endif

        return CB_EMPTY; // Buffer is empty
    }
    memcpy(item, cb->buffer + cb->tail * cb->item_size, cb->item_size);
    cb->tail = (cb->tail + 1) % cb->buffer_size;
    
    #ifdef DEBUG
    printf("Popped item from circular buffer: "
        "head:%zu tail:%zu\n", cb->head, cb->tail);
    #endif
    
	#ifdef FILTER_SIMULATION
	pthread_mutex_unlock(&cb->mutex);
	#else
	osMutexRelease(cb->mutex_id);
	#endif
    return CB_OK;
}
