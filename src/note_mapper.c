#include "note_mapper.h"

static const int PITCH_TO_DEGREE[12] = {
    0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6
};

void mapper_init(NoteMapper* m, int baseOctave, int transpose)
{
    m->baseOctave = baseOctave;
    m->transpose = transpose;
}

int mapper_map(const NoteMapper* m, int midiNoteNumber)
{
    int adjusted = midiNoteNumber + m->transpose;
    int octave = adjusted / 12 - 1;
    int pc = ((adjusted % 12) + 12) % 12;
    int degree = PITCH_TO_DEGREE[pc];
    int row = ((octave - m->baseOctave) % 3 + 3) % 3;
    return row * 7 + degree;
}
