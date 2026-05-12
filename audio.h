#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>

// The 256-byte circular audio buffer (fits easily in internal 2KB RAM)
extern volatile uint8_t audio_buffer[256];

// Flags to tell the main loop to process the data
extern volatile bool frame_ready;
extern volatile uint8_t frame_start_index;

// Initialize the ADC and the 8 kHz Timer
void Audio_Init(void);

// Start listening for speech (enables Timer0)
void Audio_StartListening(void);

// Stop listening
void Audio_StopListening(void);
void TIMER1_init_CTC_8kHz(void);
void TIMER1_start(void);
void TIMER1_stop(void);
uint8_t ADC_read();
#endif // AUDIO_H