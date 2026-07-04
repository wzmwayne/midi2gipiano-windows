#include "playback.h"
#include "key_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <conio.h>

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

/* ── interactive input check ────────────────────────── */

static int check_input(PlaybackEngine* pb, long long t0, long long offset)
{
    /* [S]top */
    if (_kbhit()) {
        int ch = _getch();
        if (ch == 'S' || ch == 's') {
            printf("\n用户停止\n");
            pb->state = STOPPED;
            key_release_all();
            return 1;
        }
        if (ch == 'X' || ch == 'x') {
            printf("\n用户退出\n");
            pb->exitRequested = 1;
            pb->state = STOPPED;
            key_release_all();
            return 1;
        }
    }

    /* [Pause] Ctrl+Shift+1 */
    {
        static int pauseWasDown = 0;
        int pauseIsDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                          (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
                          (GetAsyncKeyState('1') & 0x8000);
        if (pauseIsDown && !pauseWasDown) {
            pauseWasDown = 1;
            long long now = timer_now();
            pb->pauseAtMicros = offset + (now - t0);
            printf("\n暂停 (%.1fs)\n", pb->pauseAtMicros / 1000000.0);
            pb->state = PAUSED;
            key_release_all();
            return 1;
        }
        pauseWasDown = pauseIsDown;
    }

    return 0;
}

/* busy-wait with input checking; returns 1 if stopped/paused/exited */
static int wait_us(long long us, PlaybackEngine* pb, long long t0, long long offset)
{
    if (us <= 0) return 0;
    long long target = timer_now() + us;
    while (timer_now() < target) {
        if (check_input(pb, t0, offset)) return 1;
        if (us > 2000) Sleep(0);
    }
    return 0;
}

/* ── public API ────────────────────────────────────── */

int pb_init(PlaybackEngine* pb, const char* midiFile,
            int baseOctave, int transpose)
{
    memset(pb, 0, sizeof(*pb));

    if (!midi_parse(midiFile, &pb->midi)) {
        return 0;
    }

    mapper_init(&pb->mapper, baseOctave, transpose);
    pb->state = STOPPED;
    pb->pauseAtMicros = 0;
    pb->exitRequested = 0;
    return 1;
}

void pb_play(PlaybackEngine* pb)
{
    while (1) {
        if (pb->state == PLAYING) return;
        if (pb->exitRequested) return;

        if (!timer_freq) timer_init();

        long long t0 = timer_now();
        pb->state = PLAYING;

        int n = pb->midi.noteCount;
        long long offset = pb->pauseAtMicros;
        pb->pauseAtMicros = 0;

        int start = 0;
        while (start < n && pb->midi.notes[start].endMicros <= offset)
            start++;

        printf("\n  [S]停止  [Pause]暂停  [X]退出\n\n");

        for (int i = start; i < n; i++) {
            if (pb->state != PLAYING) break;

            if (check_input(pb, t0, offset)) break;

            MidiNote* mn = &pb->midi.notes[i];
            int ki = mapper_map(&pb->mapper, mn->noteNumber);
            if (ki < 0 || ki >= KEY_COUNT) continue;

            long long pressAt  = t0 + mn->startMicros - offset;
            long long releaseAt = t0 + mn->endMicros - offset;

            if (wait_us(pressAt - timer_now(), pb, t0, offset)) break;

            if (pb->state != PLAYING) break;
            key_press(ki);
            printf("\r  按下: %c (%s)  MIDI: %d  键位: %d    ",
                   key_letter(ki), key_name(ki), mn->noteNumber, ki);
            fflush(stdout);

            if (wait_us(releaseAt - timer_now(), pb, t0, offset)) break;

            if (pb->state != PLAYING) break;
            key_release(ki);
        }

        /* normal completion (not stopped/paused) */
        if (pb->state == PLAYING) {
            long long endAt = t0 + pb->midi.totalDurationMicros - offset;
            long long now = timer_now();
            if (now < endAt)
                Sleep((DWORD)((endAt - now) / 1000));
            key_release_all();
            pb->state = STOPPED;
            printf("\n播放完成\n");
            return;
        }

        /* paused — wait here for resume/stop/exit */
        if (pb->state == PAUSED && !pb->exitRequested) {
            printf("  [Ctrl+Shift+1]继续  [S]停止  [X]退出\n\n");
            while (pb->state == PAUSED && !pb->exitRequested) {
                if (_kbhit()) {
                    int ch = _getch();
                    if (ch == 'S' || ch == 's') {
                        pb->state = STOPPED;
                        key_release_all();
                    } else if (ch == 'X' || ch == 'x') {
                        pb->exitRequested = 1;
                        pb->state = STOPPED;
                        key_release_all();
                    }
                }
                /* Ctrl+Shift+1 → resume */
                static int resumeWasDown = 0;
                int resumeIsDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                                   (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
                                   (GetAsyncKeyState('1') & 0x8000);
                if (resumeIsDown && !resumeWasDown) {
                    printf("\n继续播放\n\n");
                    pb->state = STOPPED; /* will become PLAYING on next loop */
                    break;
                }
                resumeWasDown = resumeIsDown;
                Sleep(50);
            }
        }
    }
}

void pb_stop(PlaybackEngine* pb)
{
    pb->state = STOPPED;
    key_release_all();
}

void pb_free(PlaybackEngine* pb)
{
    pb_stop(pb);
    midi_free(&pb->midi);
}
