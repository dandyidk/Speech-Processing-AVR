#include "dsp.h"
#include "audio.h" 
#include <stdlib.h> 
#include <avr/pgmspace.h>
#include <stdio.h>
#include "uart.h"
uint8_t live_features[40][8];

// Tell the compiler these exist in Flash memory so we can 
// read them without causing a multiple-definition error!
extern const float feat_min[8] PROGMEM;
extern const float feat_max[8] PROGMEM;

extern int fix_fft(int8_t fr[], int8_t fi[], int16_t m, int16_t inverse);

float DSP_GetEnergy(uint8_t start_idx) {
    int32_t sum_squares = 0;
    for (uint16_t i = 0; i < 256; i++) {
        int16_t sample = (int16_t)audio_buffer[(uint8_t)(start_idx + i)] - 128;
        sum_squares += (int32_t)sample * sample;
    }
    float avg_energy = (float)sum_squares / 256.0f;
    return avg_energy / 16384.0f;
}

void DSP_ExtractFeatures(uint8_t start_idx, uint8_t frame_number) {

    if (frame_number >= 40) return;

    // We will temporarily hold the float values for this single frame
    float raw_features[8];

    // 1. STE
    raw_features[0] = DSP_GetEnergy(start_idx);

    // 2. ZCR
    uint16_t zcr_count = 0;
    int16_t prev_sample = (int16_t)audio_buffer[start_idx] - 128;
    for (uint16_t i = 1; i < 256; i++) {
        int16_t curr_sample = (int16_t)audio_buffer[(uint8_t)(start_idx + i)] - 128;
        if ((prev_sample >= 0 && curr_sample < 0) || (prev_sample < 0 && curr_sample >= 0)) {
            zcr_count++;
        }
        prev_sample = curr_sample;
    }
    raw_features[1] = (float)zcr_count / 256.0f;

    // FFT Setup
    int8_t real[256];
    int8_t imag[256];
    for (uint16_t i = 0; i < 256; i++) {
        real[i] = (int8_t)((int16_t)audio_buffer[(uint8_t)(start_idx + i)] - 128);
        imag[i] = 0;
    }

    fix_fft(real, imag, 8, 0);

    uint8_t mag[128];
    for (uint8_t i = 0; i < 128; i++) {
        uint8_t a = abs(real[i]);
        uint8_t b = abs(imag[i]);
        uint8_t max_val = (a > b) ? a : b;
        uint8_t min_val = (a < b) ? a : b;
        mag[i] = max_val + (min_val >> 1); 
    }

    // 3 & 4. Centroid & DomFreq
    uint32_t num = 0, den = 0;
    uint8_t max_mag = 0, dom_idx = 0;
    for (uint8_t i = 1; i < 128; i++) {
        num += i * mag[i];
        den += mag[i];
        if (mag[i] > max_mag) { max_mag = mag[i]; dom_idx = i; }
    }
    raw_features[2] = (den == 0) ? 0.0f : ((float)num / (float)den) / 128.0f;
    raw_features[3] = (float)dom_idx / 128.0f;

    // 5 to 8. MFCC Lite
    uint16_t mfcc_lite[4] = {0, 0, 0, 0};
    for(uint8_t i = 1; i < 5; i++)   mfcc_lite[0] += mag[i];
    for(uint8_t i = 5; i < 12; i++)  mfcc_lite[1] += mag[i];
    for(uint8_t i = 12; i < 27; i++) mfcc_lite[2] += mag[i];
    for(uint8_t i = 27; i < 128; i++) mfcc_lite[3] += mag[i];

    raw_features[4] = (float)mfcc_lite[0] / 200.0f;
    raw_features[5] = (float)mfcc_lite[1] / 200.0f;
    raw_features[6] = (float)mfcc_lite[2] / 200.0f;
    raw_features[7] = (float)mfcc_lite[3] / 200.0f;

    // --- INSTANT QUANTIZATION ---
    // Instead of saving floats, we instantly scale them to 0-255 and save them.
    for (uint8_t f = 0; f < 8; f++) {
        float f_min = pgm_read_float(&feat_min[f]);
        float f_max = pgm_read_float(&feat_max[f]);
        float f_range = f_max - f_min;
        if (f_range == 0.0f) f_range = 1.0f;
        
        float norm = (raw_features[f] - f_min) / f_range;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        
        live_features[frame_number][f] = (uint8_t)(norm * 255.0f + 0.5f);
    }
    
}
void DEBUG_PrintFrame(uint8_t frame_number) {
    UART_TxString("F");
    UART_TxNum(frame_number);
    UART_TxString(":[");
    for (uint8_t f = 0; f < 8; f++) {
        UART_TxNum(live_features[frame_number][f]);
        if (f < 7) UART_TxChar(',');
    }
    UART_TxString("]\r\n");
}