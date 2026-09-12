/*
 * pp_dsp.h
 *
 *  Created on: Sep 9, 2026
 *      Author: daniel_papa
 */

#ifndef SRC_PP_DSP_H_
#define SRC_PP_DSP_H_

#include <stddef.h>
#include <stdint.h>
void dsp_filter_init(void);
void dsp_test_filter(const int16_t * const buf_in, size_t buf_size, int16_t * const buf_out);
int push_audio_buffer(const int16_t * const buf_in, size_t buf_size);
int dsp_consume_audio_buffer(void);

void get_xcorr_buffer_48kHz(float * const buf_out);
#endif /* SRC_PP_DSP_H_ */
