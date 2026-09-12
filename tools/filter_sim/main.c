#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "pp_audio_buffer_config.h"
#include "pp_dsp.h"

#ifndef FILTER_SIM_INPUT_PATH
#define FILTER_SIM_INPUT_PATH "data/chirp_noise_48khz_s16le.pcm"
#endif

#ifndef FILTER_SIM_OUTPUT_PATH
#define FILTER_SIM_OUTPUT_PATH "data/chirp_noise_xcorr_48khz_f32le.raw"
#endif

_Static_assert(sizeof(float) == 4U, "The simulator output requires 32-bit floats");

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t input_available;
    pthread_cond_t chunk_processed;
    bool producer_finished;
    bool failed;
    size_t chunks_enqueued;
    size_t chunks_consumed;
    size_t chunks_written;
} simulation_context_t;

static void mark_failed(simulation_context_t *context)
{
    pthread_mutex_lock(&context->mutex);
    context->failed = true;
    context->producer_finished = true;
    pthread_cond_broadcast(&context->input_available);
    pthread_cond_broadcast(&context->chunk_processed);
    pthread_mutex_unlock(&context->mutex);
}

static void mark_producer_finished(simulation_context_t *context)
{
    pthread_mutex_lock(&context->mutex);
    context->producer_finished = true;
    pthread_cond_broadcast(&context->input_available);
    pthread_mutex_unlock(&context->mutex);
}

static void *producer_thread(void *argument)
{
    simulation_context_t *context = argument;
    FILE *input_file;
    FILE *output_file;
    const size_t chunk_size = AUDIO_LEN;
    int16_t input_buffer[AUDIO_LEN];
    float xcorr_buffer[AUDIO_LEN];
    size_t samples_read;

    input_file = fopen(FILTER_SIM_INPUT_PATH, "rb");
    if (input_file == NULL) {
        perror(FILTER_SIM_INPUT_PATH);
        mark_failed(context);
        return NULL;
    }

    output_file = fopen(FILTER_SIM_OUTPUT_PATH, "wb");
    if (output_file == NULL) {
        perror(FILTER_SIM_OUTPUT_PATH);
        fclose(input_file);
        mark_failed(context);
        return NULL;
    }

    while ((samples_read = fread(input_buffer, sizeof(input_buffer[0]), chunk_size,
                                 input_file)) != 0U) {
        if (samples_read != chunk_size) {
            fprintf(stderr, "Input contains a partial %zu-sample chunk\n", chunk_size);
            mark_failed(context);
            break;
        }

        pthread_mutex_lock(&context->mutex);
        while (context->chunks_enqueued != context->chunks_consumed &&
               !context->failed) {
            pthread_cond_wait(&context->chunk_processed, &context->mutex);
        }

        if (context->failed) {
            pthread_mutex_unlock(&context->mutex);
            break;
        }

        if (push_audio_buffer(input_buffer, chunk_size) != 0) {
            fprintf(stderr, "Failed to enqueue an input chunk\n");
            context->failed = true;
            context->producer_finished = true;
            pthread_cond_broadcast(&context->input_available);
            pthread_cond_broadcast(&context->chunk_processed);
            pthread_mutex_unlock(&context->mutex);
            break;
        }

        context->chunks_enqueued++;
        pthread_cond_signal(&context->input_available);

        while (context->chunks_consumed != context->chunks_enqueued &&
               !context->failed) {
            pthread_cond_wait(&context->chunk_processed, &context->mutex);
        }

        if (!context->failed) {
            get_xcorr_buffer_48kHz(xcorr_buffer);
        }

        const bool failed = context->failed;
        pthread_mutex_unlock(&context->mutex);

        if (failed) {
            break;
        }

        if (fwrite(xcorr_buffer, sizeof(xcorr_buffer[0]), chunk_size,
                   output_file) != chunk_size) {
            perror(FILTER_SIM_OUTPUT_PATH);
            mark_failed(context);
            break;
        }
        context->chunks_written++;
    }

    if (ferror(input_file) != 0) {
        perror(FILTER_SIM_INPUT_PATH);
        mark_failed(context);
    }

    fclose(input_file);
    if (fclose(output_file) != 0) {
        perror(FILTER_SIM_OUTPUT_PATH);
        mark_failed(context);
    }

    mark_producer_finished(context);
    return NULL;
}

static void *consumer_thread(void *argument)
{
    simulation_context_t *context = argument;

    for (;;) {
        pthread_mutex_lock(&context->mutex);
        while (context->chunks_enqueued == context->chunks_consumed &&
               !context->producer_finished && !context->failed) {
            pthread_cond_wait(&context->input_available, &context->mutex);
        }

        if (context->failed ||
            (context->producer_finished &&
             context->chunks_enqueued == context->chunks_consumed)) {
            pthread_mutex_unlock(&context->mutex);
            return NULL;
        }

        if (dsp_consume_audio_buffer() != 0) {
            fprintf(stderr, "Failed to consume an input chunk\n");
            context->failed = true;
            context->producer_finished = true;
            pthread_cond_broadcast(&context->input_available);
            pthread_cond_broadcast(&context->chunk_processed);
            pthread_mutex_unlock(&context->mutex);
            return NULL;
        }

        context->chunks_consumed++;
        pthread_cond_signal(&context->chunk_processed);
        pthread_mutex_unlock(&context->mutex);
    }
}

int main(void)
{
    simulation_context_t context = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .input_available = PTHREAD_COND_INITIALIZER,
        .chunk_processed = PTHREAD_COND_INITIALIZER,
    };
    pthread_t producer;
    pthread_t consumer;

    dsp_filter_init();

    if (pthread_create(&consumer, NULL, consumer_thread, &context) != 0) {
        perror("pthread_create consumer");
        return EXIT_FAILURE;
    }

    if (pthread_create(&producer, NULL, producer_thread, &context) != 0) {
        perror("pthread_create producer");
        mark_failed(&context);
        pthread_join(consumer, NULL);
        return EXIT_FAILURE;
    }

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    if (context.failed) {
        return EXIT_FAILURE;
    }

    printf("Processed %zu chunks of %u samples from %s\n", context.chunks_consumed,
           AUDIO_LEN, FILTER_SIM_INPUT_PATH);
    printf("Producer wrote %zu x-correlation chunks to %s\n", context.chunks_written,
           FILTER_SIM_OUTPUT_PATH);

    return EXIT_SUCCESS;
}