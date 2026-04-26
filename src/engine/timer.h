#ifndef TIMER_H
#define TIMER_H

/* Install the millisecond-resolution timer handler. */
void timer_init(void);

/* Restore the previous timer handler. */
void timer_shutdown(void);

/* Milliseconds elapsed since timer_init(). Wraps after ~49 days. */
unsigned long timer_ms(void);

#endif
