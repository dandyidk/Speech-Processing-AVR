

#define F_CPU 11059200UL

#include <avr/io.h>
#include <util/delay.h>
#include "sram.h"
#include "uart.h"

// ================= PIN DEFINITIONS =================
#define LATCH1   PB1
#define LATCH2   PB2
#define OE_PIN   PB3
#define WE_PIN   PB4

// ================= INTERNAL BUS CONTROL =================

static void bus_output(void)
{
    DDRA = 0xFE;
    DDRB |= (1 << PB0);
}

static void bus_input(void)
{
    DDRA = 0x00;
    DDRB &= ~(1 << PB0);
}

static void bus_write(uint8_t data)
{
    PORTA = (PORTA & 0x01) | (data << 1); //take fromm 1 to 7 bits

    if (data & 0x01) PORTB |= (1 << PB0);
    else PORTB &= ~(1 << PB0);
}

static uint8_t bus_read(void)
{
    uint8_t data = 0;

    if (PINB & (1 << PB0)) data |= 0x01;
    data |= ((PINA >> 1) << 1);

    return data;
}

// ================= INIT =================

void SRAM_init(void)
{
    DDRB |= (1 << LATCH1) | (1 << LATCH2) |
            (1 << OE_PIN) | (1 << WE_PIN);

    PORTB |= (1 << OE_PIN) | (1 << WE_PIN);

    bus_input();
}

// ================= BASIC OPERATIONS =================

void SRAM_write(uint16_t addr, uint8_t data)
{
    bus_output();

    // address low
    bus_write(addr & 0xFF);
    PORTB |= (1 << LATCH1);
    _delay_us(1);
    PORTB &= ~(1 << LATCH1);

    // address high
    bus_write(addr >> 8);
    PORTB |= (1 << LATCH2);
    _delay_us(1);
    PORTB &= ~(1 << LATCH2);

    // data
    bus_write(data);

    // write pulse
    PORTB &= ~(1 << WE_PIN);
    _delay_us(2);
    PORTB |= (1 << WE_PIN);
}

uint8_t SRAM_read(uint16_t addr)
{
    uint8_t data;

    bus_output();

    // address low
    bus_write(addr & 0xFF);
    PORTB |= (1 << LATCH1);
    _delay_us(1);
    PORTB &= ~(1 << LATCH1);

    // address high
    bus_write(addr >> 8);
    PORTB |= (1 << LATCH2);
    _delay_us(1);
    PORTB &= ~(1 << LATCH2);

    // switch to input
    bus_input();

    PORTB &= ~(1 << OE_PIN);
    _delay_us(2);

    data = bus_read();

    PORTB |= (1 << OE_PIN);

    return data;
}
void SRAM_write32(uint16_t addr, uint32_t val)
{
    SRAM_write(addr + 0, (uint8_t)(val >> 0));
    SRAM_write(addr + 1, (uint8_t)(val >> 8));
    SRAM_write(addr + 2, (uint8_t)(val >> 16));
    SRAM_write(addr + 3, (uint8_t)(val >> 24));
}

uint32_t SRAM_read32(uint16_t addr)
{
    uint32_t b0 = SRAM_read(addr + 0);
    uint32_t b1 = SRAM_read(addr + 1);
    uint32_t b2 = SRAM_read(addr + 2);
    uint32_t b3 = SRAM_read(addr + 3);

    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}