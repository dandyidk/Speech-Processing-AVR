#ifndef F_CPU
#define F_CPU 11059200UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdbool.h>
#include <string.h>

#include "uart.h"
#include "lcd.h"
#include "audio.h"
#include "dsp.h"
#include "dtw.h"
#include "ext_int.h"
#include "sram.h"

// ================= CONFIG =================
#define MAX_SAMPLES 8000
#define CMD_BUF_SIZE 16


// ================= GLOBALS =================
volatile uint8_t recording = 0;
volatile uint8_t prev_recording = 0;

volatile uint8_t audio_sample = 0;
volatile uint8_t sample_ready = 0;

volatile uint16_t sample_count = 0;
volatile uint8_t write_index = 0;

volatile char uart_buf[CMD_BUF_SIZE];
volatile uint8_t uart_idx = 0;
volatile uint8_t cmd_ready = 0;

bool DataCollection = 0;

uint8_t mcusr_mirror __attribute__ ((section (".noinit")));

void get_mcusr(void) __attribute__((naked)) __attribute__((section(".init3")));

void get_mcusr(void)
{
    mcusr_mirror = MCUSR;
    MCUSR = 0;
}
// ================= TIMER ISR =================
ISR(TIMER1_COMPA_vect)
{
    if (recording)
        ADCSRA |= (1 << ADSC);
}

// ================= BUTTON ISR =================
ISR(INT0_vect)
{
    recording ^= 1;

    if (recording)
    {
        sample_count = 0;
        write_index = 0;

        frame_ready = false;
        frame_start_index = 0;

        ADCSRA |= (1 << ADEN);
    }
    else
    {
        ADCSRA &= ~(1 << ADEN);
    }
}

// ================= ADC ISR =================
ISR(ADC_vect)
{
    if (!recording)
        return;

    audio_sample = ADCH;
    sample_ready = 1;

    audio_buffer[write_index++] = audio_sample;
    sample_count++;

    // frame ready every 128 samples
    if (write_index == 128 || write_index == 0)
    {
        frame_start_index = (write_index == 128) ? 128 : 0;
        frame_ready = true;
    }

    // stop after max samples
    if (sample_count >= MAX_SAMPLES)
        recording = 0;
}
// ================= USART ISR =================

ISR(USART_RXC_vect)
{
    char c = UDR;

    // end of command
    if (c == '\n' || c == '\r')
    {
        uart_buf[uart_idx] = '\0';
        uart_idx = 0;
        cmd_ready = 1;
    }
    else
    {
        if (uart_idx < CMD_BUF_SIZE - 1)
        {
            uart_buf[uart_idx++] = c;
        }
    }
}

void process_uart_command(void)
{
    if (strcmp((char*)uart_buf, "collect") == 0)
    {
        DataCollection = 1;
        UART_TxString("MODE: COLLECT\r\n");
    }
    else if (strcmp((char*)uart_buf, "dsp") == 0)
    {
        DataCollection = 0;
        UART_TxString("MODE: DSP\r\n");
    }
    else
    {
        UART_TxString("UNKNOWN CMD\r\n");
    }
}

// ================= MAIN =================
int main(void)
{
    UART_Init(115200);
    DEBUG_PrintRAMInfo();

    LCD_Init();
    LCD_Clear();
    LCD_String("System Ready!");

    INT0_init();
    Audio_Init();
    TIMER1_init_CTC_8kHz();
    SRAM_init();

    sei();
    UART_TxString("RESET FLAG: ");
    UART_TxNum(mcusr_mirror);
    UART_TxString("\r\n");

    uint8_t frame_count = 0;

    while (1)
    {
        if (cmd_ready)
{
    process_uart_command();
    cmd_ready = 0;
}
        // recording started
        if (recording && !prev_recording)
        {
            LCD_Clear();
            LCD_String("Recording...");

            UART_TxString("START\n");

            prev_recording = 1;
        }

        // recording stopped
        else if (!recording && prev_recording&&DataCollection)
        {
            LCD_Clear();
            LCD_String("Stopped");

            UART_TxString("STOP\n");

            prev_recording = 0;
            frame_count = 0;
        }

        // ================= DATA COLLECTION =================
        if (DataCollection && sample_ready)
        {
            UART_sendByte(audio_sample);
            sample_ready = 0;
        }

// ================= DSP MODE =================
        if (!DataCollection && frame_ready && recording)  // Only process while actively recording
        {
            frame_ready = false;
            
            // CRITICAL: Stop ADC during DSP to prevent buffer corruption
            uint8_t adc_was_on = (ADCSRA & (1 << ADEN));
            ADCSRA &= ~(1 << ADEN);  // Disable ADC            
            DSP_ExtractFeatures(frame_start_index, frame_count);
            DEBUG_PrintFrame(frame_count);
            frame_count++;
            
            // Re-enable ADC if we're still recording and haven't hit 40 yet
            if (recording && frame_count < 40 && adc_was_on)
                ADCSRA |= (1 << ADEN);
            
            if (frame_count >= 40)
            {
                // Stop recording completely before classification
                recording = 0;
                ADCSRA &= ~(1 << ADEN);
                LCD_Clear();
                LCD_String("Processing...");
                DEBUG_PrintRAMInfo();
                uint8_t word_idx = DTW_ClassifyWord_DEBUG(frame_count);
                
                char word[16];
                DTW_GetWordString(word_idx, word);
                
                LCD_Clear();
                LCD_SetCursor(0, 0);
                LCD_String("Detected:");
                LCD_SetCursor(1, 0);
                LCD_String(word);
                
                frame_count = 0;  // Reset for next recording
            }
        }
    }
}