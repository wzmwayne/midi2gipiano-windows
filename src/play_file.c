#include "play_file.h"
#include "key_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int play_file_parse(const char* filepath, ParsedPlayFile* out)
{
    FILE* f = fopen(filepath, "r");
    if (!f) return 0;

    int capacity = 1024;
    out->notes = malloc(capacity * sizeof(PlayNote));
    out->noteCount = 0;
    out->totalDurationMicros = 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        long pressMs = 0, releaseMs = 0;
        char keyChar = 0;

        if (sscanf(p, "%ld %ld %c", &pressMs, &releaseMs, &keyChar) == 3) {
            int ki = -1;
            if (keyChar != '-' && keyChar != '_') {
                keyChar = toupper((unsigned char)keyChar);
                for (int i = 0; i < KEY_COUNT; i++) {
                    if (KEY_LETTERS[i] == keyChar) { ki = i; break; }
                }
            }

            if (out->noteCount >= capacity) {
                capacity *= 2;
                out->notes = realloc(out->notes, capacity * sizeof(PlayNote));
            }

            out->notes[out->noteCount].keyIndex = ki;
            out->notes[out->noteCount].pressMicros = pressMs * 1000;
            out->notes[out->noteCount].releaseMicros = releaseMs * 1000;
            out->noteCount++;

            if (releaseMs * 1000 > out->totalDurationMicros)
                out->totalDurationMicros = releaseMs * 1000;
        }
    }

    fclose(f);
    return 1;
}

void play_file_free(ParsedPlayFile* pf)
{
    if (pf->notes) { free(pf->notes); pf->notes = NULL; }
    pf->noteCount = 0;
}

int play_file_compile(const ParsedMidi* midi, const NoteMapper* mapper,
                       const char* sourceName, char* outBuf, int outBufSize)
{
    int pos = 0;

    const char* modeName = "IGNORE";
    if (mapper->mode == MODE_EDGE) modeName = "EDGE";
    else if (mapper->mode == MODE_FULL) modeName = "FULL";

    pos += snprintf(outBuf + pos, outBufSize - pos,
        "# midi2gipiano play file\n");
    if (sourceName)
        pos += snprintf(outBuf + pos, outBufSize - pos,
            "# source: %s\n", sourceName);
    pos += snprintf(outBuf + pos, outBufSize - pos,
        "# mode: %s  transpose: %+d  baseOctave: %d\n",
        modeName, mapper->transpose, mapper->baseOctave);
    pos += snprintf(outBuf + pos, outBufSize - pos,
        "# format: <press_ms> <release_ms> <KEY>\n\n");

    for (int i = 0; i < midi->noteCount; i++) {
        int ki = mapper_map(mapper, midi->notes[i].noteNumber);
        if (ki < 0) continue;
        long pressMs = midi->notes[i].startMicros / 1000;
        long releaseMs = midi->notes[i].endMicros / 1000;
        pos += snprintf(outBuf + pos, outBufSize - pos,
            "%ld %ld %c\n", pressMs, releaseMs, KEY_LETTERS[ki]);
    }

    return pos;
}
