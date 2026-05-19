#ifndef DSP_H
#define DSP_H

#include <stdint.h>
#define NOISE_FLOOR          30   // Minimum amplitude to consider for ZCR counting
#define FRAME_SIZE 200

// RAM SAVER: Array is now uint8_t (320 bytes total instead of 1,280)
extern uint8_t live_features[40][9];
extern uint8_t live_frame_count;

// Process a 256-sample frame starting at the given index
void DSP_ExtractFeatures(uint8_t start_idx, uint8_t frame_number);

// Calculate just the Short-Time Energy
uint8_t DSP_GetEnergy(uint8_t start_idx);
uint8_t DSP_GetZCR(uint8_t start_idx);
uint8_t DSP_GetSpectralCentroid(uint8_t *mag);
uint8_t DSP_GetDominantFreq(uint8_t *mag);
void DEBUG_PrintFrame(uint8_t frame_number);
#endif // DSP_H