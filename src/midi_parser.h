#ifndef MIDI_PARSER_H
#define MIDI_PARSER_H

typedef struct {
    int  noteNumber;
    int  velocity;
    int  channel;
    long long startMicros;
    long long endMicros;
} MidiNote;

typedef struct {
    MidiNote* notes;
    int       noteCount;
    int       noteCapacity;
    long long totalDurationMicros;
    int       trackCount;
    int       ppq;
} ParsedMidi;

int  midi_parse(const char* filepath, ParsedMidi* out);
void midi_free(ParsedMidi* pm);

#endif
