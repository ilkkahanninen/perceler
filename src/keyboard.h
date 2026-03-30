#ifndef KEYBOARD_H
#define KEYBOARD_H

/* Common scancodes */
#define KEY_ESC 0x01
#define KEY_SPACE 0x39
#define KEY_UP 0x48
#define KEY_DOWN 0x50
#define KEY_LEFT 0x4B
#define KEY_RIGHT 0x4D
#define KEY_ENTER 0x1C

void keyboard_init(void);
void keyboard_shutdown(void);
int key_pressed(unsigned char scancode);

#endif
