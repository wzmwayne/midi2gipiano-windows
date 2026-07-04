#ifndef PLAYBACK_H
#define PLAYBACK_H

#include "midi_parser.h"
#include "note_mapper.h"

typedef enum { STOPPED, PLAYING, PAUSED } PlaybackState;

typedef struct {
    ParsedMidi   midi;
    NoteMapper   mapper;
    PlaybackState state;

    int eventCount;
    int eventCapacity;
    int currentEvent;

    long long pauseAtMicros;
    int exitRequested;
} PlaybackEngine;

int  pb_init(PlaybackEngine* pb, const char* midiFile,
             int baseOctave, int transpose);
void pb_play(PlaybackEngine* pb);
void pb_stop(PlaybackEngine* pb);
void pb_free(PlaybackEngine* pb);

#endif
