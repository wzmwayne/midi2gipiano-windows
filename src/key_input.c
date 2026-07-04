#include "key_input.h"

static const WORD VK_MAP[KEY_COUNT] = {
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U',
    'A', 'S', 'D', 'F', 'G', 'H', 'J',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M'
};

static const WORD SCAN_MAP[KEY_COUNT] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24,
    0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32
};

static const char* KEY_NAMES[KEY_COUNT] = {
    "do", "re", "mi", "fa", "so", "la", "ti",
    "do", "re", "mi", "fa", "so", "la", "ti",
    "do", "re", "mi", "fa", "so", "la", "ti"
};

static const char KEY_LETTERS[KEY_COUNT] = {
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U',
    'A', 'S', 'D', 'F', 'G', 'H', 'J',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M'
};

static bool pressed[KEY_COUNT] = {0};
static int useScanCodes = 0;

int key_init(void)
{
    for (int i = 0; i < KEY_COUNT; i++) {
        pressed[i] = false;
    }
    return 1;
}

const char* key_name(int keyIndex)
{
    if (keyIndex < 0 || keyIndex >= KEY_COUNT) return "?";
    return KEY_NAMES[keyIndex];
}

char key_letter(int keyIndex)
{
    if (keyIndex < 0 || keyIndex >= KEY_COUNT) return '?';
    return KEY_LETTERS[keyIndex];
}

static int send_key(int keyIndex, int down)
{
    INPUT input;
    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_KEYBOARD;

    if (useScanCodes) {
        input.ki.wVk = 0;
        input.ki.wScan = SCAN_MAP[keyIndex];
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        if (!down) input.ki.dwFlags |= KEYEVENTF_KEYUP;
    } else {
        input.ki.wVk = VK_MAP[keyIndex];
        input.ki.wScan = 0;
        input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    }

    UINT result = SendInput(1, &input, sizeof(INPUT));

    if (result != 1 && !useScanCodes) {
        useScanCodes = 1;
        return send_key(keyIndex, down);
    }
    return (result == 1) ? 1 : 0;
}

void key_press(int keyIndex)
{
    if (keyIndex < 0 || keyIndex >= KEY_COUNT) return;
    if (pressed[keyIndex]) return;
    pressed[keyIndex] = true;
    send_key(keyIndex, 1);
}

void key_release(int keyIndex)
{
    if (keyIndex < 0 || keyIndex >= KEY_COUNT) return;
    if (!pressed[keyIndex]) return;
    pressed[keyIndex] = false;
    send_key(keyIndex, 0);
}

void key_release_all(void)
{
    for (int i = 0; i < KEY_COUNT; i++) {
        if (pressed[i]) {
            pressed[i] = false;
            send_key(i, 0);
        }
    }
}
