#ifndef PLAY_FILE_H
#define PLAY_FILE_H

#include "note_mapper.h"
#include "midi_parser.h"

typedef struct {
    int keyIndex;
    long long pressMicros;
    long long releaseMicros;
} PlayNote;

typedef struct {
    PlayNote* notes;
    int noteCount;
    long long totalDurationMicros;
} ParsedPlayFile;

int  play_file_parse(const char* filepath, ParsedPlayFile* out);
void play_file_free(ParsedPlayFile* pf);
int  play_file_compile(const ParsedMidi* midi, const NoteMapper* mapper,
                       const char* sourceName, char* outBuf, int outBufSize);

#endif
