#include "uart.h"
#include <avr/io.h>
#include <stdlib.h>

// Define your clock speed (16 MHz) for the baud rate calculation
#ifndef F_CPU
#define F_CPU 11059200UL
#endif

void UART_Init(uint32_t baud) {
    // Calculate the Baud Rate Register value
    uint16_t ubrr = (F_CPU / 16 / baud) - 1;
    
    // Set baud rate registers (High and Low)
    UBRRH = (unsigned char)(ubrr >> 8);
    UBRRL = (unsigned char)ubrr;
    
    // Enable receiver and transmitter
    UCSRB = (1 << RXEN) | (1 << TXEN)|(1 << RXCIE); 
    
    // Set frame format: 8 data bits, 1 stop bit, no parity
    // Note: The URSEL bit must be 1 when writing to UCSRC on the ATmega32A
    UCSRC = (1 << URSEL) | (1 << UCSZ1) | (1 << UCSZ0);
}

void UART_TxChar(char data) {
    // Wait for the empty transmit buffer flag (UDRE)
    while (!(UCSRA & (1 << UDRE)));
    
    // Put data into the buffer to send it
    UDR = data;
}

void UART_TxString(const char* str) {
    // Loop through the string until the null terminator is hit
    while (*str) {
        UART_TxChar(*str++);
    }
}

void UART_TxNum(int32_t num) {
    // Convert the integer to an ASCII string using standard library (base 10)
    // 12 bytes is enough for a 32-bit int including the negative sign and null terminator
    char buffer[12];
    ltoa(num, buffer, 10);
    UART_TxString(buffer);
}
void UART_sendByte(uint8_t data)
{
    while (!(UCSRA & (1 << UDRE)));
    UDR = data;
}
