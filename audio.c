#include "audio.h"
#include <avr/io.h>
#define F_CPU 11059200UL 

#include <avr/interrupt.h>

// Define our volatile variables so they can be modified inside the ISR
volatile uint8_t audio_buffer[256];
// volatile uint8_t write_index = 0;

volatile bool frame_ready = false;
volatile uint8_t frame_start_index = 0;

void Audio_Init(void) {

    ADMUX = (1 << REFS0) | (1 << ADLAR); //left adjust 8 bit mode

    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1);
    ADCSRA |= (1 << ADIE);

}

void Audio_StartListening(void) {
    // Start Timer0 with Prescaler 8
    TCCR0 |= (1 << CS01);
}

void Audio_StopListening(void) {
    // Stop Timer0
    TCCR0 &= ~((1 << CS02) | (1 << CS01) | (1 << CS00));
}

void TIMER1_init_CTC_8kHz(void)
{
    // Clear Timer on Compare Match (CTC mode)
    TCCR1B |= (1 << WGM12);

    // prescaler = 8
    TCCR1B |= (1 << CS11);

    // 8 kHz interrupt rate
    OCR1A = (F_CPU / (8UL * 8000UL)) - 1;

    // enable compare interrupt
    TIMSK |= (1 << OCIE1A);
}

void TIMER1_start(void)
{
    // prescaler 8 (start timer)
    TCCR1B |= (1 << CS11);
}

void TIMER1_stop(void)
{
    // stop clock (no prescaler bits)
    TCCR1B &= ~((1 << CS12) | (1 << CS11) | (1 << CS10));
}
uint8_t ADC_read()
{
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ADCH;
}