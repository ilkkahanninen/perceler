/*
 * Custom keyboard interrupt handler (IRQ1 / INT 9)
 *
 * Replaces the BIOS keyboard handler to track key up/down state
 * for all keys simultaneously. Call keyboard_init() at startup
 * and keyboard_shutdown() before exiting.
 */

#include "keyboard.h"

#include <conio.h>
#include <dos.h>

#define KEYBOARD_INT 0x09
#define KEYBOARD_PORT 0x60
#define PIC_PORT 0x20

static volatile unsigned char keys[128];
static unsigned char prev_keys[128];
static void(__interrupt __far *old_handler)(void);

static void __interrupt __far keyboard_handler(void)
{
  unsigned char scancode;

  scancode = inp(KEYBOARD_PORT);

  if (scancode & 0x80)
  {
    /* Key released (bit 7 set) */
    keys[scancode & 0x7F] = 0;
    prev_keys[scancode & 0x7F] = 0;
  }
  else
  {
    /* Key pressed */
    keys[scancode] = 1;
  }

  /* Acknowledge interrupt to PIC */
  outp(PIC_PORT, 0x20);
}

void keyboard_init(void)
{
  int i;
  for (i = 0; i < 128; i++)
  {
    keys[i] = 0;
    prev_keys[i] = 0;
  }

  old_handler = _dos_getvect(KEYBOARD_INT);
  _dos_setvect(KEYBOARD_INT, keyboard_handler);
}

void keyboard_shutdown(void)
{
  if (old_handler)
  {
    _dos_setvect(KEYBOARD_INT, old_handler);
    old_handler = 0;
  }
}

int key_down(unsigned char scancode)
{
  return keys[scancode & 0x7F];
}

int key_pressed(unsigned char scancode)
{
  unsigned char idx = scancode & 0x7F;
  if (keys[idx] && !prev_keys[idx])
  {
    prev_keys[idx] = 1;
    return 1;
  }
  if (!keys[idx])
  {
    prev_keys[idx] = 0;
  }
  return 0;
}
