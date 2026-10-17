/*
 * pp_display_control.c
 *
 *  Created on: Oct 15, 2026
 *      Author: daniel_papa
 */

#include "pp_display_control.h"
#include "main.h"
#include "otm8009a.h"
#include <stdint.h>
#include "stm32f7xx_hal.h"

#define SDRAM_BASE_ADDR 0xC0000000UL
static volatile uint32_t * const sdram_ptr = (volatile uint32_t *)SDRAM_BASE_ADDR;

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


void display_init(void) {

  sdram_sanity_check();

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
  if (HAL_DSI_ConfigCommand(&hdsi, &panel_commands) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_DSI_Start(&hdsi) != HAL_OK)
  {
    Error_Handler();
  }

  OTM8009A_Init(OTM8009A_FORMAT_RGB888,
              OTM8009A_ORIENTATION_LANDSCAPE);

  const uint32_t lcd_dev_ID = OTM8009A_ReadID(); 

  __asm("nop"); 

  /* Restore high-speed writes while keeping reads in low-power mode. */
  panel_commands.LPDcsShortWriteNoP = DSI_LP_DSW0P_DISABLE;
  panel_commands.LPDcsShortWriteOneP = DSI_LP_DSW1P_DISABLE;
  panel_commands.LPDcsLongWrite = DSI_LP_DLW_DISABLE;
  if (HAL_DSI_ConfigCommand(&hdsi, &panel_commands) != HAL_OK)
  {
    Error_Handler();
  }

  if (Framebuffer_Config() != HAL_OK)
  {
    Error_Handler();
  }


  // Send the completed color bars once; TE synchronization is not configured yet. 
  if (HAL_DSI_Refresh(&hdsi) != HAL_OK)
  {
    Error_Handler();
  }
  
  __asm("nop"); 


  return;
}


void display_cyclic_exec_funciton(void) {
  return;
} 

