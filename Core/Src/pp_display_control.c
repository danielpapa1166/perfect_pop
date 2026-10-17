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
#include "stm32f7xx_hal.h"

#define LCD_W    800U
#define LCD_H    480U


#define SDRAM_BASE_ADDR 0xC0000000UL
static volatile uint32_t * const sdram_ptr = (volatile uint32_t *)SDRAM_BASE_ADDR;

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
} s_panel_debug;


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
  layer.FBStartAdress = SDRAM_BASE_ADDR;

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

  //sdram_sanity_check();

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

  /* The SDRAM test overwrites the framebuffer; replace it before scanout. */
  //Framebuffer_Fill(0);
  paint_all_black(sdram_ptr); 


  const uint16_t text_x_offset = 360;
  const uint16_t text_y_offset = 210;

  const char display_text[] = "Szia Agi!";
  const uint32_t text_color[] = { 
    0xFFFFFF00, 
    0xFFFF00FF,
    0xFF00FFFF,
    0xFFFF0000, 
    0xFF0000FF,
    0xFF00FF00,
    0xFFFFFF00,
    0xFFFF00FF,
    0xFF00FFFF
  };

  const uint8_t scaling_factor = 5; 

  for (uint8_t i = 0; i < sizeof(display_text) - 1; i++) {
    put_char_scaled(sdram_ptr, 
      text_x_offset + i * scaling_factor * 8, 
      text_y_offset, 
      display_text[i], 
      text_color[i], 
      scaling_factor);
  }

  put_dot(sdram_ptr, 
    text_x_offset + 6 * scaling_factor * 8 - scaling_factor * 2, 
    text_y_offset - scaling_factor * 1, 
    scaling_factor * 1, 
    0xFF00FF00);

  put_dot(sdram_ptr, 
    text_x_offset + 6 * scaling_factor * 8 - scaling_factor * 2, 
    text_y_offset - scaling_factor * 2, 
    scaling_factor * 1, 
    0xFF00FF00);


  if (Framebuffer_Config() != HAL_OK)
  {
    Error_Handler();
  }


  // Send the completed color bars once; TE synchronization is not configured yet. 
  if (HAL_DSI_Refresh(m_dsi_ptr) != HAL_OK)
  {
    Error_Handler();
  }
  
  __asm("nop"); 


  return;
}


void display_cyclic_exec_funciton(void) {

  static uint16_t dot_shift = 0U;
  static uint8_t count_up_flag = 0; 
  static uint16_t step_size = 5;

  if(m_ltdc_ptr == NULL || m_dsi_ptr == NULL) {
    Error_Handler();
  }


  if (count_up_flag == 0) {
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
  HAL_DSI_Refresh(m_dsi_ptr);

  return;
} 

