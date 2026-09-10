/*
 * pp_dsp.c
 *
 *  Created on: Sep 9, 2026
 *      Author: daniel_papa
 */


#include "pp_dsp.h"


#define FILTER_SIZE 	1


void dsp_test_filter(const int16_t * const buf_in, size_t buf_size, int16_t * const buf_out) {
	size_t i, j;
	int32_t temp_sum;
	for (i = 0; i < buf_size; i++) {
		/*temp_sum = 0;

		if(i + FILTER_SIZE < buf_size) {
			for (j = i; j <= (i + FILTER_SIZE); j ++) {
				if(j < buf_size) {
					temp_sum += buf_in[j];
				}
			}
			temp_sum /= FILTER_SIZE;
		}
		else {
			temp_sum = buf_in[i];
		}


		buf_out[i] = temp_sum;*/
		buf_out[i] = buf_in[i];
	}

	//buf_out[i] = buf_in[i];

}
