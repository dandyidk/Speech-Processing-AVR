#ifndef LCD_H
#define LCD_H

#include <stdint.h>

// We assume Data Pins D4, D5, D6, D7 are connected to PC4, PC5, PC6, PC7

// Initializes the LCD in 4-bit mode
void LCD_Init(void);

// Sends a command (like clear screen or move cursor)
void LCD_Command(uint8_t cmd);

// Sends a single ASCII character to be displayed
void LCD_Char(char data);

// Sends a full string of text
void LCD_String(const char* str);

// Clears the entire display
void LCD_Clear(void);

// Moves the cursor. Row is 0 or 1. Col is 0 to 15.
void LCD_SetCursor(uint8_t row, uint8_t col);

#endif // LCD_H