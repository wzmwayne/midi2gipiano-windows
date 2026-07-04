#ifndef KEY_INPUT_H
#define KEY_INPUT_H

#include <windows.h>
#include <stdbool.h>

#define KEY_COUNT 21

int key_init(void);
void key_press(int keyIndex);
void key_release(int keyIndex);
void key_release_all(void);
const char* key_name(int keyIndex);
char key_letter(int keyIndex);

#endif
