/*
 * timer.c - Millisecond timer using the PIT (Programmable Interval Timer)
 *
 * Reprograms PIT channel 0 to ~1000 Hz (divisor 1193).
 * Chains the old BIOS handler every 55 ticks to keep the
 * time-of-day clock and floppy motor timeout working.
 */

#include <dos.h>
#include <conio.h>
#include "timer.h"

#define TIMER_INT   0x08
#define PIT_CMD     0x43
#define PIT_CH0     0x40
#define PIC_CMD     0x20
#define PIC_EOI     0x20
#define PIT_DIVISOR 1193   /* 1193182 / 1193 ~= 1000 Hz */
#define CHAIN_EVERY 55     /* 1000 / 18.2 ~= 55 */

static volatile unsigned long ms_counter = 0;
static volatile unsigned int  chain_count = 0;
static void (__interrupt __far *old_timer)(void);

static void __interrupt __far timer_handler(void)
{
    ms_counter++;
    chain_count++;
    if (chain_count >= CHAIN_EVERY) {
        chain_count = 0;
        _chain_intr(old_timer);
    } else {
        outp(PIC_CMD, PIC_EOI);
    }
}

void timer_init(void)
{
    ms_counter = 0;
    chain_count = 0;
    old_timer = _dos_getvect(TIMER_INT);

    outp(PIT_CMD, 0x36);
    outp(PIT_CH0, PIT_DIVISOR & 0xFF);
    outp(PIT_CH0, (PIT_DIVISOR >> 8) & 0xFF);

    _dos_setvect(TIMER_INT, timer_handler);
}

void timer_shutdown(void)
{
    _dos_setvect(TIMER_INT, old_timer);

    /* Restore BIOS default: divisor 0 means 65536 = 18.2 Hz */
    outp(PIT_CMD, 0x36);
    outp(PIT_CH0, 0);
    outp(PIT_CH0, 0);
}

unsigned long timer_ms(void)
{
    return ms_counter;
}
