#ifndef __LCD_SPI2_DRV_H__
#define __LCD_SPI2_DRV_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define LCD_PWR(n)     ((n) ? \
                        HAL_GPIO_WritePin(LCD_PWR_GPIO_Port, LCD_PWR_Pin, GPIO_PIN_SET) : \
                        HAL_GPIO_WritePin(LCD_PWR_GPIO_Port, LCD_PWR_Pin, GPIO_PIN_RESET))
#define LCD_WR_RS(n)   ((n) ? \
                        HAL_GPIO_WritePin(LCD_WR_RS_GPIO_Port, LCD_WR_RS_Pin, GPIO_PIN_SET) : \
                        HAL_GPIO_WritePin(LCD_WR_RS_GPIO_Port, LCD_WR_RS_Pin, GPIO_PIN_RESET))
#define LCD_RST(n)     ((n) ? \
                        HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET) : \
                        HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET))

#define LCD_Width   240
#define LCD_Height  240

#define WHITE   0xFFFF
#define YELLOW  0xFFE0
#define BRRED   0xFC07
#define PINK    0xF81F
#define RED     0xF800
#define BROWN   0xBC40
#define GRAY    0x8430
#define GBLUE   0x07FF
#define GREEN   0x07E0
#define BLUE    0x001F
#define BLACK   0x0000

void LCD_Init(void);
void LCD_Clear(uint16_t color);
void LCD_DisplayOn(void);
void LCD_DisplayOff(void);
void LCD_Address_Set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);
void LCD_Fill(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t color);
void LCD_DrawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);
void LCD_FillCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);
void LCD_ShowChar(uint16_t x, uint16_t y, char chr, uint16_t color, uint16_t back_color);
void LCD_ShowGBKChar(uint16_t x, uint16_t y, const uint8_t *gbk, uint16_t color, uint16_t back_color);
void LCD_ShowMixedString(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                         const char *str, uint16_t color, uint16_t back_color);
void LCD_ShowProjectContributor(uint16_t x, uint16_t y, uint16_t color, uint16_t back_color);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_SPI2_DRV_H__ */
