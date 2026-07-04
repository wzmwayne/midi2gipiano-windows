#include "gui.h"
#include "playback.h"
#include "key_input.h"
#include "note_mapper.h"
#include "midi_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    ID_EDIT_FILE      = 1001,
    ID_EDIT_OCTAVE    = 1002,
    ID_EDIT_TRANSPOSE = 1003,
    ID_BTN_BROWSE     = 1004,
    ID_BTN_PLAY       = 1005,
    ID_BTN_PAUSE      = 1006,
    ID_BTN_STOP       = 1007,
    ID_BTN_EXIT       = 1008,
    ID_STATIC_STATUS  = 1009,
    ID_STATIC_NOTE    = 1010,
    ID_STATIC_TIME    = 1011,
    ID_STATIC_PROGRESS = 1012,

    HOTKEY_ID_TOGGLE  = 1,
};

static HWND hEditFile, hEditOctave, hEditTranspose;
static HWND hBtnBrowse, hBtnPlay, hBtnPause, hBtnStop;
static HWND hStaticStatus, hStaticNote, hStaticTime, hStaticProgress;

static HFONT hFont = NULL;

static HANDLE hPlayThread  = NULL;
static HANDLE hEventStop   = NULL;
static HANDLE hEventPause  = NULL;
static HANDLE hEventResume = NULL;

static long long g_totalDurationMicros = 0;

static long long timer_freq = 0;

static void timer_init(void)
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    timer_freq = freq.QuadPart;
}

static long long timer_now(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart * 1000000LL / timer_freq;
}

static HWND CreateLabel(HWND parent, int id, const wchar_t* text,
                        int x, int y, int w, int h)
{
    return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                         x, y, w, h, parent, (HMENU)(INT_PTR)id,
                         GetModuleHandleW(NULL), NULL);
}

static HWND CreateEdit(HWND parent, int id, const wchar_t* text,
                       int x, int y, int w, int h)
{
    return CreateWindowW(L"EDIT", text,
                         WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                         x, y, w, h, parent, (HMENU)(INT_PTR)id,
                         GetModuleHandleW(NULL), NULL);
}

static HWND CreateButtonW(HWND parent, int id, const wchar_t* text,
                          int x, int y, int w, int h)
{
    return CreateWindowW(L"BUTTON", text,
                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                         x, y, w, h, parent, (HMENU)(INT_PTR)id,
                         GetModuleHandleW(NULL), NULL);
}

#define WM_USER_UPDATE  (WM_USER + 100)
#define WM_USER_DONE    (WM_USER + 101)
#define WM_USER_ERROR   (WM_USER + 102)
#define WM_USER_TOTAL   (WM_USER + 103)

typedef struct {
    HWND    hWnd;
    wchar_t filePath[MAX_PATH];
    int     baseOctave;
    int     transpose;
} ThreadParam;

typedef enum { EV_PRESS, EV_RELEASE } EventType;

typedef struct {
    long long  time;
    EventType  type;
    int        keyIndex;
    int        midiNote;
} PlaybackEvent;

static int event_cmp(const void* a, const void* b)
{
    long long d = ((const PlaybackEvent*)a)->time - ((const PlaybackEvent*)b)->time;
    if (d < 0) return -1;
    if (d > 0) return  1;
    return 0;
}

static DWORD WINAPI PlaybackThreadProc(LPVOID lpParam)
{
    ThreadParam* tp = (ThreadParam*)lpParam;
    HWND hWnd = tp->hWnd;

    if (!timer_freq) timer_init();

    char filePathA[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, tp->filePath, -1,
                        filePathA, MAX_PATH, NULL, NULL);

    ParsedMidi midi;
    if (!midi_parse(filePathA, &midi)) {
        PostMessageW(hWnd, WM_USER_ERROR, 0, 0);
        free(tp);
        return 1;
    }

    NoteMapper mapper;
    mapper_init(&mapper, tp->baseOctave, tp->transpose);

    if (!key_init()) {
        midi_free(&midi);
        PostMessageW(hWnd, WM_USER_ERROR, 0, 0);
        free(tp);
        return 1;
    }

    PostMessageW(hWnd, WM_USER_TOTAL, 0, (LPARAM)midi.totalDurationMicros);

    /* build sorted event array */
    PlaybackEvent* events = malloc(midi.noteCount * 2 * sizeof(PlaybackEvent));
    int evCount = 0;

    for (int i = 0; i < midi.noteCount; i++) {
        MidiNote* mn = &midi.notes[i];
        int ki = mapper_map(&mapper, mn->noteNumber);
        if (ki < 0 || ki >= KEY_COUNT) continue;
        events[evCount++] = (PlaybackEvent){mn->startMicros, EV_PRESS,   ki, mn->noteNumber};
        events[evCount++] = (PlaybackEvent){mn->endMicros,   EV_RELEASE, ki, mn->noteNumber};
    }

    qsort(events, evCount, sizeof(PlaybackEvent), event_cmp);

    /* track pressed keys for pause/re-press */
    int pressed[KEY_COUNT];
    memset(pressed, 0, sizeof(pressed));

    /* create waitable timer */
    HANDLE hTimer = CreateWaitableTimerW(NULL, TRUE, NULL);

    long long t0 = timer_now();

    for (int i = 0; i < evCount; i++) {
        PlaybackEvent* ev = &events[i];

        while (1) {
            long long elapsed = timer_now() - t0;
            long long remaining = ev->time - elapsed;

            if (remaining <= 0) {
                if (ev->type == EV_PRESS) {
                    key_press(ev->keyIndex);
                    pressed[ev->keyIndex] = 1;
                    PostMessageW(hWnd, WM_USER_UPDATE, 2,
                                 MAKELPARAM(ev->midiNote, ev->keyIndex));
                } else {
                    key_release(ev->keyIndex);
                    pressed[ev->keyIndex] = 0;
                    PostMessageW(hWnd, WM_USER_UPDATE, 3, (LPARAM)ev->time);
                }
                break;
            }

            LARGE_INTEGER due;
            due.QuadPart = -(remaining * 10);
            SetWaitableTimer(hTimer, &due, 0, NULL, NULL, FALSE);

            HANDLE wa[] = { hTimer, hEventStop, hEventPause };
            DWORD r = WaitForMultipleObjects(3, wa, FALSE, INFINITE);

            if (r == WAIT_OBJECT_0) {
                if (ev->type == EV_PRESS) {
                    key_press(ev->keyIndex);
                    pressed[ev->keyIndex] = 1;
                    PostMessageW(hWnd, WM_USER_UPDATE, 2,
                                 MAKELPARAM(ev->midiNote, ev->keyIndex));
                } else {
                    key_release(ev->keyIndex);
                    pressed[ev->keyIndex] = 0;
                    PostMessageW(hWnd, WM_USER_UPDATE, 3, (LPARAM)ev->time);
                }
                break;
            }

            if (r == WAIT_OBJECT_0 + 1) goto cleanup;           /* stop */
            if (r == WAIT_OBJECT_0 + 2) {                        /* pause */
                ResetEvent(hEventPause);
                long long pauseTime = timer_now();
                key_release_all();
                PostMessageW(hWnd, WM_USER_UPDATE, 5, 0);

                HANDLE rv[] = { hEventStop, hEventResume };
                DWORD rr = WaitForMultipleObjects(2, rv, FALSE, INFINITE);
                if (rr == WAIT_OBJECT_0) goto cleanup;

                t0 += timer_now() - pauseTime;

                for (int k = 0; k < KEY_COUNT; k++)
                    if (pressed[k]) key_press(k);

                ResetEvent(hEventResume);
                PostMessageW(hWnd, WM_USER_UPDATE, 6, 0);
            }
        }
    }

cleanup:
    key_release_all();
    midi_free(&midi);
    free(events);
    if (hTimer) CloseHandle(hTimer);
    PostMessageW(hWnd, WM_USER_DONE, 0, 0);
    free(tp);
    return 0;
}

static void BeginPlayback(HWND hWnd)
{
    if (hPlayThread) return;

    wchar_t filePathW[MAX_PATH];
    GetWindowTextW(hEditFile, filePathW, MAX_PATH);
    if (filePathW[0] == 0) {
        SetWindowTextW(hStaticStatus, L"\u8BF7\u5148\u9009\u62E9 MIDI \u6587\u4EF6");
        return;
    }

    wchar_t buf[16];
    GetWindowTextW(hEditOctave, buf, 16);
    int octave = _wtoi(buf);
    if (octave < 0) octave = 0;
    if (octave > 9) octave = 9;

    GetWindowTextW(hEditTranspose, buf, 16);
    int transpose = _wtoi(buf);
    if (transpose < -24) transpose = -24;
    if (transpose > 24) transpose = 24;

    if (!hEventStop)   hEventStop   = CreateEventW(NULL, TRUE,  FALSE, NULL);
    if (!hEventPause)  hEventPause  = CreateEventW(NULL, TRUE,  FALSE, NULL);
    if (!hEventResume) hEventResume = CreateEventW(NULL, FALSE, FALSE, NULL);

    ResetEvent(hEventStop);
    ResetEvent(hEventPause);
    ResetEvent(hEventResume);

    ThreadParam* tp = malloc(sizeof(ThreadParam));
    tp->hWnd = hWnd;
    wcsncpy(tp->filePath, filePathW, MAX_PATH);
    tp->baseOctave = octave;
    tp->transpose  = transpose;

    hPlayThread = CreateThread(NULL, 0, PlaybackThreadProc, tp, 0, NULL);
    if (!hPlayThread) {
        SetWindowTextW(hStaticStatus, L"\u521B\u5EFA\u64AD\u653E\u7EBF\u7A0B\u5931\u8D25");
        free(tp);
        return;
    }

    EnableWindow(hBtnPlay,  FALSE);
    EnableWindow(hBtnPause, TRUE);
    EnableWindow(hBtnStop,  TRUE);
    EnableWindow(hBtnBrowse, FALSE);
    EnableWindow(hEditFile, FALSE);
    SetWindowTextW(hStaticStatus, L"\u25B6 \u64AD\u653E\u4E2D");
}

static void DoStopPlayback(HWND hWnd)
{
    (void)hWnd;
    if (hEventStop) SetEvent(hEventStop);

    if (hPlayThread) {
        if (WaitForSingleObject(hPlayThread, 3000) == WAIT_TIMEOUT)
            TerminateThread(hPlayThread, 1);
        CloseHandle(hPlayThread);
        hPlayThread = NULL;
    }

    key_release_all();
    EnableWindow(hBtnPlay,  TRUE);
    EnableWindow(hBtnPause, FALSE);
    EnableWindow(hBtnStop,  FALSE);
    EnableWindow(hBtnBrowse, TRUE);
    EnableWindow(hEditFile, TRUE);
    SetWindowTextW(hBtnPlay, L"\u25B6 \u64AD\u653E");

    SetWindowTextW(hStaticStatus,   L"\u23F9 \u5DF2\u505C\u6B62");
    SetWindowTextW(hStaticNote,     L"");
    SetWindowTextW(hStaticTime,     L"");
    SetWindowTextW(hStaticProgress, L"");
}

static void DoPause(void)
{
    if (!hPlayThread) return;
    SetEvent(hEventPause);
    EnableWindow(hBtnPause, FALSE);
    SetWindowTextW(hStaticStatus, L"\u23F8 \u6682\u505C\u4E2D");
}

static void TogglePlayPause(HWND hWnd)
{
    if (!hPlayThread) {
        BeginPlayback(hWnd);
        return;
    }

    wchar_t status[64];
    GetWindowTextW(hStaticStatus, status, 64);

    if (wcsstr(status, L"\u6682\u505C")) {
        SetEvent(hEventResume);
        SetWindowTextW(hStaticStatus, L"\u25B6 \u64AD\u653E\u4E2D");
        EnableWindow(hBtnPause, TRUE);
    } else {
        SetEvent(hEventPause);
        EnableWindow(hBtnPause, FALSE);
        SetWindowTextW(hStaticStatus, L"\u23F8 \u6682\u505C\u4E2D");
    }
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_CREATE: {
        hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        if (!hFont) hFont = GetStockObject(DEFAULT_GUI_FONT);

        RegisterHotKey(hWnd, HOTKEY_ID_TOGGLE,
                       MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, '1');

        int y = 10;

        CreateLabel(hWnd, 0, L"MIDI \u6587\u4EF6:", 10, y+4, 70, 20);
        hEditFile = CreateEdit(hWnd, ID_EDIT_FILE, L"", 85, y, 220, 24);
        SendMessageW(hEditFile, WM_SETFONT, (WPARAM)hFont, 0);
        hBtnBrowse = CreateButtonW(hWnd, ID_BTN_BROWSE, L"\u6D4F\u89C8", 310, y, 50, 24);
        SendMessageW(hBtnBrowse, WM_SETFONT, (WPARAM)hFont, 0);
        y += 35;

        CreateLabel(hWnd, 0, L"\u57FA\u51C6\u516B\u5EA6:", 10, y+4, 60, 20);
        hEditOctave = CreateEdit(hWnd, ID_EDIT_OCTAVE, L"4", 75, y, 35, 24);
        SendMessageW(hEditOctave, WM_SETFONT, (WPARAM)hFont, 0);

        CreateLabel(hWnd, 0, L"\u79FB\u8C03:", 130, y+4, 35, 20);
        hEditTranspose = CreateEdit(hWnd, ID_EDIT_TRANSPOSE, L"0", 165, y, 35, 24);
        SendMessageW(hEditTranspose, WM_SETFONT, (WPARAM)hFont, 0);
        y += 40;

        hBtnPlay  = CreateButtonW(hWnd, ID_BTN_PLAY,  L"\u25B6 \u64AD\u653E",  10, y, 80, 30);
        hBtnPause = CreateButtonW(hWnd, ID_BTN_PAUSE, L"\u23F8 \u6682\u505C",  95, y, 80, 30);
        hBtnStop  = CreateButtonW(hWnd, ID_BTN_STOP,  L"\u23F9 \u505C\u6B62", 180, y, 80, 30);
        CreateButtonW(hWnd, ID_BTN_EXIT, L"\u2715 \u9000\u51FA", 280, y, 80, 30);
        SendMessageW(hBtnPlay,  WM_SETFONT, (WPARAM)hFont, 0);
        SendMessageW(hBtnPause, WM_SETFONT, (WPARAM)hFont, 0);
        SendMessageW(hBtnStop,  WM_SETFONT, (WPARAM)hFont, 0);
        SendMessageW(GetDlgItem(hWnd, ID_BTN_EXIT), WM_SETFONT, (WPARAM)hFont, 0);
        EnableWindow(hBtnPause, FALSE);
        EnableWindow(hBtnStop,  FALSE);
        y += 45;

        hStaticStatus = CreateLabel(hWnd, ID_STATIC_STATUS, L"\u5C31\u7EEA", 10, y, 350, 22);
        SendMessageW(hStaticStatus, WM_SETFONT, (WPARAM)hFont, 0);
        y += 28;

        hStaticNote = CreateLabel(hWnd, ID_STATIC_NOTE, L"", 10, y, 350, 22);
        SendMessageW(hStaticNote, WM_SETFONT, (WPARAM)hFont, 0);
        y += 22;

        hStaticTime = CreateLabel(hWnd, ID_STATIC_TIME, L"", 10, y, 350, 22);
        SendMessageW(hStaticTime, WM_SETFONT, (WPARAM)hFont, 0);
        y += 22;

        hStaticProgress = CreateLabel(hWnd, ID_STATIC_PROGRESS, L"", 10, y, 350, 22);
        SendMessageW(hStaticProgress, WM_SETFONT, (WPARAM)hFont, 0);

        SetWindowTextW(hStaticStatus, L"\u5C31\u7EEA \u2014 Ctrl+Shift+1 \u5207\u6362\u64AD\u653E/\u6682\u505C");
        return 0;
    }

    case WM_HOTKEY: {
        if (wParam == HOTKEY_ID_TOGGLE)
            TogglePlayPause(hWnd);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);

        switch (id) {
        case ID_BTN_BROWSE: {
            OPENFILENAMEW ofn;
            wchar_t file[MAX_PATH] = {0};
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hWnd;
            ofn.lpstrFilter = L"MIDI \u6587\u4EF6\0*.mid;*.midi\0\u6240\u6709\u6587\u4EF6\0*.*\0";
            ofn.lpstrFile   = file;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
            if (GetOpenFileNameW(&ofn))
                SetWindowTextW(hEditFile, file);
            break;
        }
        case ID_BTN_PLAY:
            BeginPlayback(hWnd);
            break;
        case ID_BTN_PAUSE:
            DoPause();
            break;
        case ID_BTN_STOP:
            DoStopPlayback(hWnd);
            break;
        case ID_BTN_EXIT:
            DoStopPlayback(hWnd);
            DestroyWindow(hWnd);
            break;
        }
        return 0;
    }

    case WM_USER_UPDATE: {
        int type = (int)wParam;
        switch (type) {
        case 2: {
            int midiNote = LOWORD(lParam);
            int keyIdx   = HIWORD(lParam);
            wchar_t buf[128];
            swprintf(buf, 128, L"\u5F53\u524D: %c (%s)  MIDI: %d  \u952E\u4F4D: %d",
                     key_letter(keyIdx), key_name(keyIdx), midiNote, keyIdx);
            SetWindowTextW(hStaticNote, buf);
            break;
        }
        case 3: {
            long long pos = (long long)lParam;
            long long total = g_totalDurationMicros;
            if (total > 0) {
                int pct = (int)(pos * 100 / total);
                wchar_t buf[64];
                swprintf(buf, 64, L"\u8FDB\u5EA6: %d%%", pct);
                SetWindowTextW(hStaticProgress, buf);

                int sec = (int)(pos / 1000000);
                int totalSec = (int)(total / 1000000);
                swprintf(buf, 64, L"\u65F6\u95F4: %02d:%02d / %02d:%02d",
                         sec/60, sec%60, totalSec/60, totalSec%60);
                SetWindowTextW(hStaticTime, buf);
            }
            break;
        }
        case 5:
            SetWindowTextW(hStaticStatus, L"\u23F8 \u6682\u505C\u4E2D");
            EnableWindow(hBtnPause, FALSE);
            EnableWindow(hBtnPlay,  TRUE);
            SetWindowTextW(hBtnPlay, L"\u25B6 \u7EE7\u7EED");
            break;
        case 6:
            SetWindowTextW(hStaticStatus, L"\u25B6 \u64AD\u653E\u4E2D");
            EnableWindow(hBtnPause, TRUE);
            EnableWindow(hBtnPlay,  FALSE);
            break;
        }
        return 0;
    }

    case WM_USER_DONE: {
        EnableWindow(hBtnPlay,  TRUE);
        EnableWindow(hBtnPause, FALSE);
        EnableWindow(hBtnStop,  FALSE);
        EnableWindow(hBtnBrowse, TRUE);
        EnableWindow(hEditFile, TRUE);
        SetWindowTextW(hBtnPlay, L"\u25B6 \u64AD\u653E");

        SetWindowTextW(hStaticStatus,   L"\u2705 \u64AD\u653E\u5B8C\u6210");
        SetWindowTextW(hStaticProgress, L"\u8FDB\u5EA6: 100%");

        if (hPlayThread) {
            CloseHandle(hPlayThread);
            hPlayThread = NULL;
        }
        return 0;
    }

    case WM_USER_ERROR: {
        DoStopPlayback(hWnd);
        SetWindowTextW(hStaticStatus, L"\u274C \u89E3\u6790 MIDI \u5931\u8D25");
        return 0;
    }

    case WM_USER_TOTAL:
        g_totalDurationMicros = (long long)lParam;
        return 0;

    case WM_DESTROY:
        UnregisterHotKey(hWnd, HOTKEY_ID_TOGGLE);
        DoStopPlayback(hWnd);
        if (hEventStop)   CloseHandle(hEventStop);
        if (hEventPause)  CloseHandle(hEventPause);
        if (hEventResume) CloseHandle(hEventResume);
        if (hFont && hFont != GetStockObject(DEFAULT_GUI_FONT))
            DeleteObject(hFont);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int GuiRun(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSW wc = {0};
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)MAKEINTRESOURCE(32512));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"Midi2GiPianoClass";

    if (!RegisterClassW(&wc)) return 1;

    HWND hWnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName,
                                L"MIDI \u2192 \u98CE\u7269\u4E4B\u8BD7\u7434 \u81EA\u52A8\u6F14\u594F",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                400, 290, NULL, NULL, hInstance, NULL);
    if (!hWnd) return 1;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
