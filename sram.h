#ifndef SRAM_H
#define SRAM_H


#include <stdint.h>
#define SRAM_DTW_PREV_BASE  0x0000
#define SRAM_DTW_CURR_BASE  0x0400
#define PREV_ADDR(i) (SRAM_DTW_PREV_BASE + ((i) << 2))
#define CURR_ADDR(i) (SRAM_DTW_CURR_BASE + ((i) << 2))
// init bus
void SRAM_init(void);

// operations
void SRAM_write(uint16_t addr, uint8_t data);
uint8_t SRAM_read(uint16_t addr);

// debug versions (step-by-step)
void SRAM_debug_write(uint16_t addr, uint8_t data);
uint8_t SRAM_debug_read(uint16_t addr);
void SRAM_write32(uint16_t addr, uint32_t val);
uint32_t SRAM_read32(uint16_t addr);
#endif