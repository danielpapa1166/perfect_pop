/*
 * pp_dsp.c
 *
 *  Created on: Sep 9, 2026
 *      Author: daniel_papa
 */


#include "pp_dsp.h"
#include "pp_audio_buffer_config.h"
#include "pp_chirp_signal.h"
#include "pp_circular_buff.h"

//#define DEBUG

#include <stdint.h>
#include <string.h>

#define FILTER_SIZE 					8U
#define FILTER_DECIMATION_FACTOR 		6
#define DECIMATED_BUFFER_SIZE  			(AUDIO_LEN / FILTER_DECIMATION_FACTOR)

#define X_CORR_QUEUE_SIZE    			10
#define X_CORR_FILTER_BUFFER_SIZE  		(DECIMATED_BUFFER_SIZE * 100)


#define DSP_INPUT_QUEUE_SIZE    		10

static circular_buffer_t m_input_circular_buffer;
static circular_buffer_t m_output_circular_buffer;

static int16_t m_input_buffer_48kHz_si16[AUDIO_LEN * DSP_INPUT_QUEUE_SIZE];
static float m_output_buffer_8kHz_f32[DECIMATED_BUFFER_SIZE * X_CORR_QUEUE_SIZE];


#define LOCAL_WORK_BUFFER_LEN  10 //(AUDIO_LEN*3)
static int16_t m_local_work_buffer[AUDIO_LEN * LOCAL_WORK_BUFFER_LEN];
static int16_t m_filter_buffer[AUDIO_LEN * LOCAL_WORK_BUFFER_LEN];
static int16_t m_decimated_buffer[DECIMATED_BUFFER_SIZE * LOCAL_WORK_BUFFER_LEN];
static float m_xcorr_input_buffer[DECIMATED_BUFFER_SIZE * LOCAL_WORK_BUFFER_LEN];
static float m_xcorr_output_buffer[DECIMATED_BUFFER_SIZE * LOCAL_WORK_BUFFER_LEN];


static int16_t m_input_queue[DSP_INPUT_QUEUE_SIZE][AUDIO_LEN];

// todo: make this platform specific mutext or semaphore (CMSIS or linux build host):
static uint8_t m_input_queue_mutex = 0;

static uint8_t m_input_queue_head = 0;
static uint8_t m_input_queue_tail = 0; 





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

	float sum; 
	for (size_t buf_idx = buf_size - 1; buf_idx >= 0; buf_idx--) {
		if (buf_idx == (size_t)-1) break; // Prevent underflow for size_t
		
		sum = 0.0f; 

		for (size_t window_idx = 0; window_idx < FILTER_SIZE; window_idx++) {

			if(window_idx <= buf_idx) { // avoid accessing negative index in buf_in
				float act_sample = (float)buf_in[buf_idx - window_idx] 
					* ((float)m_x_corr_base_signal[window_idx]);

				sum += act_sample;
			}
		}

		buf_out[buf_idx] = (float)((sum) / FILTER_SIZE / 32767.0f);
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

	cb_init(
		&m_input_circular_buffer, 
		m_input_buffer_48kHz_si16, 
		DSP_INPUT_QUEUE_SIZE, 
		AUDIO_LEN * sizeof(m_input_buffer_48kHz_si16[0]));
	cb_init(
		&m_output_circular_buffer, 
		m_output_buffer_8kHz_f32, 
		X_CORR_QUEUE_SIZE, 
		DECIMATED_BUFFER_SIZE * sizeof(m_output_buffer_8kHz_f32[0]));

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

	cb_push(&m_input_circular_buffer, buf_in); // fixed audio length assumed

	return 0;
}

int pop_audio_buffer(void) {

	int16_t new_audio_sample_buff[AUDIO_LEN];

	cb_pop(&m_input_circular_buffer, new_audio_sample_buff);



	/*for (size_t i = X_CORR_FILTER_BUFFER_SIZE - DECIMATED_BUFFER_SIZE - 1; i != (size_t)(-1); i--) {
		m_xcorr_input_buffer[i + DECIMATED_BUFFER_SIZE] = m_xcorr_input_buffer[i];
	}*/

	// shift and drop latest samples 
	for (size_t i = 0; i < (LOCAL_WORK_BUFFER_LEN - 1) * AUDIO_LEN; i++) {
		m_local_work_buffer[i] = m_local_work_buffer[i + AUDIO_LEN];
	}

	// update new samples: 
	for (size_t i = 0; i < AUDIO_LEN; i++) {
		m_local_work_buffer[(LOCAL_WORK_BUFFER_LEN - 1) * AUDIO_LEN + i] 
			= new_audio_sample_buff[i];
	}

	size_t buf_size = AUDIO_LEN * LOCAL_WORK_BUFFER_LEN;

	anti_aliasing_filter(
		m_local_work_buffer, buf_size, 
		m_filter_buffer);
	

	decimation_filter(
		m_filter_buffer, 
		buf_size, 
		m_decimated_buffer, 
		FILTER_DECIMATION_FACTOR);

	for (size_t i = 0; i < DECIMATED_BUFFER_SIZE * LOCAL_WORK_BUFFER_LEN; i++) {
		m_xcorr_input_buffer[i] = m_decimated_buffer[i];
	}


	x_corr_filter(
		m_xcorr_input_buffer,
		DECIMATED_BUFFER_SIZE * LOCAL_WORK_BUFFER_LEN, 
		m_xcorr_output_buffer);


	float * xcorr_output_ptr = &m_xcorr_output_buffer[
		DECIMATED_BUFFER_SIZE * LOCAL_WORK_BUFFER_LEN - 1 - (2 * DECIMATED_BUFFER_SIZE)
	];

	cb_push(&m_output_circular_buffer, xcorr_output_ptr);


	return 0; 
}


void get_xcorr_buffer_48kHz(float * const buf_out) {

	float temp_buf[DECIMATED_BUFFER_SIZE]; 

	volatile cb_status_t cb_stat; 
	
	do {
		cb_stat = cb_pop(&m_output_circular_buffer, temp_buf);
	} while(cb_stat == CB_EMPTY);

	if(cb_stat != CB_OK) {
		return;
	}

	for (size_t i = 0; i < DECIMATED_BUFFER_SIZE; i++) {
		for (size_t j = 0; j < FILTER_DECIMATION_FACTOR; j++) {
			buf_out[i * FILTER_DECIMATION_FACTOR + j] = temp_buf[i];
		}
	}
}
