#ifndef DTW_H
#define DTW_H

#include <stdint.h>
static uint32_t DTW_Distance(uint8_t live_len, uint8_t tpl_idx);
// Runs the DTW calculation and returns the index (0-7) of the winning word
uint8_t DTW_ClassifyWord(uint8_t num_frames);

// Safely fetches the text string of the word from Flash Memory
void DTW_GetWordString(uint8_t word_idx, char* out_str);

#endif // DTW_H