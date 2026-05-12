
#define F_CPU 11059200UL

#include <avr/io.h>
#include <util/delay.h>
#include "lcd.h"
#define LCD_EN   PD7

#define LCD_RS   PC2
#define LCD_RW   PC3

#define LCD_D4   PC4
#define LCD_D5   PC5
#define LCD_D6   PC6
#define LCD_D7   PC7

// Internal helper function to pulse the Enable pin
static void LCD_pulse_enable(void)
{
    PORTD |= (1 << LCD_EN);
    _delay_us(1);
    PORTD &= ~(1 << LCD_EN);
    _delay_us(100);
}
static void LCD_write4(uint8_t nibble)
{
    // clear data bits first (PC4-PC7)
    PORTC &= 0x0F;

    if (nibble & 1) PORTC |= (1 << LCD_D4);
    if (nibble & 2) PORTC |= (1 << LCD_D5);
    if (nibble & 4) PORTC |= (1 << LCD_D6);
    if (nibble & 8) PORTC |= (1 << LCD_D7);

    LCD_pulse_enable();
}

void LCD_Command(uint8_t cmd)
{
    PORTC &= ~(1 << LCD_RS); // RS = 0
    PORTC &= ~(1 << LCD_RW); // RW = 0 (write)

    LCD_write4(cmd >> 4);
    LCD_write4(cmd & 0x0F);

    _delay_ms(2);
}

void LCD_Char(char data)
{
    PORTC |= (1 << LCD_RS);  // RS = 1
    PORTC &= ~(1 << LCD_RW); // RW = 0

    LCD_write4(data >> 4);
    LCD_write4(data & 0x0F);

    _delay_us(50);
}
void LCD_Init(void)
{
    // Set directions
    DDRD |= (1 << LCD_EN);

    DDRC |= (1 << LCD_RS) | (1 << LCD_RW);
    DDRC |= 0xF0; // PC4-PC7 output (data lines)

    _delay_ms(20);

    // Init sequence (4-bit mode)
    LCD_Command(0x02); // 4-bit mode
    LCD_Command(0x28); // 2 lines, 5x7 font
    LCD_Command(0x0C); // display ON, cursor OFF
    LCD_Command(0x06); // entry mode
    LCD_Clear();
}

void LCD_String(const char *str)
{
    while (*str)
    {
        LCD_Char(*str++);
    }
}

void LCD_Clear(void)
{
    LCD_Command(0x01);
    _delay_ms(2);
}

void LCD_SetCursor(uint8_t row, uint8_t col) {
    uint8_t address;
    if (row == 0) {
        address = 0x80 + col; // Row 0 starts at 0x80
    } else {
        address = 0xC0 + col; // Row 1 starts at 0xC0
    }
    LCD_Command(address);
}
