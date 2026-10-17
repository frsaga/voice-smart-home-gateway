#include "lcd_spi2_drv.h"
#include "gpio.h"
#include "spi.h"
#include "font.h"
#include "HzLib.h"

#define LCD_TOTAL_BUF_SIZE  (240 * 240 * 2)
#define LCD_Buf_Size        1152

static uint8_t lcd_buf[LCD_Buf_Size];

static const uint32_t name_font_32x32[3][32] =
{
  {
    0x00000000U, 0x00000000U, 0x00000000U, 0x00003800U,
    0x1FF83000U, 0x1FF07000U, 0x18706000U, 0x186FFFF8U,
    0x186FFFF8U, 0x18E8C008U, 0x18C0C000U, 0x18C18E00U,
    0x19C18C00U, 0x19838C00U, 0x19C30C00U, 0x18C70C00U,
    0x18E7FFF8U, 0x1877FFF8U, 0x18740C00U, 0x18300C00U,
    0x18708C40U, 0x1BF1CCE0U, 0x19E38C70U, 0x19C38C30U,
    0x18070C38U, 0x180E0C1CU, 0x180C0C18U, 0x18047C00U,
    0x18007C00U, 0x00003000U, 0x00000000U, 0x00000000U
  },
  {
    0x00000000U, 0x00000000U, 0x060FFFE0U, 0x0F0FFFC0U,
    0x078301C0U, 0x01838380U, 0x0081C700U, 0x0000CE00U,
    0x0000FC00U, 0x18007800U, 0x3E01FF00U, 0x0F0FC7FCU,
    0x033F01F8U, 0x001C3810U, 0x00001800U, 0x00001800U,
    0x010FFFC0U, 0x038FFFC0U, 0x03001800U, 0x03001800U,
    0x07001800U, 0x063FFFF8U, 0x063FFFF8U, 0x0E001800U,
    0x0C001800U, 0x1C001800U, 0x0C001800U, 0x00003800U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U
  },
  {
    0x00000000U, 0x00000000U, 0x00010000U, 0x00070000U,
    0x00038000U, 0x0001C000U, 0x00010000U, 0x1FFFFFF8U,
    0x1FFFFFF0U, 0x1C000030U, 0x1C000030U, 0x00000000U,
    0x01FFFF80U, 0x01FFFF80U, 0x00018000U, 0x00018000U,
    0x00018000U, 0x00018000U, 0x1FFFFFF8U, 0x1FFFFFF8U,
    0x00018000U, 0x00018000U, 0x00018000U, 0x00018000U,
    0x00018000U, 0x00018000U, 0x00018000U, 0x001F8000U,
    0x000F8000U, 0x000F0000U, 0x00000000U, 0x00000000U
  }
};

static void LCD_GPIO_Init(void)
{
  MX_GPIO_Init();

  LCD_PWR(0);
  LCD_RST(0);
  HAL_Delay(100);
  LCD_RST(1);

  MX_SPI2_Init();
}

static void LCD_Write_Cmd(uint8_t cmd)
{
  LCD_WR_RS(0);
  SPI_WriteByte(&cmd, 1);
}

static void LCD_Write_Data(uint8_t dat)
{
  LCD_WR_RS(1);
  SPI_WriteByte(&dat, 1);
}

static void LCD_Write_Color(uint16_t color)
{
  uint8_t data[2];

  data[0] = color >> 8;
  data[1] = color;
  SPI_WriteByte(data, 2);
}

void LCD_DisplayOn(void)
{
  LCD_PWR(1);
}

void LCD_DisplayOff(void)
{
  LCD_PWR(0);
}

void LCD_Address_Set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
  LCD_Write_Cmd(0x2A);
  LCD_Write_Data(x1 >> 8);
  LCD_Write_Data(x1);
  LCD_Write_Data(x2 >> 8);
  LCD_Write_Data(x2);

  LCD_Write_Cmd(0x2B);
  LCD_Write_Data(y1 >> 8);
  LCD_Write_Data(y1);
  LCD_Write_Data(y2 >> 8);
  LCD_Write_Data(y2);

  LCD_Write_Cmd(0x2C);
}

void LCD_Clear(uint16_t color)
{
  uint16_t i;
  uint16_t j;
  uint8_t data[2];

  data[0] = color >> 8;
  data[1] = color;

  for (j = 0; j < LCD_Buf_Size / 2; j++)
  {
    lcd_buf[j * 2] = data[0];
    lcd_buf[j * 2 + 1] = data[1];
  }

  LCD_Address_Set(0, 0, LCD_Width - 1, LCD_Height - 1);
  LCD_WR_RS(1);

  for (i = 0; i < (LCD_TOTAL_BUF_SIZE / LCD_Buf_Size); i++)
  {
    SPI_WriteByte(lcd_buf, (uint16_t)LCD_Buf_Size);
  }
}

void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
{
  if ((x >= LCD_Width) || (y >= LCD_Height))
  {
    return;
  }

  LCD_Address_Set(x, y, x, y);
  LCD_WR_RS(1);
  LCD_Write_Color(color);
}

void LCD_Fill(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t color)
{
  uint16_t temp;
  uint32_t point_count;
  uint32_t i;

  if (x_start > x_end)
  {
    temp = x_start;
    x_start = x_end;
    x_end = temp;
  }

  if (y_start > y_end)
  {
    temp = y_start;
    y_start = y_end;
    y_end = temp;
  }

  if ((x_start >= LCD_Width) || (y_start >= LCD_Height))
  {
    return;
  }

  if (x_end >= LCD_Width)
  {
    x_end = LCD_Width - 1;
  }

  if (y_end >= LCD_Height)
  {
    y_end = LCD_Height - 1;
  }

  point_count = ((uint32_t)x_end - x_start + 1U) * ((uint32_t)y_end - y_start + 1U);

  LCD_Address_Set(x_start, y_start, x_end, y_end);
  LCD_WR_RS(1);

  for (i = 0; i < point_count; i++)
  {
    LCD_Write_Color(color);
  }
}

static void LCD_DrawCirclePoint(uint16_t x0, uint16_t y0, int16_t x, int16_t y, uint16_t color)
{
  int16_t center_x = (int16_t)x0;
  int16_t center_y = (int16_t)y0;

  LCD_DrawPoint((uint16_t)(center_x + x), (uint16_t)(center_y + y), color);
  LCD_DrawPoint((uint16_t)(center_x - x), (uint16_t)(center_y + y), color);
  LCD_DrawPoint((uint16_t)(center_x + x), (uint16_t)(center_y - y), color);
  LCD_DrawPoint((uint16_t)(center_x - x), (uint16_t)(center_y - y), color);
  LCD_DrawPoint((uint16_t)(center_x + y), (uint16_t)(center_y + x), color);
  LCD_DrawPoint((uint16_t)(center_x - y), (uint16_t)(center_y + x), color);
  LCD_DrawPoint((uint16_t)(center_x + y), (uint16_t)(center_y - x), color);
  LCD_DrawPoint((uint16_t)(center_x - y), (uint16_t)(center_y - x), color);
}

void LCD_DrawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
  int16_t x = 0;
  int16_t y = (int16_t)r;
  int16_t d = 3 - (2 * (int16_t)r);

  while (x <= y)
  {
    LCD_DrawCirclePoint(x0, y0, x, y, color);

    if (d < 0)
    {
      d += (4 * x) + 6;
    }
    else
    {
      d += 4 * (x - y) + 10;
      y--;
    }
    x++;
  }
}

static void LCD_DrawHLineClipped(int16_t x_start, int16_t x_end, int16_t y, uint16_t color)
{
  int16_t temp;

  if ((y < 0) || (y >= (int16_t)LCD_Height))
  {
    return;
  }

  if (x_start > x_end)
  {
    temp = x_start;
    x_start = x_end;
    x_end = temp;
  }

  if ((x_end < 0) || (x_start >= (int16_t)LCD_Width))
  {
    return;
  }

  if (x_start < 0)
  {
    x_start = 0;
  }

  if (x_end >= (int16_t)LCD_Width)
  {
    x_end = (int16_t)LCD_Width - 1;
  }

  LCD_Fill((uint16_t)x_start, (uint16_t)y, (uint16_t)x_end, (uint16_t)y, color);
}

void LCD_FillCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
  int16_t x = 0;
  int16_t y = (int16_t)r;
  int16_t d = 3 - (2 * (int16_t)r);

  while (x <= y)
  {
    LCD_DrawHLineClipped((int16_t)x0 - x, (int16_t)x0 + x, (int16_t)y0 + y, color);
    LCD_DrawHLineClipped((int16_t)x0 - x, (int16_t)x0 + x, (int16_t)y0 - y, color);
    LCD_DrawHLineClipped((int16_t)x0 - y, (int16_t)x0 + y, (int16_t)y0 + x, color);
    LCD_DrawHLineClipped((int16_t)x0 - y, (int16_t)x0 + y, (int16_t)y0 - x, color);

    if (d < 0)
    {
      d += (4 * x) + 6;
    }
    else
    {
      d += 4 * (x - y) + 10;
      y--;
    }
    x++;
  }
}

void LCD_ShowChar(uint16_t x, uint16_t y, char chr, uint16_t color, uint16_t back_color)
{
  uint8_t row;
  uint8_t bit;
  uint8_t data;
  uint8_t index;

  if ((chr < ' ') || (chr > '~') || (x + 7U >= LCD_Width) || (y + 15U >= LCD_Height))
  {
    return;
  }

  index = (uint8_t)(chr - ' ');

  LCD_Address_Set(x, y, x + 7U, y + 15U);
  LCD_WR_RS(1);

  for (row = 0; row < 16U; row++)
  {
    data = asc2_1608[index][row];
    for (bit = 0; bit < 8U; bit++)
    {
      if ((data & 0x80U) != 0U)
      {
        LCD_Write_Color(color);
      }
      else
      {
        LCD_Write_Color(back_color);
      }
      data <<= 1;
    }
  }
}

void LCD_ShowGBKChar(uint16_t x, uint16_t y, const uint8_t *gbk, uint16_t color, uint16_t back_color)
{
  uint8_t row;
  uint8_t col;
  uint8_t buffer[32];
  uint16_t row_bits;

  if ((gbk == NULL) || (x + 15U >= LCD_Width) || (y + 15U >= LCD_Height))
  {
    return;
  }

  GetGBKCode(buffer, (unsigned char *)gbk);

  LCD_Address_Set(x, y, x + 15U, y + 15U);
  LCD_WR_RS(1);

  for (row = 0; row < 16U; row++)
  {
    row_bits = ((uint16_t)buffer[row * 2U] << 8) | buffer[row * 2U + 1U];
    for (col = 0; col < 16U; col++)
    {
      if ((row_bits & (0x8000U >> col)) != 0U)
      {
        LCD_Write_Color(color);
      }
      else
      {
        LCD_Write_Color(back_color);
      }
    }
  }
}

void LCD_ShowMixedString(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                         const char *str, uint16_t color, uint16_t back_color)
{
  uint16_t cursor_x = x;
  uint16_t cursor_y = y;
  uint16_t x_end = x + width;
  uint16_t y_end = y + height;
  const uint8_t *p = (const uint8_t *)str;

  if ((str == NULL) || (width == 0U) || (height == 0U))
  {
    return;
  }

  while ((*p != '\0') && (cursor_y + 15U < y_end) && (cursor_y + 15U < LCD_Height))
  {
    if (*p == '\r')
    {
      p++;
      continue;
    }

    if (*p == '\n')
    {
      cursor_x = x;
      cursor_y += 16U;
      p++;
      continue;
    }

    if ((*p >= ' ') && (*p <= '~'))
    {
      if (cursor_x + 7U >= x_end)
      {
        cursor_x = x;
        cursor_y += 16U;
      }

      if ((cursor_y + 15U >= y_end) || (cursor_y + 15U >= LCD_Height))
      {
        break;
      }

      LCD_ShowChar(cursor_x, cursor_y, (char)*p, color, back_color);
      cursor_x += 8U;
      p++;
    }
    else if ((p[0] >= 0xB0U) && (p[0] <= 0xF7U) && (p[1] >= 0xA1U) && (p[1] <= 0xFEU))
    {
      if (cursor_x + 15U >= x_end)
      {
        cursor_x = x;
        cursor_y += 16U;
      }

      if ((cursor_y + 15U >= y_end) || (cursor_y + 15U >= LCD_Height))
      {
        break;
      }

      LCD_ShowGBKChar(cursor_x, cursor_y, p, color, back_color);
      cursor_x += 16U;
      p += 2U;
    }
    else
    {
      if (cursor_x + 7U >= x_end)
      {
        cursor_x = x;
        cursor_y += 16U;
      }

      if ((cursor_y + 15U >= y_end) || (cursor_y + 15U >= LCD_Height))
      {
        break;
      }

      LCD_ShowChar(cursor_x, cursor_y, '?', color, back_color);
      cursor_x += 8U;
      p++;
    }
  }
}

static void LCD_DrawFont32(uint16_t x, uint16_t y, const uint32_t *font,
                           uint16_t color, uint16_t back_color)
{
  uint8_t row;
  uint8_t col;
  uint32_t mask;

  LCD_Address_Set(x, y, x + 31, y + 31);
  LCD_WR_RS(1);

  for (row = 0; row < 32; row++)
  {
    mask = font[row];
    for (col = 0; col < 32; col++)
    {
      if ((mask & (0x80000000U >> col)) != 0U)
      {
        LCD_Write_Color(color);
      }
      else
      {
        LCD_Write_Color(back_color);
      }
    }
  }
}

void LCD_ShowProjectContributor(uint16_t x, uint16_t y, uint16_t color, uint16_t back_color)
{
  LCD_DrawFont32(x, y, name_font_32x32[0], color, back_color);
  LCD_DrawFont32(x + 40, y, name_font_32x32[1], color, back_color);
  LCD_DrawFont32(x + 80, y, name_font_32x32[2], color, back_color);
}

void LCD_Init(void)
{
  LCD_GPIO_Init();
  HAL_Delay(120);

  LCD_Write_Cmd(0x11);
  HAL_Delay(120);

  LCD_Write_Cmd(0x36);
  LCD_Write_Data(0x00);

  LCD_Write_Cmd(0x3A);
  LCD_Write_Data(0x65);

  LCD_Write_Cmd(0xB2);
  LCD_Write_Data(0x0C);
  LCD_Write_Data(0x0C);
  LCD_Write_Data(0x00);
  LCD_Write_Data(0x33);
  LCD_Write_Data(0x33);

  LCD_Write_Cmd(0xB7);
  LCD_Write_Data(0x72);

  LCD_Write_Cmd(0xBB);
  LCD_Write_Data(0x3D);

  LCD_Write_Cmd(0xC0);
  LCD_Write_Data(0x2C);

  LCD_Write_Cmd(0xC2);
  LCD_Write_Data(0x01);

  LCD_Write_Cmd(0xC3);
  LCD_Write_Data(0x19);

  LCD_Write_Cmd(0xC4);
  LCD_Write_Data(0x20);

  LCD_Write_Cmd(0xC6);
  LCD_Write_Data(0x0F);

  LCD_Write_Cmd(0xD0);
  LCD_Write_Data(0xA4);
  LCD_Write_Data(0xA1);

  LCD_Write_Cmd(0xE0);
  LCD_Write_Data(0xD0);
  LCD_Write_Data(0x04);
  LCD_Write_Data(0x0D);
  LCD_Write_Data(0x11);
  LCD_Write_Data(0x13);
  LCD_Write_Data(0x2B);
  LCD_Write_Data(0x3F);
  LCD_Write_Data(0x54);
  LCD_Write_Data(0x4C);
  LCD_Write_Data(0x18);
  LCD_Write_Data(0x0D);
  LCD_Write_Data(0x0B);
  LCD_Write_Data(0x1F);
  LCD_Write_Data(0x23);

  LCD_Write_Cmd(0xE1);
  LCD_Write_Data(0xD0);
  LCD_Write_Data(0x04);
  LCD_Write_Data(0x0C);
  LCD_Write_Data(0x11);
  LCD_Write_Data(0x13);
  LCD_Write_Data(0x2C);
  LCD_Write_Data(0x3F);
  LCD_Write_Data(0x44);
  LCD_Write_Data(0x51);
  LCD_Write_Data(0x2F);
  LCD_Write_Data(0x1F);
  LCD_Write_Data(0x1F);
  LCD_Write_Data(0x20);
  LCD_Write_Data(0x23);

  LCD_Write_Cmd(0x21);
  LCD_Write_Cmd(0x29);

  LCD_Clear(WHITE);
  LCD_PWR(1);
}
