#ifndef F_CPU
#define F_CPU 11059200UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdbool.h>
#include <string.h>

#include "uart.h"
#include "lcd.h"
#include "audio.h"
#include "dsp.h"
#include "dtw.h"
#include "ext_int.h"
#include "sram.h"

// ============================================================
// CONFIGURATION
// ============================================================
#define MAX_SAMPLES 8000
#define CMD_BUF_SIZE 16
#define MAX_FRAMES 40

// ============================================================
// GLOBAL STATE
// ============================================================

// Recording control
volatile uint8_t recording = 0;      // 1 = recording active
volatile uint8_t prev_recording = 0; // edge detection for state change
volatile uint8_t process_start = 0;  // Processing flag for DSP mode

// Audio data
volatile uint8_t audio_sample = 0; // latest ADC sample (8-bit)
volatile uint8_t sample_ready = 0; // flag for new sample availability

// SRAM buffer control
volatile uint16_t write_index = 0; // current write position in SRAM

// UART command buffer
volatile char uart_buf[CMD_BUF_SIZE];
volatile uint8_t uart_idx = 0;
volatile uint8_t cmd_ready = 0;

// Mode flag
// 1 = stream over UART (video mode)
// 0 = store into SRAM (offline processing)
bool VideoRecord = 0;

// Reset cause register mirror
uint8_t mcusr_mirror __attribute__((section(".noinit")));

// ============================================================
// RESET CAUSE CAPTURE
// ============================================================
void get_mcusr(void) __attribute__((naked, section(".init3")));
void get_mcusr(void)
{
    mcusr_mirror = MCUSR;
    MCUSR = 0;
}

// ============================================================
// TIMER1 ISR (8 kHz sample trigger)
// ============================================================
ISR(TIMER1_COMPA_vect)
{
    if (recording)
    {
        ADCSRA |= (1 << ADSC); // start ADC conversion
    }
}

// ============================================================
// EXTERNAL INTERRUPT (START/STOP BUTTON)
// ============================================================
ISR(INT0_vect)
{
    recording ^= 1;

    if (recording)
    {
        write_index = 0;
        ADCSRA |= (1 << ADEN);
    }
    else
    {
        ADCSRA &= ~(1 << ADEN);
    }
}

// ============================================================
// ADC COMPLETE ISR
// ============================================================
ISR(ADC_vect)
{
    if (!recording)
        return;

    if (write_index < MAX_SAMPLES)
    {
        audio_sample = ADCH;
        sample_ready = 1;

        // Store only in SRAM mode
        if (!VideoRecord)
        {
            SRAM_write(write_index, audio_sample);
        }

        write_index++;
    }
    else
    {
        recording = 0;
    }
}

// ============================================================
// UART RX ISR (command parser)
// ============================================================
ISR(USART_RXC_vect)
{
    char c = UDR;

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

// ============================================================
// UART COMMAND HANDLER
// ============================================================
void process_uart_command(void)
{
    if (strcmp((char *)uart_buf, "stream") == 0)
    {
        VideoRecord = 1;
        UART_TxString("MODE: STREAMING\r\n");
        LCD_Clear();
        LCD_String("Stream mode");
    }
    else if (strcmp((char *)uart_buf, "dsp") == 0)
    {
        VideoRecord = 0;
        UART_TxString("MODE: DSP\r\n");
        LCD_Clear();
        LCD_String("DSP mode");
    }
    else
    {
        UART_TxString("UNKNOWN CMD\r\n");
    }
}


int main(void)
{
    UART_Init(230400);

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

    while (1)
    {
        // Handle UART commands
        if (cmd_ready)
        {
            process_uart_command();
            cmd_ready = 0;
        }

        // Recording start event
        if (recording && !prev_recording)
        {
            LCD_Clear();
            LCD_String("Recording...");
            UART_TxString("START\n");
            prev_recording = 1;
        }

        // Recording stop event
        else if (!recording && prev_recording)
        {
            LCD_Clear();
            LCD_String("Stopped");
            UART_TxString("STOP\n");
            prev_recording = 0;
            process_start = 1;
        }

        // Real-time streaming mode
        if (recording && VideoRecord && sample_ready)
        {
            sample_ready = 0;
            UART_sendByte(audio_sample);
        }

        // ================= DSP MODE =================
        if (!VideoRecord && process_start)
        {
            process_start = 0;

            uint8_t frame_count = 0;

            LCD_Clear();
            LCD_String("Extracting...");
            UART_TxString("EXTRACTING\n");

            uint16_t sram_index = 0;

            while ((sram_index + FRAME_SIZE) <= write_index &&
                   frame_count < MAX_FRAMES)
            {

                for (uint16_t i = 0; i < FRAME_SIZE; i++)
                {
                    audio_buffer[i] = SRAM_read(sram_index++);
                }
                DSP_ExtractFeatures(0, frame_count);
                DEBUG_PrintFrame(frame_count);

                frame_count++;
            }


            recording = 0;

            ADCSRA &= ~(1 << ADEN);

            UART_TxString("STOP\n");

            LCD_Clear();
            LCD_String("Processing...");

            uint8_t word_idx = DTW_ClassifyWord(frame_count);

            char word[16];

            DTW_GetWordString(word_idx, word);

            LCD_Clear();

            LCD_SetCursor(0, 0);
            LCD_String("Detected:");

            LCD_SetCursor(1, 0);
            LCD_String(word);

            live_frame_count = 0;
            frame_count = 0;
        }
    }
}
