#include "dtw.h"
#include "dsp.h"
#include "dtw_templates.h" // This is the ONLY file allowed to include this!
#include "uart.h"
#include <avr/pgmspace.h>
#include <stdlib.h>
#include "sram.h"
#include <string.h>

static uint32_t DTW_Distance(uint8_t live_len, uint8_t tpl_idx)
{
    uint8_t tpl_len = pgm_read_byte(&template_lengths[tpl_idx]);
    const uint8_t *tpl_ptr = (const uint8_t *)pgm_read_word(&template_ptrs[tpl_idx]);

    uint32_t prev[MAX_TEMPLATE_FRAMES];
    uint32_t curr[MAX_TEMPLATE_FRAMES];
    const uint32_t INF = 0x3FFFFFFF;

    int8_t w = DTW_BAND_W;
    if (abs((int)live_len - (int)tpl_len) > w)
    {
        w = abs((int)live_len - (int)tpl_len);
    }

    for (uint8_t j = 0; j < tpl_len; j++)
    {
        prev[j] = INF;
        curr[j] = INF;
    }

    // First row
    for (int8_t j = 0; j < tpl_len; j++)
    {
        if (abs(0 - j) > w)
            continue;
        uint16_t cost = 0;
        for (uint8_t f = 0; f < N_FEATURES; f++)
        {
            uint8_t live_val = live_features[0][f]; // Read direct from RAM
            uint8_t tpl_val = pgm_read_byte(tpl_ptr + (j * N_FEATURES) + f);
            uint16_t diff = abs((int)live_val - (int)tpl_val);
            if (f >= 4)
                diff <<= 1;
            cost += diff;
        }
        if (j == 0)
            prev[j] = cost;
        else
            prev[j] = prev[j - 1] + cost;
    }

    // Fill matrix
    for (uint8_t i = 1; i < live_len; i++)
    {
        for (uint8_t j = 0; j < tpl_len; j++)
            curr[j] = INF;

        int8_t j_start = i - w;
        if (j_start < 0)
            j_start = 0;
        int8_t j_end = i + w + 1;
        if (j_end > tpl_len)
            j_end = tpl_len;

        for (int8_t j = j_start; j < j_end; j++)
        {
            uint16_t cost = 0;
            for (uint8_t f = 0; f < N_FEATURES; f++)
            {
                uint8_t live_val = live_features[i][f]; // Read direct from RAM
                uint8_t tpl_val = pgm_read_byte(tpl_ptr + (j * N_FEATURES) + f);
                uint16_t diff = abs((int)live_val - (int)tpl_val);
                if (f >= 4)
                    diff <<= 1;
                cost += diff;
            }

            uint32_t min_prev = INF;
            if (j > 0 && curr[j - 1] < min_prev)
                min_prev = curr[j - 1];
            if (prev[j] < min_prev)
                min_prev = prev[j];
            if (j > 0 && prev[j - 1] < min_prev)
                min_prev = prev[j - 1];

            curr[j] = cost + min_prev;
        }
        for (uint8_t j = 0; j < tpl_len; j++)
            prev[j] = curr[j];
    }

    uint32_t final_cost = prev[tpl_len - 1];
    return final_cost / (live_len + tpl_len);
}

uint8_t DTW_ClassifyWord(uint8_t num_frames)
{
    uint32_t best_word_dist[N_WORDS];
    uint8_t best_word_tpl[N_WORDS];
    UART_TxString("\r\n--- DTW ---\r\n");
    UART_TxString("live_frames=");
    UART_TxNum(num_frames);
    UART_TxString("\r\n");

    uint32_t best_dist = 0xFFFFFFFF;
    uint8_t best_tpl_idx = 0;

    for (uint8_t w = 0; w < N_WORDS; w++)
    {
        best_word_dist[w] = 0xFFFFFFFF;
        best_word_tpl[w] = 0xFF;
    }
    //================= VAD =====================
    // find speech boundaries using STE (feat[0])
    uint8_t first = 0;
    for (uint8_t f = 0; f < num_frames; f++)
    {
        if (live_features[f][0] >= VAD_THRESHOLD)
        {
            first = f;
            break;
        }
    }

    uint8_t last = num_frames - 1;
    for (int8_t f = (int8_t)(num_frames - 1); f >= 0; f--)
    {
        if (live_features[f][0] >= VAD_THRESHOLD)
        {
            last = (uint8_t)f;
            break;
        }
    }

    // shift active window to index 0 (in-place)
    uint8_t trimmed = last - first + 1;
    if (first > 0)
    {
        for (uint8_t f = 0; f < trimmed; f++)
            for (uint8_t k = 0; k < 8; k++)
                live_features[f][k] = live_features[first + f][k];
    }

    live_frame_count = trimmed;

    UART_TxString("VAD: first=");
    UART_TxNum(first);
    UART_TxString(" last=");
    UART_TxNum(last);
    UART_TxString(" trimmed=");
    UART_TxNum(trimmed);
    UART_TxString("\r\n");

    for (uint8_t t = 0; t < TOTAL_TEMPLATES; t++)
    {
        uint8_t tpl_len = pgm_read_byte(&template_lengths[t]);

        uint32_t dist = DTW_Distance(num_frames, t);

        // Global best template
        if (dist < best_dist)
        {
            best_dist = dist;
            best_tpl_idx = t;
        }

        // Word corresponding to this template
        uint8_t word = pgm_read_byte(&template_labels[t]);

        // Best template for this word
        if (dist < best_word_dist[word])
        {
            best_word_dist[word] = dist;
            best_word_tpl[word] = t;
        }

        UART_TxString("t");
        UART_TxNum(t);

        UART_TxString(" len=");
        UART_TxNum(tpl_len);

        UART_TxString(" d=");
        UART_TxNum(dist);

        UART_TxString("\r\n");
    }
    UART_TxString("\r\n--- WORD RANKING ---\r\n");
        uint8_t used[N_WORDS] = {0};


    uint8_t word_rank[N_WORDS];

    // Initialize ranks to invalid
    for (uint8_t i = 0; i < N_WORDS; i++)
    {
        word_rank[i] = 0xFF;
    }

    // Generate rankings
    for (uint8_t rank = 0; rank < N_WORDS; rank++)
    {
        uint32_t rank_best_dist = 0xFFFFFFFF;
        uint8_t rank_best_word = 0xFF;

        // Find next-best unused word
        for (uint8_t w = 0; w < N_WORDS; w++)
        {
            if (used[w])
                continue;

            if (best_word_dist[w] < rank_best_dist)
            {
                rank_best_dist = best_word_dist[w];
                rank_best_word = w;
            }
        }

        if (rank_best_word == 0xFF)
            break;

        used[rank_best_word] = 1;

        // Store actual ranking
        word_rank[rank_best_word] = rank + 1;
    }
    UART_TxString("\r\n=== WORD RANKINGS ===\r\n");

    for (uint8_t w = 0; w < N_WORDS; w++)
    {
        char wbuf[16];
        DTW_GetWordString(w, wbuf);

        UART_TxString("#");
        UART_TxNum(word_rank[w]);

        UART_TxString("  ");
        UART_TxString(wbuf);

        UART_TxString("  tpl=");
        UART_TxNum(best_word_tpl[w]);

        UART_TxString("  dist=");
        UART_TxNum(best_word_dist[w]);

        UART_TxString("\r\n");
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

// Helper to pass strings to main.c without exposing the headers
void DTW_GetWordString(uint8_t word_idx, char *out_str)
{
    const char *word_ptr = (const char *)pgm_read_word(&word_labels[word_idx]);
    strcpy_P(out_str, word_ptr);
}
