#include "dtw.h"
#include "dsp.h"             
#include "dtw_templates.h"   // This is the ONLY file allowed to include this!
#include "uart.h"            
#include <avr/pgmspace.h>
#include <stdlib.h>          
#include "sram.h"
void DEBUG_PrintRAMInfo(void) {
    // Measure free stack space by scanning for 0xAA fill pattern.
    // Add this to the TOP of main(), before sei():
    //
    //   extern uint8_t _end;       // end of BSS (start of heap)
    //   extern uint8_t __stack;    // top of stack
    //   uint8_t *p = &_end;
    //   while (p < (uint8_t*)SP) *p++ = 0xAA;
    //
    // Then call this function after a few seconds of running:
 
    extern uint8_t _end;
    uint8_t *p = &_end;
    uint16_t free_count = 0;
    while (*p == 0xAA && p < (uint8_t*)0x085F) {
        free_count++;
        p++;
    }
    UART_TxString("FREE_RAM=");
    UART_TxNum(free_count);
    UART_TxString("\r\n");
}
static uint32_t DTW_Distance(uint8_t live_len, uint8_t tpl_idx) {
    uint8_t tpl_len = pgm_read_byte(&template_lengths[tpl_idx]);
    const uint8_t* tpl_ptr = (const uint8_t*)pgm_read_word(&template_ptrs[tpl_idx]);
    const uint32_t INF = 0x3FFFFFFF;

    int8_t w = DTW_BAND_W;
    if (abs((int)live_len - (int)tpl_len) > w)
        w = abs((int)live_len - (int)tpl_len);

    // Init prev to INF in external SRAM
    for (uint8_t j = 0; j < tpl_len; j++) {
        SRAM_write32(PREV_ADDR(j), INF);
    }

    // First row
    for (uint8_t j = 0; j < tpl_len; j++) {
        if (abs(0 - (int)j) > w) continue;
        uint16_t cost = 0;
        for (uint8_t f = 0; f < N_FEATURES; f++) {
            uint8_t live_val = live_features[0][f];
            uint8_t tpl_val  = pgm_read_byte(tpl_ptr + (j * N_FEATURES) + f);
            uint16_t diff    = abs((int)live_val - (int)tpl_val);
            if (f >= 4) diff <<= 1;
            cost += diff;
        }
        uint32_t prev_val = (j == 0) ? 0 : SRAM_read32(PREV_ADDR(j - 1));
        SRAM_write32(PREV_ADDR(j), (j == 0) ? cost : prev_val + cost);
    }

    // Fill matrix row by row
    for (uint8_t i = 1; i < live_len; i++) {
        // Init curr row to INF
        uint8_t j_start = (i > w) ? (i - w) : 0;
        uint8_t j_end   = (i + w + 1 < tpl_len) ? (i + w + 1) : tpl_len;

        // Zero out only the band we'll write (saves SRAM writes)
        for (uint8_t j = 0; j < tpl_len; j++)
            SRAM_write32(CURR_ADDR(j), INF);

        for (uint8_t j = j_start; j < j_end; j++) {
            uint16_t cost = 0;
            for (uint8_t f = 0; f < N_FEATURES; f++) {
                uint8_t live_val = live_features[i][f];
                uint8_t tpl_val  = pgm_read_byte(tpl_ptr + (j * N_FEATURES) + f);
                uint16_t diff    = abs((int)live_val - (int)tpl_val);
                if (f >= 4) diff <<= 1;
                cost += diff;
            }

            uint32_t a = (j > 0) ? SRAM_read32(CURR_ADDR(j - 1)) : INF;
            uint32_t b = SRAM_read32(PREV_ADDR(j));
            uint32_t c = (j > 0) ? SRAM_read32(PREV_ADDR(j - 1)) : INF;

            uint32_t min_prev = a;
            if (b < min_prev) min_prev = b;
            if (c < min_prev) min_prev = c;

            SRAM_write32(CURR_ADDR(j), (uint32_t)cost + min_prev);
        }

        // Swap curr -> prev
        for (uint8_t j = 0; j < tpl_len; j++)
            SRAM_write32(PREV_ADDR(j), SRAM_read32(CURR_ADDR(j)));
    }

    uint32_t result = SRAM_read32(PREV_ADDR(tpl_len - 1));
    return result / (live_len + tpl_len);
}

uint8_t DTW_ClassifyWord(uint8_t num_frames) {
    UART_TxString("Running DTW against Templates...\r\n");
    uint32_t best_dist = 0xFFFFFFFF;
    uint8_t best_tpl_idx = 0;
    uint8_t t = 0;
    for (t = 0; t < TOTAL_TEMPLATES; t++) {
        
        uint32_t dist = DTW_Distance(num_frames, t);
        if (dist < best_dist) {
            best_dist = dist;
            best_tpl_idx = t;
        }
    }

    
    uint8_t recognized_word_idx = pgm_read_byte(&template_labels[best_tpl_idx]);
    
    char buffer[16];
    DTW_GetWordString(recognized_word_idx, buffer); 
    
    UART_TxString("\r\nMATCH FOUND: ");
    UART_TxString(buffer);
    UART_TxString(" (Score: ");
    UART_TxNum(best_dist);
    UART_TxString(")\r\n\r\n");
    
    return recognized_word_idx;
}

// Helper to pass strings to main.c without exposing the headers
void DTW_GetWordString(uint8_t word_idx, char* out_str) {
    const char* word_ptr = (const char*)pgm_read_word(&word_labels[word_idx]);
    strcpy_P(out_str, word_ptr);
}
uint8_t DTW_ClassifyWord_DEBUG(uint8_t num_frames) {
    UART_TxString("\r\n--- DTW ---\r\n");
    UART_TxString("live_frames=");
    UART_TxNum(num_frames);
    UART_TxString("\r\n");
 
    uint32_t best_dist = 0xFFFFFFFF;
    uint8_t  best_tpl_idx = 0;
 
    for (uint8_t t = 0; t < TOTAL_TEMPLATES; t++) {
        uint8_t tpl_len = pgm_read_byte(&template_lengths[t]);
        uint32_t dist   = DTW_Distance(num_frames, t);
 
        // Print: t0 len=12 dist=4321
        UART_TxString("t");
        UART_TxNum(t);
        UART_TxString(" len=");
        UART_TxNum(tpl_len);
        UART_TxString(" d=");
        UART_TxNum(dist);
        UART_TxString("\r\n");
 
        if (dist < best_dist) {
            best_dist     = dist;
            best_tpl_idx  = t;
        }
    }
 
    uint8_t word_idx = pgm_read_byte(&template_labels[best_tpl_idx]);
    char buffer[16];
    DTW_GetWordString(word_idx, buffer);
 
    UART_TxString("BEST: t");
    UART_TxNum(best_tpl_idx);
    UART_TxString(" -> ");
    UART_TxString(buffer);
    UART_TxString(" score=");
    UART_TxNum(best_dist);
    UART_TxString("\r\n");
 
    return word_idx;
}