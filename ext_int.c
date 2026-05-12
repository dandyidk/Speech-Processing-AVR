

#include <avr/io.h>
#include <avr/interrupt.h>
#include "ext_int.h"

void INT0_init(void)
{
    DDRD &= ~(1 << PD2);   // input
    PORTD |= (1 << PD2);   // pull-up enabled

    MCUCR |= (1 << ISC01); // falling edge trigger
    GICR |= (1 << INT0);   // enable INT0
}