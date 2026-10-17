/*
 * pp_display_control.h
 *
 *  Created on: Oct 15, 2026
 *      Author: daniel_papa
 */

#ifndef SRC_PP_DISPLAY_CONTROL_H_
#define SRC_PP_DISPLAY_CONTROL_H_

#include "stm32f7xx_hal.h"

void display_init(LTDC_HandleTypeDef * f_ltdc_ptr, DSI_HandleTypeDef * f_dsi_ptr);
void display_cyclic_exec_funciton(void);


#endif /* SRC_PP_DISPLAY_CONTROL_H_ */
