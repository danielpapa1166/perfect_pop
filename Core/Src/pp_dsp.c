/*
 * pp_dsp.c
 *
 *  Created on: Sep 9, 2026
 *      Author: daniel_papa
 */


#include "pp_dsp.h"
#include "pp_audio_buffer_config.h"
#include "pp_chirp_signal.h"


#include <stdint.h>
#include <string.h>

#define FILTER_SIZE 					8U
#define FILTER_DECIMATION_FACTOR 		6
#define DECIMATED_BUFFER_SIZE  			(AUDIO_LEN / FILTER_DECIMATION_FACTOR)
#define X_CORR_FILTER_BUFFER_SIZE  		(DECIMATED_BUFFER_SIZE * 100)


#define DSP_INPUT_QUEUE_SIZE    		10

static int16_t m_input_queue[DSP_INPUT_QUEUE_SIZE][AUDIO_LEN];

// todo: make this platform specific mutext or semaphore (CMSIS or linux build host):
static uint8_t m_input_queue_mutex = 0;

static uint8_t m_input_queue_head = 0;
static uint8_t m_input_queue_tail = 0; 


static int16_t m_filter_buffer[AUDIO_LEN];
static int16_t m_decimated_buffer[DECIMATED_BUFFER_SIZE];

static float m_xcorr_input_buffer[X_CORR_FILTER_BUFFER_SIZE];
static float m_xcorr_output_buffer[X_CORR_FILTER_BUFFER_SIZE];

static int m_filter_type = 3;


static float m_alpha = 0.5f;
static float m_beta = 0.001f;

static const float m_matched_filter_sample[FILTER_SIZE] = {
	0.000000f, 0.707107f, 1.000000f, 0.707107f,
	0.000000f, -0.707107f, -1.000000f, -0.707107f,
};

//static float m_matched_filter_10[FILTER_SIZE*10];
const int16_t * const m_x_corr_base_signal = pp_chirp_signal;

static void all_pass_filter(const int16_t * const buf_in, size_t buf_size, int16_t * const buf_out) {
	size_t i;
	for (i = 0; i < buf_size; i++) {
		buf_out[i] = buf_in[i];
	}
}

static void low_pass_filter(const int16_t * const buf_in, size_t buf_size, int16_t * const buf_out) {
	size_t i;
	for (i = 0; i < buf_size; i++) {
		if (i == 0) {
			buf_out[i] = buf_in[i];
		} else {
			buf_out[i] = (int16_t)(m_alpha * buf_in[i] + (1.0f - m_alpha) * buf_out[i - 1]);
		}
	}
}


static void anti_aliasing_filter(const int16_t * const buf_in, size_t buf_size, int16_t * const buf_out) {
	size_t i;
	buf_out[0] = buf_in[0];
	for (i = 1; i < buf_size; i++) {
		buf_out[i] = (buf_in[i] + buf_in[i - 1]) / 2;
	}
}

static void decimation_filter(const int16_t * const buf_in, size_t buf_size, int16_t * const buf_out, size_t factor) {
	size_t i;
	for (i = 0; i < buf_size / factor; i++) {
		buf_out[i] = buf_in[i * factor];
	}
}


static void matched_filter(const int16_t * const buf_in, size_t buf_size, int16_t * const buf_out) {

	if(buf_size > AUDIO_LEN) {
		// Handle error: buffer size too large for filter buffer
		return;
	}

	anti_aliasing_filter(buf_in, buf_size, m_filter_buffer);
	
	decimation_filter(
		m_filter_buffer, buf_size, 
		m_decimated_buffer, FILTER_DECIMATION_FACTOR);

	const size_t decimated_size = buf_size / FILTER_DECIMATION_FACTOR;

	size_t i;
	float sum; 
	for (i = 0; i < decimated_size; i++) {
		
		sum = 0.0f; 
		if (i < FILTER_SIZE*10) {
			sum = 0; //m_decimated_buffer[i] * (int64_t)(m_matched_filter_sample[i] * 32767);
		}
		else {
			for (size_t j = 0; j < FILTER_SIZE*10; j++) {
				if (i >= j) {

					float act_sample = (float)m_decimated_buffer[i - j] 
						* ((float) m_x_corr_base_signal[j]);

					sum += act_sample;
				}
			}

		}

		for (size_t j = 0; j < FILTER_DECIMATION_FACTOR; j++) {
			buf_out[i * FILTER_DECIMATION_FACTOR + j] = (int16_t)((sum) / FILTER_SIZE / 32767.0f);
		}
	}
}


static void x_corr_filter(const float * const buf_in, size_t buf_size, float * const buf_out) {
	size_t i;
	float sum; 
	for (i = 0; i < buf_size; i++) {
		
		sum = 0.0f; 

		for (size_t j = 0; j < FILTER_SIZE*10; j++) {
			if (i >= j) {

				float act_sample = (float)buf_in[i - j] 
					* ((float)m_x_corr_base_signal[j]);

				sum += act_sample;
			}
		}

		buf_out[i] = (float)((sum) / FILTER_SIZE / 32767.0f);
	}
}


static void high_pass_filter(const int16_t * const buf_in, size_t buf_size, int16_t * const buf_out) {

	size_t i;
	for (i = 0; i < buf_size; i++) {
		if (i == 0) {
			buf_out[i] = buf_in[i];
		} else {

			int16_t previous_output = buf_out[i - 1];
			int16_t previous_input = buf_in[i - 1];
			int16_t current_input = buf_in[i];

			int16_t term_a = (2 - m_beta) * (current_input - previous_input) / 2;
			int16_t term_b = (1 - m_beta) * previous_output;

			buf_out[i] = term_a + term_b;
		}
	}
}

void dsp_filter_init(void) {
	/*for (size_t i = 0; i < 10; i++) {
		memcpy(&m_matched_filter_10[i * FILTER_SIZE], 
			m_matched_filter_sample, 
			sizeof(m_matched_filter_sample));
	}*/

}


void dsp_test_filter(const int16_t * const buf_in, size_t buf_size, int16_t * const buf_out) {

	if(m_filter_type == 0) {
		all_pass_filter(buf_in, buf_size, buf_out);
	}
	else if(m_filter_type == 1) {
		low_pass_filter(buf_in, buf_size, buf_out);
	}
	else if(m_filter_type == 2) {
		high_pass_filter(buf_in, buf_size, buf_out);
	}
	else if(m_filter_type == 3) {
		matched_filter(buf_in, buf_size, buf_out);
	}
}


int push_audio_buffer(const int16_t * const buf_in, size_t buf_size) {

	if (buf_size != AUDIO_LEN) {
		return -1;
	}

	// shift the xcorr buffer 
	/*
    */

	while(m_input_queue_mutex) {
		// wait for the input queue to be free
	}
	
	// acquire the input queue mutex
	m_input_queue_mutex = 1;

	// store new samples: 
	memcpy(m_input_queue[m_input_queue_head], buf_in, buf_size * sizeof(int16_t));
	m_input_queue_head = (m_input_queue_head + 1) % DSP_INPUT_QUEUE_SIZE;
	
	// release the input queue mutex
	m_input_queue_mutex = 0;

	
	

	return 0;
}

int dsp_consume_audio_buffer(void) {
	// acquire the input queue mutex
	while(m_input_queue_mutex) {
		// wait for the input queue to be free
	}
	m_input_queue_mutex = 1;

	if(m_input_queue_head == m_input_queue_tail) {
		// queue is empty
		m_input_queue_mutex = 0;
		return -1;
	}

	for (size_t i = X_CORR_FILTER_BUFFER_SIZE - DECIMATED_BUFFER_SIZE - 1; i != (size_t)(-1); i--) {
		m_xcorr_input_buffer[i + DECIMATED_BUFFER_SIZE] = m_xcorr_input_buffer[i];
	}

	int16_t * const buf_in = m_input_queue[m_input_queue_tail];
	size_t buf_size = AUDIO_LEN;

	// consume the audio buffer
	anti_aliasing_filter(buf_in, buf_size, m_filter_buffer);
	
	m_input_queue_tail = (m_input_queue_tail + 1) % DSP_INPUT_QUEUE_SIZE;

	// release the input queue mutex
	m_input_queue_mutex = 0;

	// working in local buffers

	decimation_filter(
		m_filter_buffer, buf_size, 
		m_decimated_buffer, 
		FILTER_DECIMATION_FACTOR);

	for (size_t i = 0; i < DECIMATED_BUFFER_SIZE; i++) {
		m_xcorr_input_buffer[i] = m_decimated_buffer[i];
	}


	x_corr_filter(
		m_xcorr_input_buffer,
		X_CORR_FILTER_BUFFER_SIZE, 
		m_xcorr_output_buffer);


	return 0; 
}


void get_xcorr_buffer_48kHz(float * const buf_out) {
	size_t j; 
	for (size_t i = 0; i < AUDIO_LEN; i++) {
		j = X_CORR_FILTER_BUFFER_SIZE - (i / FILTER_DECIMATION_FACTOR) - 1;
		buf_out[i] = m_xcorr_output_buffer[j];
	}
}
