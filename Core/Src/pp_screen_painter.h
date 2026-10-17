/*
 * pp_screen_painter.h
 *
 *  Created on: Oct 16, 2026
 *      Author: daniel_papa
 */

#ifndef SRC_PP_SCREEN_PAINTER_H_
#define SRC_PP_SCREEN_PAINTER_H_


#include <stdint.h>

void paint_all_black(uint32_t * const sdram_ptr);
void paint_dot(uint32_t * const sdram_ptr, uint16_t x_pos, uint16_t y_pos);

void put_dot(uint32_t * const sdram_ptr, uint16_t x_pos, uint16_t y_pos, uint16_t diam, uint32_t color);
void move_dot(uint32_t * const sdram_ptr, uint16_t new_x, uint16_t new_y, uint32_t color);

/* Draw one printable ASCII character into the 800x480 ARGB8888 framebuffer.
   Coordinates specify the top-left corner of its 8x8 cell. The background is
   preserved; unsupported characters draw '?'. These functions only paint the
   framebuffer: the display controller must request a refresh afterwards. */
void put_char(volatile uint32_t * const sdram_ptr, uint16_t x_pos,
              uint16_t y_pos, char c, uint32_t color);

/* Integer scaling: 1 = 8x8, 2 = 16x16, 4 = 32x32. Scale 0 draws nothing.
   Drawings are clipped at the right and bottom edges of the framebuffer. */
void put_char_scaled(volatile uint32_t * const sdram_ptr, uint16_t x_pos,
                     uint16_t y_pos, char c, uint32_t color, uint8_t scale);

#endif /* SRC_PP_SCREEN_PAINTER_H_ */
