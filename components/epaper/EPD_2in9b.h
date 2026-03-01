/*
 * 2.9" e-Paper Module (B) - Tricolor (red/black/white) - SSD1680
 * 128 x 296 pixels
 */
#ifndef __EPD_2IN9B_H_
#define __EPD_2IN9B_H_

#define EPD_2IN9B_WIDTH  128
#define EPD_2IN9B_HEIGHT 296

void EPD_2IN9B_Init(void);
void EPD_2IN9B_Clear(void);
void EPD_2IN9B_Display(const uint8_t *blackimage, const uint8_t *ryimage);
void EPD_2IN9B_Sleep(void);

#endif
