/*
 * pp_display_control.c
 *
 *  Created on: Oct 15, 2026
 *      Author: daniel_papa
 */

#include "pp_display_control.h"
#include "pp_screen_painter.h"
#include "main.h"
#include "otm8009a.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stm32f7xx_hal.h"
#include "lvgl.h"

#define LCD_W    800U
#define LCD_H    480U
#define DISPLAY_REFRESH_TIMEOUT_MS 100U


#define SDRAM_BASE_ADDR 0xC0000000UL
static volatile uint32_t * const sdram_ptr = (volatile uint32_t *)SDRAM_BASE_ADDR;

lv_obj_t *label = NULL;

static LTDC_HandleTypeDef * m_ltdc_ptr;
static DSI_HandleTypeDef * m_dsi_ptr;

/* Keep panel readbacks available to GDB, including each read's status. */
static volatile struct {
  uint32_t id;
  int32_t pixel_format_status;
  uint8_t pixel_format;
  int32_t memory_access_status;
  uint8_t memory_access;
  uint32_t ltdc_framebuffer;
  uint32_t ltdc_pixel_format;
  uint32_t ltdc_line_length;
  uint32_t ltdc_line_count;
  HAL_StatusTypeDef ltdc_config_status;
  HAL_StatusTypeDef refresh_status;
  uint32_t refresh_count;
} s_panel_debug;


/* NOLOAD: clear only after MX_FMC_Init(). Both LVGL and LTDC use this array.
 * The existing non-cacheable 2 MiB MPU region covers the entire framebuffer. */
static uint32_t draw_buf[LCD_W * LCD_H]
    __attribute__((section(".sdram_framebuffer"), aligned(32)));

static HAL_StatusTypeDef display_refresh(void)
{
    /* Poll completion; no DSI IRQ handler is required for this driver. */
    __HAL_DSI_CLEAR_FLAG(m_dsi_ptr, DSI_FLAG_ER);
    __DSB();

    HAL_StatusTypeDef status = HAL_DSI_Refresh(m_dsi_ptr);
    if (status != HAL_OK) {
        return status;
    }

    const uint32_t started = HAL_GetTick();
    while (__HAL_DSI_GET_FLAG(m_dsi_ptr, DSI_FLAG_ER) == 0U) {
        if ((HAL_GetTick() - started) >= DISPLAY_REFRESH_TIMEOUT_MS) {
            return HAL_TIMEOUT;
        }
        /* Yield the display task while LTDC/DSI reads the shared buffer. */
        OTM8009A_IO_Delay(1U);
    }

    CLEAR_BIT(m_dsi_ptr->Instance->WCR, DSI_WCR_LTDCEN);
    __HAL_DSI_CLEAR_FLAG(m_dsi_ptr, DSI_FLAG_ER);
    s_panel_debug.refresh_count++;
    return HAL_OK;
}

static void display_flush(
		lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {

    (void)area;
    (void)px_map;

    /* DIRECT mode has already drawn into the LTDC framebuffer. Present once
     * all invalid areas are rendered, then release it only after transfer. */
    if (lv_display_flush_is_last(disp)) {
        s_panel_debug.refresh_status = display_refresh();
        if (s_panel_debug.refresh_status != HAL_OK) {
            Error_Handler();
            return;
        }
    }

    lv_display_flush_ready(disp);
}

void LVGL_Init(void)
{
    lv_init();
    lv_tick_set_cb(HAL_GetTick);

    lv_display_t *disp =
        lv_display_create(LCD_W, LCD_H);
    if (disp == NULL) {
        Error_Handler();
        return;
    }

    lv_display_set_color_format(
        disp, LV_COLOR_FORMAT_ARGB8888);

    lv_display_set_buffers(
        disp,
        draw_buf,
        NULL,
        sizeof(draw_buf),
        LV_DISPLAY_RENDER_MODE_DIRECT);

    lv_display_set_flush_cb(disp, display_flush);

    label = lv_label_create(lv_screen_active());
    if (label == NULL) {
        Error_Handler();
        return;
    }

    lv_label_set_text(label, "Szia Agi!");
    lv_obj_center(label);
}


static void sdram_sanity_check(void) {

  sdram_ptr[0] = 0xDEADBEEF;
  sdram_ptr[1] = 0xCAFEBABE;
  sdram_ptr[2] = 0xFEEDFACE;
  sdram_ptr[3] = 0xBAADF00D;

  // read back: 
  uint32_t val0 = sdram_ptr[0];
  uint32_t val1 = sdram_ptr[1];
  uint32_t val2 = sdram_ptr[2];
  uint32_t val3 = sdram_ptr[3];

  if(val0 != 0xDEADBEEF || val1 != 0xCAFEBABE || val2 != 0xFEEDFACE || val3 != 0xBAADF00D) {
    // SDRAM test failed
    Error_Handler();
  }

  const int SDRAM_SIZE = (16UL * 1024 * 1024); 
  const uint32_t words = SDRAM_SIZE / sizeof(uint32_t);

  // Write address-dependent patterns
  for (uint32_t i = 0; i < words; i++)
  {
    sdram_ptr[i] = 0xA5A50000UL ^ i;
  }

  // Verify all addresses
  for (uint32_t i = 0; i < words; i++) {
    if (sdram_ptr[i] != (0xA5A50000UL ^ i)) {
      Error_Handler();
    }
  }

}



static HAL_StatusTypeDef Framebuffer_Config(void) {
  LTDC_LayerCfgTypeDef layer = {0};

  layer.WindowX0 = 0;
  layer.WindowX1 = LCD_W;
  layer.WindowY0 = 0;
  layer.WindowY1 = LCD_H;

  layer.PixelFormat = LTDC_PIXEL_FORMAT_ARGB8888;
  layer.FBStartAdress = (uint32_t)(uintptr_t)draw_buf;

  layer.Alpha = 255;
  layer.Alpha0 = 0;

  layer.BlendingFactor1 =
      LTDC_BLENDING_FACTOR1_PAxCA;
  layer.BlendingFactor2 =
      LTDC_BLENDING_FACTOR2_PAxCA;

  layer.ImageWidth = LCD_W;
  layer.ImageHeight = LCD_H;

  /* In adapted command mode, the wrapper can halt the LTDC clock. The HAL
     reads LTDC registers to calculate the layer window, so release the wrapper
     while programming and reloading the layer. */
  __HAL_DSI_WRAPPER_DISABLE(m_dsi_ptr);

  HAL_StatusTypeDef res = HAL_LTDC_ConfigLayer(m_ltdc_ptr, &layer, 0);
  if (res == HAL_OK)
  {
    const uint32_t reload_start = HAL_GetTick();
    while ((m_ltdc_ptr->Instance->SRCR & LTDC_SRCR_IMR) != 0U)
    {
      if ((HAL_GetTick() - reload_start) >= 100U)
      {
        res = HAL_TIMEOUT;
        break;
      }
    }
  }

  /* Snapshot while the registers are readable; inspect these after refresh. */
  s_panel_debug.ltdc_framebuffer = LTDC_LAYER(m_ltdc_ptr, 0)->CFBAR;
  s_panel_debug.ltdc_pixel_format = LTDC_LAYER(m_ltdc_ptr, 0)->PFCR;
  s_panel_debug.ltdc_line_length = LTDC_LAYER(m_ltdc_ptr, 0)->CFBLR;
  s_panel_debug.ltdc_line_count = LTDC_LAYER(m_ltdc_ptr, 0)->CFBLNR;
  s_panel_debug.ltdc_config_status = res;

  __HAL_DSI_WRAPPER_ENABLE(m_dsi_ptr);

  return res;
}


static void Framebuffer_Fill(uint8_t color_shift) {

  color_shift %= 3; // Limit to 3

  for (uint32_t y = 0; y < LCD_H; y++)
  {
    for (uint32_t x = 0; x < LCD_W; x++)
    {
      uint32_t color;

      if (x < LCD_W / 3) {
        color = (0xFF000000 | (0xFF << (((color_shift + 0) % 3) * 8))); 
      }
      else if (x < 2 * LCD_W / 3) {
        color = (0xFF000000 | (0xFF << (((color_shift + 1) % 3) * 8))); 
      }
      else {
        color = (0xFF000000 | (0xFF << (((color_shift + 2) % 3) * 8))); 
      }

      sdram_ptr[y * LCD_W + x] = color;
    }
  }
}





void display_init(LTDC_HandleTypeDef * f_ltdc_ptr, DSI_HandleTypeDef * f_dsi_ptr) {

  if(f_ltdc_ptr == NULL || f_dsi_ptr == NULL) {
    Error_Handler();
  }

  m_ltdc_ptr = f_ltdc_ptr;
  m_dsi_ptr = f_dsi_ptr;

  /* Own the refresh cycle: automatic refresh could read the single buffer
     while LVGL modifies it. Completion is polled by display_refresh(). */
  CLEAR_BIT(m_dsi_ptr->Instance->WCFGR, DSI_WCFGR_AR);
  __HAL_DSI_DISABLE_IT(m_dsi_ptr, DSI_IT_TE | DSI_IT_ER);

  //sdram_sanity_check();

  /* FMC/SDRAM initialization in main() precedes creation of this task. */
  memset(draw_buf, 0, sizeof(draw_buf));

  /* Use the platform delay callback during startup or from an RTOS task. */
  HAL_GPIO_WritePin(DSI_RESET_GPIO_Port, DSI_RESET_Pin, GPIO_PIN_SET);
  OTM8009A_IO_Delay(10U);

  /* Send the panel's initialization sequence in low-power DCS mode. */
  DSI_LPCmdTypeDef panel_commands = {0};
  panel_commands.LPDcsShortWriteNoP = DSI_LP_DSW0P_ENABLE;
  panel_commands.LPDcsShortWriteOneP = DSI_LP_DSW1P_ENABLE;
  panel_commands.LPDcsLongWrite = DSI_LP_DLW_ENABLE;
  panel_commands.LPDcsShortReadNoP = DSI_LP_DSR0P_ENABLE;
  panel_commands.LPMaxReadPacket = DSI_LP_MRDP_ENABLE;
  if (HAL_DSI_ConfigCommand(m_dsi_ptr, &panel_commands) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_DSI_Start(m_dsi_ptr) != HAL_OK)
  {
    Error_Handler();
  }

  OTM8009A_Init(OTM8009A_FORMAT_RGB888,
              OTM8009A_ORIENTATION_LANDSCAPE);

  const uint32_t lcd_dev_ID = OTM8009A_ReadID(); 
  (void) lcd_dev_ID;

  s_panel_debug.id = lcd_dev_ID;

  uint8_t register_value = 0U;
  s_panel_debug.pixel_format_status =
      DSI_IO_ReadCmd(OTM8009A_CMD_RDDCOLMOD, &register_value, 1U);
  s_panel_debug.pixel_format = register_value;

  register_value = 0U;
  s_panel_debug.memory_access_status =
      DSI_IO_ReadCmd(OTM8009A_CMD_RDDMADCTL, &register_value, 1U);
  s_panel_debug.memory_access = register_value;

  /* Break here to inspect s_panel_debug before starting pixel transfers. */
  __asm("nop"); 

  /* Restore high-speed writes while keeping reads in low-power mode. */
  panel_commands.LPDcsShortWriteNoP = DSI_LP_DSW0P_DISABLE;
  panel_commands.LPDcsShortWriteOneP = DSI_LP_DSW1P_DISABLE;
  panel_commands.LPDcsLongWrite = DSI_LP_DLW_DISABLE;
  if (HAL_DSI_ConfigCommand(m_dsi_ptr, &panel_commands) != HAL_OK)
  {
    Error_Handler();
  }

  if (Framebuffer_Config() != HAL_OK)
  {
    Error_Handler();
  }


  LVGL_Init();

  lv_timer_handler();


  return;
}


void display_cyclic_exec_funciton(uint8_t data) {

  static uint16_t dot_shift = 0U;
  static uint8_t count_up_flag = 0; 
  static uint16_t step_size = 5;

  if(m_ltdc_ptr == NULL || m_dsi_ptr == NULL) {
    Error_Handler();
  }

  if(label == NULL) {
    Error_Handler();
  }


  /*if (count_up_flag == 0) {
    dot_shift += step_size;
    if (dot_shift >= LCD_W - 20) {
      count_up_flag = 1;
    }
  } else {
    if (dot_shift <= step_size) {
      count_up_flag = 0;
    }
    dot_shift -= step_size;
  }

  move_dot(sdram_ptr, dot_shift, 0, 0xFFFFFFFF);
  HAL_DSI_Refresh(m_dsi_ptr);*/
 

  static uint16_t test_cnt = 0; 
  char buffer[64];
  sprintf(buffer, "Szia Agi! %u %u", test_cnt++, data);

  lv_label_set_text(label, buffer);

  lv_timer_handler();

  return;
} 

