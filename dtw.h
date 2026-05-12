#ifndef DTW_H
#define DTW_H

#include <stdint.h>

// Runs the DTW calculation and returns the index (0-7) of the winning word
uint8_t DTW_ClassifyWord(uint8_t num_frames);

// Safely fetches the text string of the word from Flash Memory
void DTW_GetWordString(uint8_t word_idx, char* out_str);
uint8_t DTW_ClassifyWord_DEBUG(uint8_t num_frames);
void DEBUG_PrintRAMInfo(void);
#endif // DTW_H