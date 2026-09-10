/*
 * pp_sai.h
 *
 *  Created on: Sep 9, 2026
 *      Author: daniel_papa
 */

#ifndef SRC_PP_SAI_H_
#define SRC_PP_SAI_H_

#include "main.h"
#include "stm32f7xx_hal_sai.h"

int sai_init(SAI_HandleTypeDef * sai_hdl, DMA_HandleTypeDef * dma_hdl, I2C_HandleTypeDef * i2c_hdl);
int sai_submit_mono_block(const int16_t * const mono_buf, size_t frame_count);
int sai_start_repeating_mono_block(const int16_t * const mono_buf, size_t frame_count);
uint32_t sai_get_tx_underrun_count(void);
uint32_t sai_get_error_code(void);

#endif /* SRC_PP_SAI_H_ */
