#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "pp_audio_buffer_config.h"
#include "pp_dsp.h"

#ifndef FILTER_SIM_INPUT_PATH
#define FILTER_SIM_INPUT_PATH "data/chirp_noise_48khz_s16le.pcm"
#endif

#ifndef FILTER_SIM_OUTPUT_PATH
#define FILTER_SIM_OUTPUT_PATH "data/chirp_noise_filtered_48khz_s16le.pcm"
#endif

int main(void)
{
    FILE *input_file;
    FILE *output_file;
    const size_t chunk_size = AUDIO_LEN;
    int16_t input_buffer[AUDIO_LEN];
    int16_t output_buffer[AUDIO_LEN];
    size_t chunks_processed = 0;
    size_t samples_read;

    input_file = fopen(FILTER_SIM_INPUT_PATH, "rb");
    if (input_file == NULL) {
        perror(FILTER_SIM_INPUT_PATH);
        return EXIT_FAILURE;
    }

    output_file = fopen(FILTER_SIM_OUTPUT_PATH, "wb");
    if (output_file == NULL) {
        perror(FILTER_SIM_OUTPUT_PATH);
        fclose(input_file);
        return EXIT_FAILURE;
    }

    dsp_filter_init();

    while ((samples_read = fread(input_buffer, sizeof(input_buffer[0]), chunk_size,
                                 input_file)) != 0U) {
        if (samples_read != chunk_size) {
            fprintf(stderr, "Input contains a partial %zu-sample chunk\n", chunk_size);
            fclose(input_file);
            fclose(output_file);
            return EXIT_FAILURE;
        }

        dsp_test_filter(input_buffer, chunk_size, output_buffer);
        if (fwrite(output_buffer, sizeof(output_buffer[0]), chunk_size,
                   output_file) != chunk_size) {
            perror(FILTER_SIM_OUTPUT_PATH);
            fclose(input_file);
            fclose(output_file);
            return EXIT_FAILURE;
        }
        chunks_processed++;
    }

    if (ferror(input_file) != 0) {
        perror(FILTER_SIM_INPUT_PATH);
        fclose(input_file);
        fclose(output_file);
        return EXIT_FAILURE;
    }

    fclose(input_file);
    if (fclose(output_file) != 0) {
        perror(FILTER_SIM_OUTPUT_PATH);
        return EXIT_FAILURE;
    }

    printf("Processed %zu chunks of %zu samples from %s\n", chunks_processed,
           chunk_size, FILTER_SIM_INPUT_PATH);
    printf("Wrote filtered samples to %s\n", FILTER_SIM_OUTPUT_PATH);

    return EXIT_SUCCESS;
}