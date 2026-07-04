#ifndef NOTE_MAPPER_H
#define NOTE_MAPPER_H

#include "key_input.h"

typedef struct {
    int baseOctave;
    int transpose;
} NoteMapper;

void mapper_init(NoteMapper* m, int baseOctave, int transpose);
int  mapper_map(const NoteMapper* m, int midiNoteNumber);

#endif
