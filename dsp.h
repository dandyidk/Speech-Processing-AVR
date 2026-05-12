#ifndef DSP_H
#define DSP_H

#include <stdint.h>

// RAM SAVER: Array is now uint8_t (320 bytes total instead of 1,280)
extern uint8_t live_features[40][8];

// Process a 256-sample frame starting at the given index
void DSP_ExtractFeatures(uint8_t start_idx, uint8_t frame_number);

// Calculate just the Short-Time Energy
float DSP_GetEnergy(uint8_t start_idx);

void DEBUG_PrintFrame(uint8_t frame_number);
#endif // DSP_H