#ifndef KEYBOARD_H
#define KEYBOARD_H

/* Common BIOS scancodes. */
#define KEY_ESC   0x01
#define KEY_SPACE 0x39
#define KEY_UP    0x48
#define KEY_DOWN  0x50
#define KEY_LEFT  0x4B
#define KEY_RIGHT 0x4D
#define KEY_ENTER 0x1C

/* Install the keyboard handler. */
void keyboard_init(void);

/* Restore the previous keyboard handler. */
void keyboard_shutdown(void);

/* Returns non-zero while `scancode` is held. */
int key_down(unsigned char scancode);

/* Returns non-zero once per key-press transition for `scancode`. */
int key_pressed(unsigned char scancode);

#endif
