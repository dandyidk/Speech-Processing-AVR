/*
 * fix_fft.c — Fixed-point FFT for 8-bit AVR
 *
 * This is a standard Cooley-Tukey radix-2 DIT FFT using a proper
 * 128-entry sine lookup table in PROGMEM. The broken twiddle factor
 * approximation (wr=85, wi=85) has been replaced with correct values.
 *
 * Input:  fr[N], fi[N] — real and imaginary parts as int8_t
 * Output: fr[N], fi[N] — in-place, bit-reversed order
 * m:      log2(N), so N=256 -> m=8
 * inverse: 0 = forward FFT
 *
 * Twiddle factors: W_N^k = cos(2πk/N) - j·sin(2πk/N)
 * Stored as int8_t scaled to [-127, 127].
 */

#include <stdint.h>
#include <avr/pgmspace.h>

/*
 * Sine table: sin_table[k] = round(127 * sin(2*pi*k / 256))
 * for k = 0..127. Covers a full half-period.
 * cos(x) = sin(x + 64) using this 256-point table indexing.
 */
static const int8_t sin_table[128] PROGMEM = {
      0,   3,   6,   9,  12,  15,  18,  21,
     24,  27,  30,  33,  36,  39,  42,  45,
     48,  51,  54,  57,  59,  62,  65,  67,
     70,  73,  75,  78,  80,  82,  85,  87,
     89,  91,  94,  96,  98, 100, 102, 103,
    105, 107, 108, 110, 112, 113, 114, 116,
    117, 118, 119, 120, 121, 122, 123, 123,
    124, 125, 125, 126, 126, 126, 127, 127,
    127, 127, 127, 126, 126, 126, 125, 125,
    124, 123, 123, 122, 121, 120, 119, 118,
    117, 116, 114, 113, 112, 110, 108, 107,
    105, 103, 102, 100,  98,  96,  94,  91,
     89,  87,  85,  82,  80,  78,  75,  73,
     70,  67,  65,  62,  59,  57,  54,  51,
     48,  45,  42,  39,  36,  33,  30,  27,
     24,  21,  18,  15,  12,   9,   6,   3
};

/*
 * sin_pgm(k) — returns sin(2*pi*k/256) scaled to [-127,127]
 * k is taken modulo 256.
 */
static int8_t sin_pgm(uint8_t k)
{
    if (k < 128)
        return pgm_read_byte(&sin_table[k]);
    else
        return -pgm_read_byte(&sin_table[k - 128]);
}

/*
 * cos_pgm(k) — cos(2*pi*k/256) = sin(2*pi*(k+64)/256)
 */
static int8_t cos_pgm(uint8_t k)
{
    return sin_pgm((uint8_t)(k + 64));
}

int fix_fft(int8_t fr[], int8_t fi[], int16_t m, int16_t inverse)
{
    int16_t n = 1 << m;   /* N = 256 for m=8 */

    /* --- Bit-reversal permutation --- */
    int16_t mr = 0;
    int16_t nn = n - 1;
    for (int16_t i = 1; i <= nn; i++) {
        int16_t l = n;
        do { l >>= 1; } while (mr + l > nn);
        mr = (mr & (l - 1)) + l;
        if (mr <= i) continue;
        int8_t t;
        t = fr[i]; fr[i] = fr[mr]; fr[mr] = t;
        t = fi[i]; fi[i] = fi[mr]; fi[mr] = t;
    }

    /* --- Cooley-Tukey butterfly stages --- */
    int16_t l = 1;
    /*
     * At each stage, the twiddle step through the 256-point table is:
     *   step = 256 / (2*l)  = 128 / l
     * For l=1:   step=128  -> W = cos(pi) = -1  (correct for 2-pt FFT)
     * For l=2:   step=64   -> W = {1, -j}
     * For l=4:   step=32   -> W = {1, 0.707-0.707j, -j, ...}
     * etc.
     */
    while (l < n) {
        int16_t istep = l << 1;
        /* twiddle table step for this stage */
        uint8_t step = (uint8_t)(128 / l);   /* 256/(2l) = 128/l */

        for (int16_t k = 0; k < l; k++) {
            uint8_t angle = (uint8_t)(k * step); /* k * 256 / (2l) */
            int8_t wr =  cos_pgm(angle);
            int8_t wi = (inverse ? sin_pgm(angle) : -sin_pgm(angle));

            for (int16_t i = k; i < n; i += istep) {
                int16_t j = i + l;
                /* butterfly: scaled by >>7 to stay in int8 range */
                int16_t tr = (int8_t)(((int16_t)wr * fr[j] - (int16_t)wi * fi[j]) >> 7);
                int16_t ti = (int8_t)(((int16_t)wr * fi[j] + (int16_t)wi * fr[j]) >> 7);
                int16_t qr = fr[i];
                int16_t qi = fi[i];
                fr[j] = qr - tr;
                fi[j] = qi - ti;
                fr[i] = qr + tr;
                fi[i] = qi + ti;
            }
        }
        l = istep;
    }
    return 0;
}
