#ifndef UART_H
#define UART_H

#include <stdint.h>

// Initialize the UART hardware with a specific baud rate
void UART_Init(uint32_t baud);

// Transmit a single character
void UART_TxChar(char data);

// Transmit a standard null-terminated string
void UART_TxString(const char* str);

// Transmit an integer number (crucial for debugging your math later)
void UART_TxNum(int32_t num);
void UART_sendByte(uint8_t data);
#endif // UART_H