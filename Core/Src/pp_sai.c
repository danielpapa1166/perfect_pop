/*
 * pp_sai.c
 *
 *  Created on: Sep 9, 2026
 *      Author: daniel_papa
 */

#include <limits.h>

#include "pp_sai.h"

#include "wm8894/wm8994.h"

#define WM8994_I2C_ADDRESS          0x34U
#define WM8994_I2C_READY_TRIALS     3U
#define WM8994_I2C_TIMEOUT_MS       100U
#define WM8994_OUTPUT_VOLUME         80U
#define SAI_PLLI2S_N                172U
#define SAI_PLLI2S_Q                  7U
#define SAI_PLLI2S_DIVQ               1U
#define SAI_PLAYBACK_CHANNELS          2U
#define SAI_PLAYBACK_DMA_HALVES        2U
#define SAI_PLAYBACK_QUEUE_DEPTH       100U
#define SAI_PLAYBACK_MAX_FRAMES      128U
#define SAI_PLAYBACK_MAX_SAMPLES (SAI_PLAYBACK_MAX_FRAMES * SAI_PLAYBACK_CHANNELS)
#define SAI_PLAYBACK_DMA_SAMPLES (SAI_PLAYBACK_MAX_SAMPLES * SAI_PLAYBACK_DMA_HALVES)

static SAI_HandleTypeDef * m_sai_ptr = NULL;
static DMA_HandleTypeDef * m_dma_ptr = NULL;
static I2C_HandleTypeDef * m_i2c_ptr = NULL;
static WM8994_Object_t m_wm8994;
static int16_t m_playback_dma_buffer[SAI_PLAYBACK_DMA_SAMPLES]
    __attribute__((section(".dma_buffer")));
static int16_t m_playback_queue[SAI_PLAYBACK_QUEUE_DEPTH][SAI_PLAYBACK_MAX_SAMPLES];
static size_t m_playback_block_samples = 0U;
static volatile uint8_t m_playback_queue_read_index = 0U;
static volatile uint8_t m_playback_queue_write_index = 0U;
static volatile uint8_t m_playback_started = 0U;
static volatile uint8_t m_playback_repeat_source = 0U;
static volatile uint32_t m_playback_underrun_count = 0U;
static volatile uint32_t m_sai_error_code = HAL_SAI_ERROR_NONE;


static int32_t wm8994_i2c_init(void) {
    if (m_i2c_ptr == NULL) {
        return WM8994_ERROR;
    }

    return (HAL_I2C_IsDeviceReady(
            m_i2c_ptr,
            WM8994_I2C_ADDRESS,
            WM8994_I2C_READY_TRIALS,
            WM8994_I2C_TIMEOUT_MS) == HAL_OK) ? WM8994_OK : WM8994_ERROR;
}


static int32_t wm8994_i2c_deinit(void) {
    return WM8994_OK;
}


static int32_t wm8994_i2c_read_reg(
        uint16_t device_address,
        uint16_t register_address,
        uint8_t * data,
        uint16_t length) {
    if ((m_i2c_ptr == NULL) || (data == NULL) || (length == 0U)) {
        return WM8994_ERROR;
    }

    return (HAL_I2C_Mem_Read(
            m_i2c_ptr,
            device_address,
            register_address,
            I2C_MEMADD_SIZE_16BIT,
            data,
            length,
            WM8994_I2C_TIMEOUT_MS) == HAL_OK) ? WM8994_OK : WM8994_ERROR;
}


static int32_t wm8994_i2c_write_reg(
        uint16_t device_address,
        uint16_t register_address,
        uint8_t * data,
        uint16_t length) {
    if ((m_i2c_ptr == NULL) || (data == NULL) || (length == 0U)) {
        return WM8994_ERROR;
    }

    return (HAL_I2C_Mem_Write(
            m_i2c_ptr,
            device_address,
            register_address,
            I2C_MEMADD_SIZE_16BIT,
            data,
            length,
            WM8994_I2C_TIMEOUT_MS) == HAL_OK) ? WM8994_OK : WM8994_ERROR;
}


static int32_t wm8994_get_tick(void) {
    return (int32_t) HAL_GetTick();
}


static uint8_t sai_next_queue_index(uint8_t queue_index) {
    return (uint8_t) ((queue_index + 1U) % SAI_PLAYBACK_QUEUE_DEPTH);
}


static void sai_write_stereo_block(
        int16_t * const destination,
        const int16_t * const mono_buf,
        size_t frame_count) {
    size_t frame_index;

    for (frame_index = 0U; frame_index < frame_count; frame_index++) {
        destination[SAI_PLAYBACK_CHANNELS * frame_index] = mono_buf[frame_index];
        destination[(SAI_PLAYBACK_CHANNELS * frame_index) + 1U] = mono_buf[frame_index];
    }
}


static int sai_queue_mono_block(
        const int16_t * const mono_buf,
        size_t frame_count) {
    const uint8_t write_index = m_playback_queue_write_index;
    const uint8_t next_write_index = sai_next_queue_index(write_index);

    if (next_write_index == m_playback_queue_read_index) {
        return 1;
    }

    sai_write_stereo_block(m_playback_queue[write_index], mono_buf, frame_count);

    __DMB();
    m_playback_queue_write_index = next_write_index;

    return 0;
}


static void sai_refill_dma_half(size_t half_index) {
    const uint8_t read_index = m_playback_queue_read_index;
    const uint8_t write_index = m_playback_queue_write_index;
    size_t sample_index;
    int16_t * const destination =
            &m_playback_dma_buffer[half_index * m_playback_block_samples];

    __DMB();
    if (read_index == write_index) {
        if (m_playback_repeat_source != 0U) {
            return;
        }

        m_playback_underrun_count++;
        return;
    }
    for (sample_index = 0U;
            sample_index < m_playback_block_samples;
            sample_index++) {
        destination[sample_index] = m_playback_queue[read_index][sample_index];
    }

    __DMB();
    m_playback_queue_read_index = sai_next_queue_index(read_index);
}


void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef * sai_hdl) {
    if ((sai_hdl == m_sai_ptr) && (m_playback_started != 0U)) {
        sai_refill_dma_half(0U);
    }
}


void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef * sai_hdl) {
    if ((sai_hdl == m_sai_ptr) && (m_playback_started != 0U)) {
        sai_refill_dma_half(1U);
    }
}


void HAL_SAI_ErrorCallback(SAI_HandleTypeDef * sai_hdl) {
    if (sai_hdl == m_sai_ptr) {
        m_sai_error_code = HAL_SAI_GetError(sai_hdl);
    }
}


static HAL_StatusTypeDef sai_configure_48khz_clock(void) {
    RCC_PeriphCLKInitTypeDef peripheral_clock_config = {0};

    peripheral_clock_config.PeriphClockSelection = RCC_PERIPHCLK_SAI1;
    peripheral_clock_config.PLLI2S.PLLI2SN = SAI_PLLI2S_N;
    peripheral_clock_config.PLLI2S.PLLI2SQ = SAI_PLLI2S_Q;
    peripheral_clock_config.PLLI2SDivQ = SAI_PLLI2S_DIVQ;
    peripheral_clock_config.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLLI2S;

    return HAL_RCCEx_PeriphCLKConfig(&peripheral_clock_config);
}


int sai_init(
        SAI_HandleTypeDef * sai_hdl,
        DMA_HandleTypeDef * dma_hdl,
        I2C_HandleTypeDef * i2c_hdl) {
    if ((sai_hdl == NULL) || (dma_hdl == NULL) || (i2c_hdl == NULL)) {
        return -1;
    }

    m_sai_ptr = NULL;
    m_dma_ptr = NULL;
    m_i2c_ptr = i2c_hdl;

    __HAL_SAI_DISABLE(sai_hdl);
    if (sai_configure_48khz_clock() != HAL_OK) {
        return -8;
    }

    sai_hdl->Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
    sai_hdl->Init.FIFOThreshold = SAI_FIFOTHRESHOLD_1QF;
    sai_hdl->Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
    sai_hdl->Init.MonoStereoMode = SAI_STEREOMODE;
    if (HAL_SAI_InitProtocol(
            sai_hdl,
            SAI_I2S_STANDARD,
            SAI_PROTOCOL_DATASIZE_16BIT,
            2U) != HAL_OK) {
        return -7;
    }
    dma_hdl->Init.MemInc = DMA_MINC_ENABLE;
    dma_hdl->Init.Mode = DMA_CIRCULAR;
    if ((HAL_DMA_DeInit(dma_hdl) != HAL_OK) ||
            (HAL_DMA_Init(dma_hdl) != HAL_OK)) {
        return -9;
    }

    WM8994_IO_t io_context = {
        .Init = wm8994_i2c_init,
        .DeInit = wm8994_i2c_deinit,
        .Address = WM8994_I2C_ADDRESS,
        .ReadReg = wm8994_i2c_read_reg,
        .WriteReg = wm8994_i2c_write_reg,
        .GetTick = wm8994_get_tick,
    };
    WM8994_Init_t codec_init = {
        .InputDevice = WM8994_IN_NONE,
        .OutputDevice = WM8994_OUT_HEADPHONE,
        .Frequency = WM8994_FREQUENCY_48K,
        .Resolution = WM8994_RESOLUTION_16b,
        .Volume = WM8994_OUTPUT_VOLUME,
    };
    uint32_t codec_id;

	__HAL_SAI_ENABLE(sai_hdl);
	if (WM8994_RegisterBusIO(&m_wm8994, &io_context) != WM8994_OK) {
		return -2;
	}
	if ((WM8994_ReadID(&m_wm8994, &codec_id) != WM8994_OK) ||
			(codec_id != WM8994_ID)) {
		return -3;
	}
	if (WM8994_Reset(&m_wm8994) != WM8994_OK) {
		return -4;
	}
	if (WM8994_Init(&m_wm8994, &codec_init) != WM8994_OK) {
		return -5;
	}
	if (WM8994_Play(&m_wm8994) != WM8994_OK) {
		return -6;
	}

    m_sai_ptr = sai_hdl;
    m_dma_ptr = dma_hdl;
	 m_playback_block_samples = 0U;
	 m_playback_queue_read_index = 0U;
	 m_playback_queue_write_index = 0U;
	 m_playback_started = 0U;
     m_playback_repeat_source = 0U;
	 m_playback_underrun_count = 0U;
	 m_sai_error_code = HAL_SAI_ERROR_NONE;
	return 0;
}


static int sai_start_mono_block(
        const int16_t * const mono_buf,
    size_t frame_count,
    uint8_t repeat_source) {
    HAL_StatusTypeDef status;

    if ((m_sai_ptr == NULL) || (m_dma_ptr == NULL) || (mono_buf == NULL) ||
            (frame_count == 0U) ||
            (frame_count > SAI_PLAYBACK_MAX_FRAMES)) {
        return -1;
    }

    if (m_playback_started != 0U) {
        if (repeat_source != 0U) {
            return -2;
        }

        m_playback_repeat_source = 0U;
        if ((frame_count * SAI_PLAYBACK_CHANNELS) != m_playback_block_samples) {
            return -1;
        }

        return sai_queue_mono_block(mono_buf, frame_count);
    }

    if (HAL_SAI_GetState(m_sai_ptr) != HAL_SAI_STATE_READY) {
        return -2;
    }

    m_playback_block_samples = frame_count * SAI_PLAYBACK_CHANNELS;
    m_playback_queue_read_index = 0U;
    m_playback_queue_write_index = 0U;
    m_playback_repeat_source = repeat_source;
    m_playback_underrun_count = 0U;
    sai_write_stereo_block(m_playback_dma_buffer, mono_buf, frame_count);
    sai_write_stereo_block(
            &m_playback_dma_buffer[m_playback_block_samples],
            mono_buf,
            frame_count);

            __DMB();
    m_playback_started = 1U;
    status = HAL_SAI_Transmit_DMA(
            m_sai_ptr,
            (uint8_t *) m_playback_dma_buffer,
            (uint16_t) (m_playback_block_samples * SAI_PLAYBACK_DMA_HALVES));
    if (status != HAL_OK) {
        m_playback_started = 0U;
        m_playback_repeat_source = 0U;
        return -3;
    }

    return 0;
}


int sai_submit_mono_block(
        const int16_t * const mono_buf,
        size_t frame_count) {
    return sai_start_mono_block(mono_buf, frame_count, 0U);
}


int sai_start_repeating_mono_block(
        const int16_t * const mono_buf,
        size_t frame_count) {
    return sai_start_mono_block(mono_buf, frame_count, 1U);
}



uint32_t sai_get_tx_underrun_count(void) {
    return m_playback_underrun_count;
}


uint32_t sai_get_error_code(void) {
    return m_sai_error_code;
}
