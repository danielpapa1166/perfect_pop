/*
 * pp_uart.c
 *
 *  Created on: Aug 27, 2026
 *      Author: daniel_papa
 */

#include "pp_uart.h"
#include "main.h"
#include "cmsis_os.h"
#include "string.h"

// ----------------------------------------------------------------------------

#define UART_BUFFER_SIZE 		1
#define UART_TX_MAX_SIZE		1024  // bytes

// Frame: [4 sync bytes][1 count byte][integer payload][1 checksum byte].
// The checksum lets the receiver reject a false marker match inside payload
// data and keep searching instead of misparsing it as a frame start.
static const uint8_t UART_SYNC_MARKER_INT32[4] = {0xAA, 0x55, 0xA5, 0x5A};
static const uint8_t UART_SYNC_MARKER_INT16[4] = {0xAA, 0x55, 0xA5, 0x5B};
#define UART_HEADER_SIZE 		(sizeof(UART_SYNC_MARKER_INT32) + 1)
#define UART_FOOTER_SIZE 		1

typedef enum {
	UART_FREE,
	UART_BUSY
} uart_status_t;

// ----------------------------------------------------------------------------

static UART_HandleTypeDef * m_uart_instance_hdl = NULL;
static uart_status_t dev_stat = UART_FREE;


static uint8_t RxData[UART_BUFFER_SIZE];
static uint8_t TxData[UART_TX_MAX_SIZE];


static int rxCnt = 0;
static int txCnt = 0;

// ----------------------------------------------------------------------------

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
	rxCnt ++;
	__asm("nop");

	HAL_UART_Transmit_DMA(m_uart_instance_hdl, RxData, UART_BUFFER_SIZE);
}


void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
	txCnt ++;
	__asm("nop");
	dev_stat = UART_FREE;
}

// ----------------------------------------------------------------------------


int exec_uart_task(UART_HandleTypeDef * const uart_hdl) {
    m_uart_instance_hdl = uart_hdl;

    HAL_UART_Receive_DMA(m_uart_instance_hdl, RxData, UART_BUFFER_SIZE);

	static int cnt = 0;
    while(1) {
    	__asm("nop");

		cnt ++;
		HAL_GPIO_WritePin(
				LD_USER2_GPIO_Port,
				LD_USER2_Pin,
				(cnt % 2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
		osDelay(500);
    }
}


static int send_uart_integers(
		const void * const txBuffer,
		size_t bufferSize,
		const uint8_t * const syncMarker,
		size_t integerSize) {

	if(dev_stat != UART_FREE) {
		return -1;
	}
	else {
		dev_stat = UART_BUSY;
	}

	int retval = 0;

	size_t maxBufferSize =
			(UART_TX_MAX_SIZE - UART_HEADER_SIZE - UART_FOOTER_SIZE) / integerSize;
	if(maxBufferSize > UINT8_MAX) {
		maxBufferSize = UINT8_MAX;
	}
	if(bufferSize > maxBufferSize) {
		bufferSize = maxBufferSize;
		retval = 1;
	}

	memset(TxData, 0, UART_TX_MAX_SIZE);
	const size_t payload_size = bufferSize * integerSize;

	memcpy(TxData, syncMarker, sizeof(UART_SYNC_MARKER_INT32));
	TxData[sizeof(UART_SYNC_MARKER_INT32)] = (uint8_t) bufferSize;
	memcpy(TxData + UART_HEADER_SIZE, txBuffer, payload_size);

	uint8_t checksum = 0;
	for (size_t i = sizeof(UART_SYNC_MARKER_INT32); i < UART_HEADER_SIZE + payload_size; i++) {
		checksum += TxData[i];
	}
	TxData[UART_HEADER_SIZE + payload_size] = checksum;

	const size_t copy_size = UART_HEADER_SIZE + payload_size + UART_FOOTER_SIZE;
	HAL_UART_Transmit_DMA(m_uart_instance_hdl, TxData, copy_size);


	return retval;
}


int send_uart_int32(const int32_t * const txBuffer, size_t bufferSize) {
	return send_uart_integers(txBuffer, bufferSize, UART_SYNC_MARKER_INT32, sizeof(*txBuffer));
}


int send_uart_int16(const int16_t * const txBuffer, size_t bufferSize) {
	return send_uart_integers(txBuffer, bufferSize, UART_SYNC_MARKER_INT16, sizeof(*txBuffer));
}

