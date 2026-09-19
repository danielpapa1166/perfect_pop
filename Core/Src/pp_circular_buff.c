/*
 * pp_circular_buff.c
 *
 *      Author: daniel_papa
 */


#include "pp_circular_buff.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

//#define DEBUG


cb_status_t cb_init(circular_buffer_t *cb, void *buffer, size_t buffer_size, size_t item_size) {
    if (!cb || !buffer || buffer_size == 0 || item_size == 0) {
        return CB_ERROR;
    }
    cb->buffer = buffer;
    cb->head = 0;
    cb->tail = 0;
    cb->buffer_size = buffer_size;
    cb->item_size = item_size;
    pthread_mutex_init(&cb->mutex, NULL);
    return CB_OK;
}

cb_status_t cb_free(circular_buffer_t *cb) {
    if (!cb) {
        return CB_ERROR;
    }
    pthread_mutex_destroy(&cb->mutex);
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
    pthread_mutex_lock(&cb->mutex);
    size_t next_head = (cb->head + 1) % cb->buffer_size;
    if (next_head == cb->tail) {
        pthread_mutex_unlock(&cb->mutex);

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

    pthread_mutex_unlock(&cb->mutex);
    return CB_OK;
}

cb_status_t cb_pop(circular_buffer_t * const cb, void * const item) {
    if (!cb || !item) {
        return CB_ERROR;
    }
    pthread_mutex_lock(&cb->mutex);
    if (cb->head == cb->tail) {
        pthread_mutex_unlock(&cb->mutex);

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
    
    pthread_mutex_unlock(&cb->mutex);
    return CB_OK;
}