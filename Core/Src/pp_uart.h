/*
 * pp_uart.h
 *
 *  Created on: Aug 27, 2026
 *      Author: daniel_papa
 */

#ifndef SRC_PP_UART_H_
#define SRC_PP_UART_H_


#include "stm32f7xx.h"
#include "stm32f7xx_hal_uart.h"
#include "stm32f7xx_hal_uart_ex.h"


int exec_uart_task(UART_HandleTypeDef * const uart_hdl);
int send_uart_integer(const int32_t * const txBuffer, size_t bufferSize);


#endif /* SRC_PP_UART_H_ */
