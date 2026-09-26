#include "note_mapper.h"

static const int PITCH_TO_DEGREE[12] = {
    0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6
};

void mapper_init(NoteMapper* m, int baseOctave, int transpose, int mode)
{
    m->baseOctave = baseOctave;
    m->transpose = transpose;
    m->mode = mode;
}

int mapper_map(const NoteMapper* m, int midiNoteNumber)
{
    int adjusted = midiNoteNumber;
    int low  = m->baseOctave * 12;
    int high = m->baseOctave * 12 + 35;

    if (m->mode == MODE_EDGE) {
        if (adjusted < low) {
            int shift = ((low - adjusted + 11) / 12) * 12;
            adjusted += shift;
        } else if (adjusted > high) {
            int shift = ((adjusted - high + 11) / 12) * 12;
            adjusted -= shift;
        }
    }

    adjusted += m->transpose;

    if (m->mode == MODE_FULL) {
        /* full compression handled externally in gui.c */
    }

    if (m->mode == MODE_EDGE) {
        if (adjusted < low) {
            int shift = ((low - adjusted + 11) / 12) * 12;
            adjusted += shift;
        } else if (adjusted > high) {
            int shift = ((adjusted - high + 11) / 12) * 12;
            adjusted -= shift;
        }
    } else if (m->mode == MODE_IGNORE) {
        if (adjusted < low || adjusted > high) return -1;
    }

    int octave = adjusted / 12 - 1;
    int pc = ((adjusted % 12) + 12) % 12;
    int degree = PITCH_TO_DEGREE[pc];
    int row = ((octave - m->baseOctave) % 3 + 3) % 3;
    return row * 7 + degree;
}
