/*
 * pp_pdm.c
 *
 *  Created on: Aug 27, 2026
 *      Author: daniel_papa
 */


#include <string.h>
#include "pp_audio_buffer_config.h"

#include "cmsis_os.h"
#include "pp_dsp.h"
#include "pp_sai.h"

#include "main.h"
#include "pp_pdm.h"
#include "pp_uart.h"
#include "stm32f7xx_hal_def.h"



// ----------------------------------------------------------------------------

static DFSDM_Filter_HandleTypeDef * m_filter_handle = NULL;

// DTCM (default RAM) is not reachable by DMA2
int32_t micRecBuf[AUDIO_LEN] __attribute__((section(".dma_buffer")));
int16_t micAudioBuf[AUDIO_LEN] __attribute__((section(".dma_buffer")));

int16_t filtered_buf[AUDIO_LEN] __attribute__((section(".dma_buffer")));


float xcorr_buffer_48kHz[AUDIO_LEN];
float conv_val = 10; 
int16_t xcorr_buffer_48kHz_int16[AUDIO_LEN] __attribute__((section(".dma_buffer")));

volatile uint8_t audioFilterHalfCplt = 0;
volatile uint8_t audioFilterCplt = 0;
volatile uint32_t dma2Stream0IrqCount = 0;
volatile uint32_t audioFilterHalfCpltCount = 0;
volatile uint32_t audioFilterCpltCount = 0;
volatile uint32_t audioFilterErrorCode = 0;
volatile int saiDmaStatus = 0;
volatile uint32_t saiDmaErrorCount = 0;
volatile uint32_t saiDmaUnderrunCount = 0;
volatile uint32_t saiDmaErrorCode = 0;

/*PDM_Filter_Handler_t PDM1_filter_handler;
PDM_Filter_Config_t PDM1_filter_config;*/

static int32_t test_buffer[] = {
		1234,
		-5678,
		9876,
		1222222
};

static int m_uart_send_failed = 0;

// ----------------------------------------------------------------------------

void HAL_DFSDM_FilterRegConvHalfCpltCallback(
		DFSDM_Filter_HandleTypeDef *hdfsdm_filter) {

	if (hdfsdm_filter == m_filter_handle) {
		audioFilterHalfCpltCount++;
		audioFilterHalfCplt = 1;
	}
}
void HAL_DFSDM_FilterRegConvCpltCallback(
		DFSDM_Filter_HandleTypeDef *hdfsdm_filter) {

	if (hdfsdm_filter == m_filter_handle) {
		audioFilterCpltCount++;
		audioFilterCplt = 1;
	}
}

void HAL_DFSDM_FilterErrorCallback(
		DFSDM_Filter_HandleTypeDef *hdfsdm_filter) {
	if (hdfsdm_filter == m_filter_handle) {
		audioFilterErrorCode = hdfsdm_filter->ErrorCode;
	}
}

// ----------------------------------------------------------------------------




int exec_pdm_task(DFSDM_Filter_HandleTypeDef * const dfsdm_filter_hdl) {

	/*
	 *   __HAL_RCC_CRC_CLK_ENABLE();
  CRC->CR = CRC_CR_RESET;

  PDM1_filter_handler.bit_order = PDM_FILTER_BIT_ORDER_LSB;
  PDM1_filter_handler.endianness = PDM_FILTER_ENDIANNESS_BE;
  PDM1_filter_handler.high_pass_tap = 2104533974; // Coff = 0.98 -> get this number as 0.98*(2^31-1)
  PDM1_filter_handler.in_ptr_channels = 1;
  PDM1_filter_handler.out_ptr_channels = 1;
  uint32_t ecode = PDM_Filter_Init(&PDM1_filter_handler);

  PDM1_filter_config.decimation_factor = PDM_FILTER_DEC_FACTOR_64;
  PDM1_filter_config.output_samples_number = AUDIO_LEN;
  PDM1_filter_config.mic_gain = 0;
  ecode = PDM_Filter_setConfig(&PDM1_filter_handler, &PDM1_filter_config);
	 * */

	dsp_filter_init();

	m_filter_handle = dfsdm_filter_hdl;

	memset(micRecBuf, 0, sizeof(micRecBuf[0]) * AUDIO_LEN);
	memset(micAudioBuf, 0, sizeof(micAudioBuf[0]) * AUDIO_LEN);
	memset(filtered_buf, 0, sizeof(filtered_buf[0]) * AUDIO_LEN);
	const HAL_StatusTypeDef res = HAL_DFSDM_FilterRegularStart_DMA(
			m_filter_handle, micRecBuf, AUDIO_LEN);

	const uint64_t pBuf = (uint64_t) &micRecBuf;
	(void) pBuf;
	if (res != HAL_OK) {
		while(1) {
		  __asm("nop");
		}
	}



	static int cnt = 0;
	static int i = 0;
	while(1) {
		// first half of DMA is filled, now process
		if (audioFilterHalfCplt == 1) {
		    // fill the audio buffer
		    for (i = 0; i < H_AUDIO_LEN; i++) {
	            micAudioBuf[i] = (int16_t) (micRecBuf[i] >> 8);
		    }
		    // reset the flag
		    audioFilterHalfCplt = 0;
		}
		// second half is filled, finish process
		if (audioFilterCplt == 1) {
		    // fill the audio buffer
		    for (i = H_AUDIO_LEN; i < AUDIO_LEN; i++) {
	            micAudioBuf[i] = (int16_t) (micRecBuf[i] >> 8);
	        }
	        audioFilterCplt = 0;

	        //PDM_Filter(&micAudioBuf[0], &pcm_buff[0], &PDM1_filter_handler);

		    cnt ++;
		    if(cnt % 200 == 0) {
			    HAL_GPIO_TogglePin(
					LD_USER1_GPIO_Port,
					LD_USER1_Pin);
		    }


			/*push_audio_buffer(micAudioBuf, AUDIO_LEN);
			get_xcorr_buffer_48kHz(xcorr_buffer_48kHz); 
			for (size_t i = 0; i < AUDIO_LEN; i++) {
				xcorr_buffer_48kHz_int16[i] = (int16_t)(xcorr_buffer_48kHz[i] * conv_val);
			}*/

		    //send_uart_int32(test_buffer, sizeof(test_buffer) / sizeof(test_buffer[0]));
		    const int res = send_uart_int16(micAudioBuf, AUDIO_LEN);
		    if(res == -1) {
		    	m_uart_send_failed ++;
		    }

		    saiDmaStatus = sai_submit_mono_block(micAudioBuf, AUDIO_LEN);
		    if (saiDmaStatus != 0) {
				saiDmaErrorCount++;
		    }
		    saiDmaUnderrunCount = sai_get_tx_underrun_count();
		    saiDmaErrorCode = sai_get_error_code();
		}

	    osDelay(1);
	}


	return 0;
}
