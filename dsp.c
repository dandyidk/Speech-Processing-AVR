#include "dsp.h"
#include "audio.h"
#include <stdlib.h>
#include "uart.h"
uint8_t live_features[40][8];
uint8_t live_frame_count=0;
extern int fix_fft(int8_t fr[], int8_t fi[], int16_t m, int16_t inverse);

uint8_t DSP_GetEnergy(uint8_t start_idx)
{
    int32_t sum_sq = 0;
    for (uint16_t i = 0; i < FRAME_SIZE; i++)
    {
        int16_t s = (int16_t)audio_buffer[(uint8_t)(start_idx + i)] - 128;
        sum_sq += (int32_t)s * s;
    }
    // avg in range [0, 16384], scale to [0, 255]
    uint32_t avg = (uint32_t)sum_sq / 256;
    return (avg >= 16384) ? 255 : (uint8_t)((avg * 255UL) / 16384);
}

uint8_t DSP_GetZCR(uint8_t start_idx)
{
    uint16_t zcr = 0, valid = 0;
    int16_t prev = (int16_t)audio_buffer[start_idx] - 128;

    for (uint16_t i = 1; i < FRAME_SIZE; i++)
    {
        int16_t curr = (int16_t)audio_buffer[(uint8_t)(start_idx + i)] - 128;

        if (abs(curr) < NOISE_FLOOR && abs(prev) < NOISE_FLOOR)
        {
            prev = curr;
            continue;
        }
        valid++;
        if ((prev ^ curr) < 0)
            zcr++;
        prev = curr;
    }

    return (valid < 10) ? 0 : (uint8_t)((zcr * 255U) / valid);
}

uint8_t DSP_GetSpectralCentroid(uint8_t *mag)
{
    uint32_t num = 0, den = 0;
    for (uint8_t i = 1; i < 128; i++)
    {
        num += (uint32_t)i * mag[i];
        den += mag[i];
    }
    if (den == 0)
        return 0;
    // centroid in [0,127], scale to [0,255]
    uint8_t c = (uint8_t)((num / den) * 2);
    return c;
}

uint8_t DSP_GetDominantFreq(uint8_t *mag)
{
    uint8_t best = 0;
    uint16_t best_val = 0;
    for (uint8_t i = 2; i < 126; i++)
    {
        uint16_t v = mag[i - 1] + ((uint16_t)mag[i] << 1) + mag[i + 1];
        if (v > best_val)
        {
            best_val = v;
            best = i;
        }
    }
    return (uint8_t)((best * 255U) / 127);
}

void DSP_ExtractFeatures(uint8_t start_idx, uint8_t frame_number)
{
    if (frame_number >= 40)
        return;

    uint8_t *feat = live_features[frame_number];

    // 1. STE  2. ZCR
    feat[0] = DSP_GetEnergy(start_idx);
    feat[1] = DSP_GetZCR(start_idx);

    // FFT
    int8_t real[256], imag[256];
    for (uint16_t i = 0; i < FRAME_SIZE; i++)
    {
        real[i] = (int8_t)((int16_t)audio_buffer[(uint8_t)(start_idx + i)] - 128);
        imag[i] = 0;
    }
    fix_fft(real, imag, 8, 0);

    // Magnitude
    uint8_t mag[128];
    for (uint8_t i = 0; i < 128; i++)
    {
        uint8_t a = (uint8_t)abs(real[i]);
        uint8_t b = (uint8_t)abs(imag[i]);
        uint8_t mx = (a > b) ? a : b;
        uint8_t mn = (a < b) ? a : b;
        mag[i] = mx + (mn >> 1);
    }

    // 3. Spectral Centroid  4. Dominant Frequency
    feat[2] = DSP_GetSpectralCentroid(mag);
    feat[3] = DSP_GetDominantFreq(mag);

    // 5–8. MFCC Lite bands → scale to 0–255 using theoretical max per band
    // Band maxes: 4*255=1020, 7*255=1785, 15*255=3825, 101*255=25755
    uint16_t b0 = 0, b1 = 0, b2 = 0;
    uint32_t b3 = 0;
    for (uint8_t i = 1; i < 5; i++)
        b0 += mag[i];
    for (uint8_t i = 5; i < 12; i++)
        b1 += mag[i];
    for (uint8_t i = 12; i < 27; i++)
        b2 += mag[i];
    for (uint8_t i = 27; i < 128; i++)
        b3 += mag[i];

    feat[4] = (uint8_t)((b0 * 255U) / 1020U);
    feat[5] = (uint8_t)((b1 * 255U) / 1785U);
    feat[6] = (uint8_t)((b2 * 255U) / 3825U);
    feat[7] = (uint8_t)((b3 * 255UL) / 25755UL);
}

void DEBUG_PrintFrame(uint8_t frame_number)
{
    UART_TxString("F");
    UART_TxNum(frame_number);
    UART_TxString(":[");
    for (uint8_t f = 0; f < 8; f++)
    {
        UART_TxNum(live_features[frame_number][f]);
        if (f < 7)
            UART_TxChar(',');
    }
    UART_TxString("]\r\n");
}
