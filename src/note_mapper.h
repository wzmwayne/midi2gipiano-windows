#ifndef NOTE_MAPPER_H
#define NOTE_MAPPER_H

#include "key_input.h"

enum { MODE_IGNORE = 0, MODE_EDGE = 1, MODE_FULL = 2 };

typedef struct {
    int baseOctave;
    int transpose;
    int mode;
} NoteMapper;

void mapper_init(NoteMapper* m, int baseOctave, int transpose, int mode);
int  mapper_map(const NoteMapper* m, int midiNoteNumber);

#endif
