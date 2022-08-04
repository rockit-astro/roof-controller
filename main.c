//**********************************************************************************
//  Copyright 2017, 2022 Paul Chote
//  This file is part of superwasp-roof-controller, which is free software. It is made
//  available to you under version 3 (or later) of the GNU General Public License,
//  as published by the Free Software Foundation and included in the LICENSE file.
//**********************************************************************************

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "usb.h"

#define BLINKER_LED_DISABLED PORTC &= ~_BV(PC7)
#define BLINKER_LED_ENABLED  PORTC |= _BV(PC7)
#define BLINKER_LED_INIT     DDRC |= _BV(DDC7), BLINKER_LED_DISABLED

#define OPEN_ENABLED PORTF &= ~_BV(PF0)
#define OPEN_DISABLED  PORTF |= _BV(PF0)
#define OPEN_INIT     DDRF |= _BV(DDF0), OPEN_DISABLED

#define CLOSE_ENABLED PORTF &= ~_BV(PF1)
#define CLOSE_DISABLED  PORTF |= _BV(PF1)
#define CLOSE_INIT     DDRF |= _BV(DDF1), CLOSE_DISABLED

#define SIREN_ENABLED PORTF &= ~_BV(PF4)
#define SIREN_DISABLED  PORTF |= _BV(PF4)
#define SIREN_INIT     DDRF |= _BV(DDF4), SIREN_DISABLED

#define AUXMOTOR_ENABLED PORTF &= ~_BV(PF5)
#define AUXMOTOR_DISABLED  PORTF |= _BV(PF5)
#define AUXMOTOR_INIT     DDRF |= _BV(DDF5), AUXMOTOR_DISABLED

#define LIMIT_OPEN_TRIGGERED bit_is_clear(PINF, PINF6)
#define LIMIT_OPEN_INIT      DDRF &= ~_BV(DDF6), PORTF |= _BV(PF6)

#define LIMIT_CLOSED_TRIGGERED bit_is_clear(PINF, PINF7)
#define LIMIT_CLOSED_INIT      DDRF &= ~_BV(DDF7), PORTF |= _BV(PF7)

#define STATUS_PARTOPEN 0
#define STATUS_CLOSED 1
#define STATUS_OPEN 2
#define STATUS_CLOSING 3
#define STATUS_OPENING 4

// Number of seconds remaining until triggering the force-close
volatile uint8_t heartbeat_seconds_remaining = 0;

// Sticky status for whether the heartbeat has timed out
// and is either closing or has closed the roof.
volatile bool heartbeat_triggered = false;

volatile uint8_t close_seconds_remaining = 0;
volatile uint8_t open_seconds_remaining = 0;
volatile uint8_t siren_seconds_remaining = 0;
volatile bool close_using_auxmotor = false;

// Rate limit the status reports to the host PC to 1Hz
// This is done
volatile uint8_t current_status = STATUS_PARTOPEN;
volatile bool send_status = false;

volatile bool led_active;
char output[15];

// The value recorded by the 8-cycle ADC mean of the ground
const int16_t ground_offset = 1979;

// The relationship between ADC units and volts
const float gain = 0.01712;

volatile int16_t voltage = 0;

void measure_voltage(void)
{
    uint8_t msb, lsb;
    uint16_t sum = 0;

    // Constantly loop reading values from SPI
    // Average 16 values to measure voltage
    for (uint8_t i = 0; i < 16; i++)
    {
        PORTB &= ~_BV(PB0);

        // Read two bytes of data then assemble them into a 16 bit
        // number following Figure 6-1 from the MCP3201 data sheets
        SPDR = 0x00;
        loop_until_bit_is_set(SPSR, SPIF);
        msb = SPDR;

        SPDR = 0x00;
        loop_until_bit_is_set(SPSR, SPIF);
        lsb = SPDR;

        PORTB |= _BV(PB0);

        // Extract 12 bit value following the bit pattern described in
        // Figure 6-1 from the MCP3201 data sheet and add to the average
        sum += (((msb & 0x1F) << 8) | lsb) >> 1;
    }

    // Divide by 16 to complete the average and subtract the zero offset
    // Disable interrupts while updating to ensure data consistency
    cli();
    voltage = (int16_t)(sum >> 4) - ground_offset;
    sei();
}

void poll_usb(void)
{
    // Check for commands from the host PC
    while (usb_can_read())
    {
        int16_t value = usb_read();
        if (value < 0)
            break;
        
        // Values between 0-240 are treated as heartbeat pings
        // Values greater than 240 (0xF0) are reserved for commands
        switch (value)
        {
            // Open roof
            case 0xF1:
                if (!heartbeat_triggered)
                {
                    close_seconds_remaining = 0;
                    open_seconds_remaining = MAX_OPEN_SECONDS;
                    close_using_auxmotor = false;
                }
                break;

            // Close roof
            case 0xF2:
                open_seconds_remaining = 0;
                close_seconds_remaining = MAX_CLOSE_SECONDS;
                close_using_auxmotor = false;
                break;

            // Close roof using auxillery (12v) motor
            case 0xF3:
                open_seconds_remaining = 0;
                close_seconds_remaining = MAX_AUX_CLOSE_SECONDS;
                close_using_auxmotor = true;
                break;

            // Enable the siren for SIREN_ACTIVE_SECONDS
            case 0xFE:
                siren_seconds_remaining = SIREN_ACTIVE_SECONDS;
                break;

            // Stop roof movement and siren
            case 0xFF:
                if (open_seconds_remaining > 1)
                    open_seconds_remaining = 1;
                if (close_seconds_remaining > 1)
                    close_seconds_remaining = 1;
                if (siren_seconds_remaining > 1)
                    siren_seconds_remaining = 1;
                break;

            // Clear the sticky trigger flag when disabling the heartbeat
            // Also stops an active close
            case 0:
                heartbeat_triggered = false;
                close_seconds_remaining = 0;
                heartbeat_seconds_remaining = 0;
                break;

            // Reset the heartbeat timer
            default:
                // If the heatbeat has triggered the status must be manually
                // cleared by sending a 0 byte
                if (!heartbeat_triggered && value <= 240)
                    heartbeat_seconds_remaining = value;
        }
    }

    if (send_status)
    {
        // Send current status back to the host computer
        // Disable interrupts while updating to ensure data consistency
        cli();
        uint8_t heartbeat = heartbeat_triggered ? 0xFF : heartbeat_seconds_remaining;
        snprintf(output, 15, "%01d,%03d,%+06.2f\r\n", current_status, heartbeat, voltage * gain);
        sei();

        usb_write_data(output, 14);
        send_status = false;
    }
}

int main(void)
{
    // Configure timer1 to interrupt every 0.50 seconds
    OCR1A = 7812;
    TCCR1B = _BV(CS12) | _BV(CS10) | _BV(WGM12);
    TIMSK1 |= _BV(OCIE1A);

    // Configure GPIO pins
    BLINKER_LED_INIT;
    OPEN_INIT;
    CLOSE_INIT;
    SIREN_INIT;
    AUXMOTOR_INIT;
    LIMIT_OPEN_INIT;
    LIMIT_CLOSED_INIT;

    usb_initialize();

    // Set SS, SCK as output
    DDRB = _BV(DDB0) | _BV(DDB1);
 
    // Enable SPI Master @ 250kHz, transmit MSB first
    // Clock idle level is low, sample on falling edge
    SPCR = _BV(SPE) | _BV(MSTR) | _BV(SPR1) | _BV(CPHA);

    sei();
    for (;;)
    {
        measure_voltage();
        poll_usb();
    }
}

ISR(TIMER1_COMPA_vect)
{
    if ((led_active ^= true))
    {
        BLINKER_LED_ENABLED;
        
        // Update roof status every other cycle (once per second)
        // This is done inside the ISR to avoid any problems with the USB connection blocking
        // from interfering with the primary job of the device
        //
        // Decrement the heartbeat counter and trigger a close if it reaches 0
        // Force closing always uses both motors because we don't know here whether
        // there is a power failure on the main motor or not.
        if (!heartbeat_triggered && heartbeat_seconds_remaining != 0)
        {
            // Start the siren 5 seconds before closing
            // There is a chance that we may receive another
            // ping, but its much more likely that we will close
            if (heartbeat_seconds_remaining == SIREN_ACTIVE_SECONDS)
                siren_seconds_remaining = SIREN_ACTIVE_SECONDS;

            if (--heartbeat_seconds_remaining == 0)
            {
                OPEN_DISABLED;
                heartbeat_triggered = true;
                close_using_auxmotor = true;
                open_seconds_remaining = 0;
                close_seconds_remaining = MAX_AUX_CLOSE_SECONDS;
            }
        }

        uint8_t status = STATUS_PARTOPEN;
        if (LIMIT_CLOSED_TRIGGERED)
        {
            CLOSE_DISABLED;
            AUXMOTOR_DISABLED;
            status = STATUS_CLOSED;
            close_seconds_remaining = 0;
            close_using_auxmotor = false;
        }
        else if (LIMIT_OPEN_TRIGGERED)
        {
            OPEN_DISABLED;
            status = STATUS_OPEN;
            open_seconds_remaining = 0;
        }

        if (close_seconds_remaining > 0)
        {
            CLOSE_ENABLED;
            if (close_using_auxmotor)
                AUXMOTOR_ENABLED;

            status = STATUS_CLOSING;
            if (--close_seconds_remaining == 0)
            {
                CLOSE_DISABLED;
                AUXMOTOR_DISABLED;
                close_using_auxmotor = false;
            }
        }
        else if (open_seconds_remaining > 0)
        {
            OPEN_ENABLED;
            status = STATUS_OPENING;
            if (--open_seconds_remaining == 0)
                OPEN_DISABLED;
        }

        if (siren_seconds_remaining > 0)
        {
            SIREN_ENABLED;
            if (--siren_seconds_remaining == 0)
                SIREN_DISABLED;
        }

        // Send status from the main loop to avoid issues with
        // USB blocking if the connection goes down uncleanly.
        current_status = status;
        send_status = true;
    }
    else
        BLINKER_LED_DISABLED;
}